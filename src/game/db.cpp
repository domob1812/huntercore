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

#include <game/db.h>

#include <chain.h>
#include <chainparams.h>
#include <consensus/validation.h>
#include <game/move.h>
#include <game/state.h>
#include <util.h>
#include <validation.h>

#include <vector>
#include <memory>

#include <boost/thread.hpp>

/* Define prefix for database keys.  We only index by block hash, but still
   need them so we can tell game states apart from the obfuscation key that
   is also in the database.  */
static const char DB_GAMESTATE = 'g';

/* Define some configuration parameters.  */
/* TODO: Make them CLI options.  */
static const unsigned KEEP_EVERY_NTH = 2000;
static const unsigned MIN_IN_MEMORY = 10;
static const unsigned MAX_IN_MEMORY = 100;
static const unsigned DB_CACHE_SIZE = (25 << 20);

CGameDB::CGameDB (bool fMemory, bool fWipe)
  : keepEveryNth(KEEP_EVERY_NTH),
    minInMemory(MIN_IN_MEMORY), maxInMemory(MAX_IN_MEMORY),
    keepEverything(false),
    db(GetDataDir() / "gamestates", DB_CACHE_SIZE, fMemory, fWipe, true),
    cache(), cs_cache()
{
  // Nothing else to do.
}

CGameDB::~CGameDB ()
{
  LOCK (cs_cache);
  flush (true);
  assert (cache.empty ());
}

bool
CGameDB::getFromCache (const uint256& hash, GameState& state) const
{
  {
    LOCK (cs_cache);
    const GameStateMap::const_iterator mi = cache.find (hash);
    if (mi != cache.end ())
      {
        state = *mi->second;
        assert (hash == state.hashBlock);
        return true;
      }
  }

  if (!db.Read (std::make_pair (DB_GAMESTATE, hash), state))
    return false;

  assert (hash == state.hashBlock);
  return true;
}

bool
CGameDB::get (const uint256& hash, GameState& state)
{
  if (!getFromCache (hash, state))
    {
      /* Look up the latest previous block for which the game
         state is known in the cache somewhere.  If it goes back
         to the genesis block, use a default-constructed game state
         instead as the input.  It corresponds to the block "before"
         the genesis block.

         We keep all CBlockIndex pointers in a vector, so that
         we can then go back up the chain without relying on chainActive.  */

      LOCK (cs_main);
      const CChainParams& chainparams = Params ();
      GameState stateIn(chainparams.GetConsensus ());

      std::vector<const CBlockIndex*> needed;
      const BlockMap::const_iterator mi = mapBlockIndex.find (hash);
      if (mi == mapBlockIndex.end ())
        return error ("%s: block hash not found", __func__);
      needed.push_back (mi->second);
      while (needed.back ()->pprev)
        {
          const CBlockIndex* pprev = needed.back ()->pprev;
          if (getFromCache (*pprev->phashBlock, stateIn))
            break;
          needed.push_back (pprev);
        }

      LogPrint (BCLog::GAME,
                "Integrating game state from height %d to height %d.\n",
                stateIn.nHeight, needed.front ()->nHeight);

      while (!needed.empty ())
        {
          const CBlockIndex* pindex = needed.back ();
          needed.pop_back ();
          assert (stateIn.nHeight + 1 == pindex->nHeight);

          CBlock block;
          if (!ReadBlockFromDisk (block, pindex, chainparams.GetConsensus ()))
            return error ("%s: failed to read block from disk", __func__);

          CValidationState valid;
          StepResult res;
          if (!PerformStep (block, stateIn, NULL, valid, res, state))
            return error ("%s: failed to perform game step", __func__);

          assert (state.hashBlock == *pindex->phashBlock);
          stateIn = state;
        }

      store (hash, state);
    }

  assert (hash == state.hashBlock);
  return true;
}

void
CGameDB::store (const uint256& hash, const GameState& state)
{
  assert (hash == state.hashBlock);
  LOCK (cs_cache);

  const GameStateMap::iterator mi = cache.find (hash);
  if (mi != cache.end ())
    {
      delete mi->second;
      mi->second = new GameState (Params ().GetConsensus ());
      *mi->second = state;
    }
  else
    {
      std::unique_ptr<GameState> s(new GameState (Params ().GetConsensus ()));
      *s = state;
      cache.insert (std::make_pair (hash, s.release ()));
    }

  attemptFlush ();
}

void
CGameDB::flush (bool saveAll)
{
  AssertLockHeld (cs_cache);
  LogPrint (BCLog::GAME, "Flushing game db to disk...\n");

  /* Find blocks that we want to continue to hold in memory.  These are
     main-chain blocks with recent height.  */
  std::set<uint256> keepInMemory;
  {
    LOCK (cs_main);

    const CBlockIndex* pindex = chainActive.Tip ();
    assert (pindex);
    const int minHeight = pindex->nHeight - minInMemory;
    for (; pindex && pindex->nHeight > minHeight; pindex = pindex->pprev)
      keepInMemory.insert (*pindex->phashBlock);
  }

  /* Go through everything and delete or store to disk.  */
  std::set<uint256> toErase;
  CDBBatch batch(db);
  unsigned written = 0, discarded = 0;
  for (GameStateMap::iterator mi = cache.begin (); mi != cache.end (); ++mi)
    {
      bool keepThis = (keepInMemory.count (mi->first) > 0);
      if (!saveAll && keepThis)
        continue;

      LOCK (cs_main);
      bool write = keepThis;

      /* It can happen that cache contains blocks that are not in mapBlockIndex.
         This is the case if they were added to the cache through ConnectBlock
         called from TestBlockValidity and mining (or testing).  If this is
         not the case and the block is part of mapBlockIndex, we can look
         at the block's height and keep it if the height is divisible
         by KEEP_EVERY_NTH.  */
      const BlockMap::const_iterator bmi = mapBlockIndex.find (mi->first);
      if (!write && bmi != mapBlockIndex.end ())
        {
          const CBlockIndex* pindex = bmi->second;
          assert (pindex);
          write = (pindex->nHeight % keepEveryNth == 0);
        }

      if (write)
        {
          batch.Write (std::make_pair (DB_GAMESTATE, mi->first), *mi->second);
          ++written;
        }
      else
        ++discarded;

      delete mi->second;
      toErase.insert (mi->first);
    }
  for (std::set<uint256>::const_iterator i = toErase.begin ();
       i != toErase.end (); ++i)
    cache.erase (*i);
  assert (!saveAll || cache.empty ());
  LogPrint (BCLog::GAME, "  wrote %u game states, discarded %u\n",
            written, discarded);

  /* Purge unwanted elements from the database on disk.  They may have been
     stored due to the last shutdown and now be unwanted due to advancing
     the chain since then.  */
  /* TODO: Possibly not do this always.  We could do it for saveAll only,
     or with an explicit call.  Depends on how long this usually takes.  */
  discarded = 0;
  std::unique_ptr<CDBIterator> pcursor(db.NewIterator ());
  for (pcursor->Seek (DB_GAMESTATE); pcursor->Valid (); pcursor->Next ())
    {
      boost::this_thread::interruption_point();
      char chType;
      if (!pcursor->GetKey(chType) || chType != DB_GAMESTATE)
        break;

      std::pair<char, uint256> key;
      if (!pcursor->GetKey (key) || key.first != DB_GAMESTATE)
        {
          error ("%s: failed to read game state key", __func__);
          break;
        }

      /* Check first if this is in our keep-in-memory list.  If it is
         and we want to "save all", keep it.  */
      const bool keepThis = (keepInMemory.count (key.second) > 0);
      if (saveAll && keepThis)
        continue;

      /* Otherwise, check for block height condition and delete if
         this is not a state we want to keep.  */
      LOCK (cs_main);
      const BlockMap::const_iterator bmi = mapBlockIndex.find (key.second);
      assert (bmi != mapBlockIndex.end ());
      const CBlockIndex* pindex = bmi->second;
      assert (pindex);
      if (pindex->nHeight % keepEveryNth != 0)
        {
          ++discarded;
          batch.Erase (key);
        }
    }
  LogPrint (BCLog::GAME, "  pruning %u game states from disk\n", discarded);

  /* Finalise by writing the database batch.  */
  const bool ok = db.WriteBatch (batch);
  if (!ok)
    error ("failed to write game db");
}
