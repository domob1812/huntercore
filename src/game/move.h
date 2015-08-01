#ifndef GAME_MOVE_H
#define GAME_MOVE_H

#include "game/common.h"
#include "consensus/params.h"
#include "uint256.h"

#include <univalue.h>

#include <boost/optional.hpp>

#include <string>

class GameState;

struct Move
{
    PlayerID player;

    // New amount of locked coins (equals name output of move tx).
    int64_t newLocked;

    // Updates to the player state
    boost::optional<std::string> message;
    boost::optional<std::string> address;
    boost::optional<std::string> addressLock;

    /* For spawning moves.  */
    unsigned char color;

    std::map<int, WaypointVector> waypoints;
    std::set<int> destruct;

    Move ()
      : newLocked(-1), color(0xFF)
    {}

    std::string AddressOperationPermission(const GameState &state) const;

    bool IsSpawn() const { return color != 0xFF; }
    bool IsValid(const GameState &state) const;
    void ApplyCommon(GameState &state) const;
    void ApplySpawn(GameState &state, RandomGenerator &rnd) const;
    void ApplyWaypoints(GameState &state) const;
 
    // Move must be empty before Parse and cannot be reused after Parse
    bool Parse(const PlayerID &player, const std::string &json);

    // Returns true if move is initialized (i.e. was parsed successfully)
    //operator bool() { return !player.empty(); }

    /**
     * Return the minimum required "game fee" for this move.  The params
     * and block height are used to decide about fork states.
     */
    int64_t MinimumGameFee (const Consensus::Params& param,
                            unsigned nHeight) const;

    /** Check player name.  */
    static bool IsValidPlayerName (const std::string& player);
};

struct StepData
{
    int64_t nTreasureAmount;
    uint256 newHash;
    std::vector<Move> vMoves;
};

#endif
