// Copyright (C) 2015-2016 Crypto Realities Ltd

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

#include "chainparams.h"
#include "game/common.h"
#include "game/db.h"
#include "game/movecreator.h"
#include "game/state.h"
#include "game/tx.h"
#include "rpc/server.h"
#include "script/script.h"
#include "uint256.h"
#include "validation.h"

#include <univalue.h>

#include <boost/thread.hpp>

/* Decode an integer (could be encoded as OP_x or a bignum)
   from the script.  Returns -1 in case of error.  */
static int
GetScriptUint (const CScript& script, CScript::const_iterator& pc)
{
  opcodetype opcode;
  valtype vch;
  if (!script.GetOp (pc, opcode, vch))
    return -1;

  if (opcode >= OP_1 && opcode <= OP_16)
    return opcode - OP_1 + 1;

  /* Convert byte vector to integer.  */
  int res = 0;
  for (unsigned i = 0; i < vch.size (); ++i)
    res += (1 << (i * 8)) * vch[i];

  return res;
}

/**
 * Convert a game tx input script to a JSON representation.  This is used
 * by decoderawtransaction.
 */
UniValue
GameInputToJSON (const CScript& scriptSig)
{
  UniValue res(UniValue::VOBJ);

  CScript::const_iterator pc = scriptSig.begin ();
  opcodetype opcode;
  valtype vch;
  if (!scriptSig.GetOp (pc, opcode, vch))
    goto error;
  res.push_back (Pair ("player", ValtypeToString (vch)));

  if (!scriptSig.GetOp (pc, opcode))
    goto error;

  switch (opcode - OP_1 + 1)
    {
    case GAMEOP_KILLED_BY:
      {
        UniValue killers(UniValue::VARR);
        while (scriptSig.GetOp (pc, opcode, vch))
          killers.push_back (ValtypeToString (vch));

        if (killers.empty ())
          res.push_back (Pair ("op", "spawn_death"));
        else
          {
            res.push_back (Pair ("op", "killed_by"));
            res.push_back (Pair ("killers", killers));
          }

        break;
      }

    case GAMEOP_KILLED_POISON:
      res.push_back (Pair ("op", "poison_death"));
      break;

    case GAMEOP_COLLECTED_BOUNTY:
      res.push_back (Pair ("op", "banking"));
      res.push_back (Pair ("index", GetScriptUint (scriptSig, pc)));
      res.push_back (Pair ("first_block", GetScriptUint (scriptSig, pc)));
      res.push_back (Pair ("last_block", GetScriptUint (scriptSig, pc)));
      res.push_back (Pair ("first_collected", GetScriptUint (scriptSig, pc)));
      res.push_back (Pair ("last_collected", GetScriptUint (scriptSig, pc)));
      break;

    case GAMEOP_REFUND:
      res.push_back (Pair ("op", "refund"));
      res.push_back (Pair ("index", GetScriptUint (scriptSig, pc)));
      res.push_back (Pair ("height", GetScriptUint (scriptSig, pc)));
      break;

    default:
      goto error;
    }

  return res;

error:
  res.push_back (Pair ("error", "could not decode game tx"));
  return res;
}

/* ************************************************************************** */

UniValue
game_getplayerstate (const JSONRPCRequest& request)
{
  if (request.fHelp || request.params.size () < 1 || request.params.size () > 2)
    throw std::runtime_error (
        "game_getplayerstate \"name\" (\"hash\")\n"
        "\nLook up and return the player state for \"name\" either the latest"
        " block or the block with the given hash.\n"
        "\nArguments:\n"
        "1. \"name\"         (string, mandatory) the player name\""
        "2. \"blockhash\"    (string, optional) the block hash\n"
        "\nResult:\n"
        "JSON representation of the player state\n"
        "\nExamples:\n"
        + HelpExampleCli ("game_getplayerstate", "\"domob\"")
        + HelpExampleCli ("game_getplayerstate", "\"domob\" \"7125a396097e238e6f47662aaa3fa3b97af9125b8bcfea0dbd01aeedaae1faeb\"")
        + HelpExampleRpc ("game_getplayerstate", "\"domob\" \"7125a396097e238e6f47662aaa3fa3b97af9125b8bcfea0dbd01aeedaae1faeb\"")
      );

  uint256 hash;
  {
    LOCK (cs_main);
    if (request.params.size () >= 2)
      hash = uint256S (request.params[1].get_str ());
    else
      hash = *chainActive.Tip ()->phashBlock;

    if (mapBlockIndex.count (hash) == 0)
      throw JSONRPCError (RPC_INVALID_ADDRESS_OR_KEY, "Block not found");
  }

  GameState state(Params ().GetConsensus ());
  if (!pgameDb->get (hash, state))
    throw JSONRPCError (RPC_DATABASE_ERROR, "Failed to fetch game state");

  const PlayerID name = request.params[0].get_str ();
  PlayerStateMap::const_iterator mi = state.players.find (name);
  if (mi == state.players.end ())
    throw JSONRPCError (RPC_INVALID_ADDRESS_OR_KEY, "No such player");

  int crownIndex = -1;
  if (name == state.crownHolder.player)
    crownIndex = state.crownHolder.index;

  return mi->second.ToJsonValue (crownIndex);
}

UniValue
game_getstate (const JSONRPCRequest& request)
{
  if (request.fHelp || request.params.size () > 1)
    throw std::runtime_error (
        "game_getstate (\"hash\")\n"
        "\nLook up and return the game state for either the latest block"
        " or the block with the given hash.\n"
        "\nArguments:\n"
        "1. \"blockhash\"    (string, optional) the block hash\n"
        "\nResult:\n"
        "JSON representation of the game state\n"
        "\nExamples:\n"
        + HelpExampleCli ("game_getstate", "")
        + HelpExampleCli ("game_getstate", "\"7125a396097e238e6f47662aaa3fa3b97af9125b8bcfea0dbd01aeedaae1faeb\"")
        + HelpExampleRpc ("game_getstate", "\"7125a396097e238e6f47662aaa3fa3b97af9125b8bcfea0dbd01aeedaae1faeb\"")
      );

  uint256 hash;
  {
    LOCK (cs_main);
    if (request.params.size () >= 1)
      hash = uint256S (request.params[0].get_str ());
    else
      hash = *chainActive.Tip ()->phashBlock;

    if (mapBlockIndex.count (hash) == 0)
      throw JSONRPCError (RPC_INVALID_ADDRESS_OR_KEY, "Block not found");
  }

  GameState state(Params ().GetConsensus ());
  if (!pgameDb->get (hash, state))
    throw JSONRPCError (RPC_DATABASE_ERROR, "Failed to fetch game state");

  return state.ToJsonValue ();
}

/* ************************************************************************** */

UniValue
game_waitforchange (const JSONRPCRequest& request)
{
  if (request.fHelp || request.params.size () > 1)
    throw std::runtime_error (
        "game_waitforchange (\"hash\")\n"
        "\nDo not use this call in new applications.  Instead, -blocknotify\n"
        "or the ZeroMQ system should be used.\n"
      );

  uint256 hash;
  {
    LOCK (cs_main);
    if (request.params.size () >= 1)
      hash = uint256S (request.params[0].get_str ());
    else
      hash = *chainActive.Tip ()->phashBlock;

    if (mapBlockIndex.count (hash) == 0)
      throw JSONRPCError (RPC_INVALID_ADDRESS_OR_KEY, "Block not found");
  }

  boost::unique_lock<boost::mutex> lock(mut_currentState);
  while (IsRPCRunning())
    {
      /* Atomically check whether we have found a new best block and return
         it if that's the case.  We use a lock on cs_main in order to
         prevent race conditions.  */
      {
        LOCK (cs_main);
        const uint256 bestHash = *chainActive.Tip ()->phashBlock;
        if (hash != bestHash)
          {
            GameState state(Params ().GetConsensus ());
            if (!pgameDb->get (bestHash, state))
              throw JSONRPCError (RPC_DATABASE_ERROR,
                                  "Failed to fetch game state");

            return state.ToJsonValue ();
          }
      }

      /* Wait on the condition variable.  */
      cv_stateChange.wait (lock);
    }

  return UniValue();
}

/* ************************************************************************** */

UniValue
game_getpath (const JSONRPCRequest& request)
{
  if (request.fHelp || request.params.size () != 2)
    throw std::runtime_error (
        "game_getpath [fromX,fromY] [toX,toY]\n"
        "\nReturn a set of way points that travels in a shortest path"
        " between the given coordinates.\n"
        "\nArguments:\n"
        "1. \"from\"    (int array, required) starting coordinate\n"
        "1. \"to\"      (int array, required) target coordinate\n"
        "\nResult:\n"
        "[              (json array of integers)\n"
        "   x1, y1,\n"
        "   x2, y2,\n"
        "   ...\n"
        "]\n"
        "\nExamples:\n"
        + HelpExampleCli ("game_getpath", "[0,0] [100,100]")
        + HelpExampleCli ("game_getpath", "[0,0] [100,100]")
        + HelpExampleRpc ("game_getpath", "[0,0] [100,100]")
      );

  if (!request.params[0].isArray () || !request.params[1].isArray ())
    throw std::runtime_error ("arguments must be arrays");
  if (request.params[0].size () != 2 || request.params[1].size () != 2)
    throw std::runtime_error ("invalid coordinates given");

  const Coord fromC(request.params[0][0].get_int (),
                    request.params[0][1].get_int ());
  const Coord toC(request.params[1][0].get_int (),
                  request.params[1][1].get_int ());

  const std::vector<Coord> path = FindPath (fromC, toC);

  UniValue res(UniValue::VARR);
  bool first = true;
  BOOST_FOREACH(const Coord& c, path)
    {
      if (first)
        {
          first = false;
          continue;
        }

      res.push_back (c.x);
      res.push_back (c.y);
    }

  return res;
}

/* ************************************************************************** */

static const CRPCCommand commands[] =
{ //  category              name                      actor (function)         okSafeMode
  //  --------------------- ------------------------  -----------------------  ----------
    { "game",               "game_getplayerstate",    &game_getplayerstate,    true },
    { "game",               "game_getstate",          &game_getstate,          true },
    { "game",               "game_getpath",           &game_getpath,           true },
    { "game",               "game_waitforchange",     &game_waitforchange,     true },
};

void RegisterGameRPCCommands(CRPCTable &tableRPC)
{
    for (unsigned int vcidx = 0; vcidx < ARRAYLEN(commands); vcidx++)
        tableRPC.appendCommand(commands[vcidx].name, &commands[vcidx]);
}
