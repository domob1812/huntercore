#ifndef GAME_MOVE_H
#define GAME_MOVE_H

#include <amount.h>
#include <game/common.h>
#include <consensus/params.h>
#include <uint256.h>

#include <univalue.h>

#include <boost/optional.hpp>

#include <set>
#include <string>
#include <vector>

class CBlock;
class CCoinsView;
class CGameDB;
class CTransaction;
class CValidationState;
class GameState;
class StepResult;

struct Move
{
    PlayerID player;

    // New amount of locked coins (equals name output of move tx).
    CAmount newLocked;

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

    /**
     * Return the minimum required "game fee" for this move.  The params
     * and block height are used to decide about fork states.
     */
    CAmount MinimumGameFee (const Consensus::Params& param,
                            unsigned nHeight) const;

    /** Check player name.  */
    static bool IsValidPlayerName (const std::string& player);
};

class StepData
{

private:

    /* Reference to current game state (on which this builds).  */
    const GameState& state;

    /* Used to detect (and prevent) multiple moves per block of the same
       player name.  */
    std::set<PlayerID> dup;

public:

    /* Public due to the legacy code.  */
    CAmount nTreasureAmount;
    uint256 newHash;
    std::vector<Move> vMoves;

    /* Construct for the given current game state.  */
    explicit StepData (const GameState& s);

    /* Try to add a tx to the current block.  Returns true if the tx
       is either not a move at all or a valid one.  False if it is not
       valid and can not be part of a block at the moment.  This needs
       the current UTXO set to validate address permissions.  If view
       is set to NULL, this validation is turned off.  This can be
       used to just compute the game state without validating, when
       we need it for already validated blocks.  */
    bool addTransaction (const CTransaction& tx, const CCoinsView* pview,
                         CValidationState& res);

};

/* Perform a game engine step based on the given block.  Returns false if any
   error occurs and the block should be considered invalid.  */
bool PerformStep (const CBlock& block, const GameState& stateIn,
                  const CCoinsView* pview, CValidationState& valid,
                  StepResult& res, GameState& stateOut);

#endif
