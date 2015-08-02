// Copyright (C) 2015 Crypto Realities Ltd

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

#include "game/db.h"

#include "chain.h"
#include "chainparams.h"
#include "consensus/validation.h"
#include "game/move.h"
#include "game/state.h"
#include "main.h"
#include "util.h"

#include <vector>
#include <memory>

/* Define some configuration parameters.  */
/* TODO: Make them CLI options.  */
static const unsigned KEEP_EVERY_NTH = 2000;
static const unsigned MIN_IN_MEMORY = 10;
static const unsigned MAX_IN_MEMORY = 100;
static const unsigned DB_CACHE_SIZE = (25 << 20);

CGameDB::CGameDB (bool fWipe)
  : keepEveryNth(KEEP_EVERY_NTH),
    minInMemory(MIN_IN_MEMORY), maxInMemory(MAX_IN_MEMORY),
    db(GetDataDir() / "gamestates", DB_CACHE_SIZE, false, fWipe),
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

  if (!db.Read (hash, state))
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
      GameState stateIn(Params ().GetConsensus ());

      std::vector<const CBlockIndex*> needed;
      needed.push_back (mapBlockIndex[hash]);
      while (needed.back ()->pprev)
        {
          const CBlockIndex* pprev = needed.back ()->pprev;
          if (getFromCache (*pprev->phashBlock, stateIn))
            break;
          needed.push_back (pprev);
        }

      LogPrintf ("%s: integrating game state from height %d to height %d\n",
                 __func__, stateIn.nHeight, needed.front ()->nHeight);

      while (!needed.empty ())
        {
          const CBlockIndex* pindex = needed.back ();
          needed.pop_back ();
          assert (stateIn.nHeight + 1 == pindex->nHeight);

          CBlock block;
          if (!ReadBlockFromDisk (block, pindex))
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
      std::auto_ptr<GameState> s(new GameState (Params ().GetConsensus ()));
      *s = state;
      cache.insert (std::make_pair (hash, s.release ()));
    }

  if (cache.size () > maxInMemory)
    flush (false);
}

void
CGameDB::flush (bool saveAll)
{
  AssertLockHeld (cs_cache);
  LogPrintf ("Flushing game db to disk...\n");

  /* Find blocks that we want to continue to hold in memory.  These are
     main-chain blocks with recent height.  */
  std::set<uint256> keepInMemory;
  {
    LOCK (cs_main);

    const int nBestHeight = chainActive.Tip ()->nHeight;
    for (GameStateMap::const_iterator mi = cache.begin ();
         mi != cache.end (); ++mi)
      {
        const CBlockIndex* pindex = mapBlockIndex[mi->first];
        if (!chainActive.Contains (pindex))
          continue;
        assert (pindex->nHeight <= nBestHeight);
        if (nBestHeight - pindex->nHeight < static_cast<int> (minInMemory))
          keepInMemory.insert (mi->first);
      }
  }

  /* Go through everything and delete or store to disk.  */
  std::set<uint256> toErase;
  CLevelDBBatch batch(db.GetObfuscateKey());
  unsigned written = 0, discarded = 0;
  for (GameStateMap::iterator mi = cache.begin (); mi != cache.end (); ++mi)
    {
      const bool keepThis = (keepInMemory.count (mi->first) > 0);
      if (!saveAll && keepThis)
        continue;

      LOCK (cs_main);
      const CBlockIndex* pindex = mapBlockIndex[mi->first];
      if (pindex->nHeight % keepEveryNth == 0 || keepThis)
        {
          batch.Write (mi->first, *mi->second);
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
  LogPrintf ("  wrote %u game states, discarded %u\n", written, discarded);

  /* Purge unwanted elements from the database on disk.  They may have been
     stored due to the last shutdown and now be unwanted due to advancing
     the chain since then.  */
  /* TODO: Possibly not do this always.  We could do it for saveAll only,
     or with an explicit call.  Depends on how long this usually takes.  */
  discarded = 0;
  std::auto_ptr<leveldb::Iterator> pcursor(db.NewIterator ());
  for (pcursor->SeekToFirst (); pcursor->Valid (); pcursor->Next ())
    {
      const leveldb::Slice slKey = pcursor->key ();
      CDataStream ssKey(slKey.data (), slKey.data () + slKey.size (),
                        SER_DISK, CLIENT_VERSION);
      uint256 key;
      ssKey >> key;

      /* Check first if this is in our keep-in-memory list.  If it is
         and we want to "save all", keep it.  */
      const bool keepThis = (keepInMemory.count (key) > 0);
      if (saveAll && keepThis)
        continue;

      /* Otherwise, check for block height condition and delete if
         this is not a state we want to keep.  */
      LOCK (cs_main);
      const CBlockIndex* pindex = mapBlockIndex[key];
      if (pindex->nHeight % keepEveryNth != 0)
        {
          ++discarded;
          batch.Erase (key);
        }
    }
  LogPrintf ("  pruning %u game states from disk\n", discarded);

  /* Finalise by writing the database batch.  */
  const bool ok = db.WriteBatch (batch);
  if (!ok)
    error ("failed to write game db");
}
