// Copyright (C) 2015-2017 Crypto Realities Ltd

//  This program is free software: you can redistribute it and/or modify
//  it under the terms of the GNU General Public License as published by
//  the Free Software Foundation, either version 3 of the License, or
//  (at your option) any later version.
//
//  This program is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.
//
//  You should have received a copy of the GNU General Public License
//  along with this program.  If not, see <http://www.gnu.org/licenses/>.

#include "game/tx.h"

#include "base58.h"
#include "coins.h"
#include "game/state.h"
#include "names/common.h"
#include "primitives/transaction.h"
#include "script/standard.h"
#include "undo.h"
#include "util.h"
#include "validation.h"

bool
CreateGameTransactions (const CCoinsView& view, unsigned nHeight,
                        const StepResult& stepResult,
                        std::vector<CTransactionRef>& vGameTx)
{
  vGameTx.clear ();

  /* Destroy name-coins of killed players.  */

  bool haveTxKills = false;
  CMutableTransaction txKills;
  txKills.SetGameTx ();

  const PlayerSet& killedPlayers = stepResult.GetKilledPlayers ();
  const KilledByMap& killedBy = stepResult.GetKilledBy ();
  txKills.vin.reserve (killedPlayers.size ());
  for (const auto& victim : killedPlayers)
    {
      const valtype vchName = ValtypeFromString (victim);
      CNameData data;
      if (!view.GetName (vchName, data))
        return error ("Game engine killed a non-existing player %s",
                      victim.c_str ());

      CTxIn txin(data.getUpdateOutpoint ());

      /* List all killers, if player was simultaneously killed by several
         other players.  If the reason was not KILLED_DESTRUCT, handle
         it also.  If multiple reasons apply, the game tx is constructed
         for the first reason according to the ordering inside of KilledByMap.
         (Which in turn is determined by the enum values for KILLED_*.)  */

      typedef KilledByMap::const_iterator Iter;
      const std::pair<Iter, Iter> iters = killedBy.equal_range (victim);
      if (iters.first == iters.second)
        return error ("No reason for killed player %s", victim.c_str ());
      const KilledByInfo::Reason reason = iters.first->second.reason;

      /* Unless we have destruct, there should be exactly one entry with
         the "first" reason.  There may be multiple entries for different
         reasons, for instance, killed by poison and staying in spawn
         area at the same time.  */
      {
        Iter it = iters.first;
        ++it;
        if (reason != KilledByInfo::KILLED_DESTRUCT && it != iters.second
            && reason == it->second.reason)
          return error ("Multiple same-reason, non-destruct killed-by"
                        " entries for %s", victim.c_str ());
      }

      switch (reason)
        {
        case KilledByInfo::KILLED_DESTRUCT:
          txin.scriptSig << vchName << GAMEOP_KILLED_BY;
          for (Iter it = iters.first; it != iters.second; ++it)
            {
              if (it->second.reason != KilledByInfo::KILLED_DESTRUCT)
                {
                  assert (it != iters.first);
                  break;
                }
              txin.scriptSig
                << ValtypeFromString (it->second.killer.ToString ());
            }
          break;

        case KilledByInfo::KILLED_SPAWN:
          txin.scriptSig << vchName << GAMEOP_KILLED_BY;
          break;

        case KilledByInfo::KILLED_POISON:
          txin.scriptSig << vchName << GAMEOP_KILLED_POISON;
          break;

        default:
          assert (false);
        }

      txKills.vin.push_back (txin);
      haveTxKills = true;
    }
  if (haveTxKills)
    {
      CTransactionRef tx = MakeTransactionRef (std::move (txKills));
      assert (tx->IsGameTx () && !tx->IsBountyTx ());
      vGameTx.push_back (tx);
    }

  /* Pay bounties to the players who collected them.  The transaction
     inputs are just "dummy" containing informational messages.  */

  bool haveTxBounties = false;
  CMutableTransaction txBounties;
  txBounties.SetGameTx ();

  txBounties.vin.reserve (stepResult.bounties.size ());
  txBounties.vout.reserve (stepResult.bounties.size ());

  for (const auto& bounty : stepResult.bounties)
    {
      const valtype vchName = ValtypeFromString (bounty.character.player);
      CNameData data;
      if (!view.GetName (vchName, data))
        return error ("Game engine created bounty for non-existing player");

      CTxOut txout;
      txout.nValue = bounty.loot.nAmount;

      if (!bounty.address.empty ())
        {
          /* Player-provided addresses are validated before accepting them,
             so failing here is ok.  */
          CBitcoinAddress addr(bounty.address);
          if (!addr.IsValid ())
            return error ("Failed to set player-provided address for bounty");
          txout.scriptPubKey = GetScriptForDestination (addr.Get ());
        }
      else
        txout.scriptPubKey = data.getAddress ();

      txBounties.vout.push_back (txout);

      CTxIn txin;
      if (bounty.loot.IsRefund ())
        txin.scriptSig
          << vchName << GAMEOP_REFUND
          << bounty.character.index << bounty.loot.GetRefundHeight ();
      else
        txin.scriptSig
          << vchName << GAMEOP_COLLECTED_BOUNTY
          << bounty.character.index
          << bounty.loot.firstBlock
          << bounty.loot.lastBlock
          << bounty.loot.collectedFirstBlock
          << bounty.loot.collectedLastBlock;
      txBounties.vin.push_back (txin);

      haveTxBounties = true;
    }
  if (haveTxBounties)
    {
      CTransactionRef tx = MakeTransactionRef (std::move (txBounties));
      assert (tx->IsGameTx () && tx->IsBountyTx ());
      vGameTx.push_back (tx);
    }

  /* Print log chatter.  */
  if (haveTxKills || haveTxBounties)
    {
      LogPrint (BCLog::GAME, "Game transactions @%d:\n", nHeight);
      if (haveTxKills)
        LogPrint (BCLog::GAME, "  kills:    %s\n",
                  txKills.GetHash ().ToString ());
      if (haveTxBounties)
        LogPrint (BCLog::GAME, "  bounties: %s\n",
                  txBounties.GetHash ().ToString ());
    }

  return true;
}

void
ApplyGameTransactions (const std::vector<CTransactionRef>& vGameTx,
                       const StepResult& stepResult, unsigned nHeight,
                       CCoinsViewCache& view, CBlockUndo& undo)
{
  for (const auto& gameTxRef : vGameTx)
    {
      undo.vtxundo.push_back (CTxUndo ());
      UpdateCoins (*gameTxRef, view, undo.vtxundo.back (), nHeight);
    }

  /* Update name db for killed players.  */
  const PlayerSet& victims = stepResult.GetKilledPlayers ();
  if (!victims.empty ())
    {
      assert (!vGameTx.empty ());
      const CTransaction& txKills = *vGameTx.front ();
      assert (txKills.vout.empty ());
      assert (txKills.vin.size () == victims.size ());

      for (const auto& name : victims)
        {
          const valtype& vchName = ValtypeFromString (name);
          LogPrint (BCLog::NAMES, "Killing player at height %d: %s\n",
                    nHeight, name.c_str ());

          CNameTxUndo opUndo;
          opUndo.fromOldState (vchName, view);
          undo.vnameundo.push_back (opUndo);

          CNameData data;
          data.setDead (nHeight, txKills.GetHash ());
          view.SetName (vchName, data, false);
        }
    }
}

bool
NameFromGameTransactionInput (const CScript& scriptSig, valtype& name)
{
  CScript::const_iterator pc = scriptSig.begin ();
  opcodetype opcode;
  return scriptSig.GetOp (pc, opcode, name);
}
