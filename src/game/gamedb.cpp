// Copyright (c) 2015 Daniel Kraft
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "game/gamedb.h"

#include "chain.h"
#include "chainparams.h"
#include "game/gamestate.h"
#include "main.h"
#include "util.h"

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
        return true;
      }
  }

  return db.Read (hash, state);
}

bool
CGameDB::get (const uint256& hash, GameState& state)
{
  /* FIXME: Implement!  */
  assert (false);
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
        if (nBestHeight - pindex->nHeight > static_cast<int> (minInMemory))
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
  std::auto_ptr<leveldb::Iterator> pcursor(db.NewIterator ());
  pcursor->SeekToFirst ();
  discarded = 0;
  while (pcursor->Valid ())
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
