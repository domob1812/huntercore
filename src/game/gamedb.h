// Copyright (c) 2015 Daniel Kraft
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_GAME_GAMEDB
#define BITCOIN_GAME_GAMEDB

#include "leveldbwrapper.h"
#include "sync.h"
#include "uint256.h"

#include <map>

class GameState;

/**
 * Database for caching game states.  Note that each block hash corresponds
 * uniquely to a game state.  Game states can never change, they are only
 * ever read again for a block hash or new ones (corresponding to different
 * block hashes) created.  This database is in fundamental contrast to the
 * UTXO database, which is modified while connecting/disconnecting blocks.
 * Thus it is in its own class and directory, not using the chainstate.
 *
 * The database (on disk) stores the states to every Nth block.  Intermediate
 * steps can be recomputed, but that is costly.  The last few states are kept
 * in memory, so that reorgs can be done efficiently.
 */
class CGameDB
{

public:

    explicit CGameDB (bool fWipe);
    ~CGameDB ();

    /**
     * Query for a game state by corresponding block hash.  The block
     * must be present in mapBlockIndex already.  If the game state is not
     * directly available, it is recomputed as necessary.
     * @param hash The block hash to look up.
     * @param state Put the game state here.
     * @return True iff successful.
     */
    bool get (const uint256& hash, GameState& state);

    /**
     * Store a game state.  This is in principle not necessary, since get()
     * itself also stores the game state after computing it.  We use it,
     * nevertheless, when connecting blocks.  This avoids a duplicate
     * computation.
     */
    void store (const uint256& hash, const GameState& state);

private:

    /** Keep every Nth game state permanently on disk.  */
    unsigned keepEveryNth;
    /** Minimum number of states to keep in memory (the last ones).  */
    unsigned minInMemory;
    /**
     * Maximum number of states to keep in memory.  If this is reached,
     * the cache will be flushed back to disk.
     */
    unsigned maxInMemory;

    /** The backing LevelDB.  */
    CLevelDBWrapper db;

    typedef std::map<uint256, GameState*> GameStateMap;
    /** In-memory store of the last few block states.  */
    GameStateMap cache;
    /** Lock to protect the cache datastructure.  */
    mutable CCriticalSection cs_cache;

    /**
     * Get without recomputation.  Returns false if the state is not
     * readily available.
     */
    bool getFromCache (const uint256& hash, GameState& state) const;

    /**
     * Flush the in-memory cache to disk.  The minimum in-memory blocks
     * are kept in memory, and the others are written to disk or discarded
     * (depending on the keep-every-nth policy).  This also goes through
     * the on-disk states and removes ones that do not fit the policy.
     * @param saveAll Store all in-memory cache to disk.  This is done
     *                when shutting down the node.
     */
    void flush (bool saveAll);

};

#endif // BITCOIN_GAME_GAMEDB
