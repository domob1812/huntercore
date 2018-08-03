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

#ifndef GAME_STATE_H
#define GAME_STATE_H

#include <amount.h>
#include <consensus/params.h>
#include <game/common.h>
#include <uint256.h>
#include <serialize.h>

#include <univalue.h>

#include <cmath>
#include <map>
#include <string>

class GameState;
class Move;
class StepData;
class StepResult;

/* Return the minimum necessary amount of locked coins.  This influences
   both the minimum move game fees (for spawning a new player) and
   also the damage / HP calculation for life-steal.  */
CAmount GetNameCoinAmount (const Consensus::Params& param, unsigned nHeight);

/**
 * A character on the map that stores information while processing attacks.
 * Keep track of all attackers, so that we can both construct the killing gametx
 * and also handle life-stealing.
 */
struct AttackableCharacter
{

  /** The character this represents.  */
  CharacterID chid;

  /** The character's colour.  */
  unsigned char color;

  /**
   * Amount of coins already drawn from the attacked character's life.
   * This is the value that can be redistributed to the attackers.
   */
  CAmount drawnLife;

  /** All attackers that hit it.  */
  std::set<CharacterID> attackers;

  /**
   * Perform an attack by the given character.  Its ID and state must
   * correspond to the same attacker.
   */
  void AttackBy (const CharacterID& attackChid, const PlayerState& pl);

  /**
   * Handle self-effect of destruct.  The game state's height is used
   * to determine whether or not this has an effect (before the life-steal
   * fork).
   */
  void AttackSelf (const GameState& state);

};

/**
 * Hold the map from tiles to attackable characters.  This is built lazily
 * when attacks are done, so that we can save the processing time if not.
 */
struct CharactersOnTiles
{

  /** The map type used.  */
  typedef std::multimap<Coord, AttackableCharacter> Map;

  /** The actual map.  */
  Map tiles;

  /** Whether it is already built.  */
  bool built;

  /**
   * Construct an empty object.
   */
  inline CharactersOnTiles ()
    : tiles(), built(false)
  {}

  /**
   * Build it from the game state if not yet built.
   * @param state The game state from which to extract characters.
   */
  void EnsureIsBuilt (const GameState& state);

  /**
   * Perform all attacks in the moves.
   * @param state The current game state to build it if necessary.
   * @param moves All moves in the step.
   */
  void ApplyAttacks (const GameState& state, const std::vector<Move>& moves);

  /**
   * Deduct life from attached characters.  This also handles killing
   * of those with too many attackers, including pre-life-steal.
   * @param state The game state, will be modified.
   * @param result The step result object to fill in.
   */
  void DrawLife (GameState& state, StepResult& result);

  /**
   * Remove mutual attacks from the attacker arrays.
   * @param state The state to look up players.
   */
  void DefendMutualAttacks (const GameState& state);

  /**
   * Give drawn life back to attackers.  If there are more attackers than
   * available coins, distribute randomly.
   * @param rnd The RNG to use.
   * @param state The state to update.
   */
  void DistributeDrawnLife (RandomGenerator& rnd, GameState& state) const;

};

// Do not use for user-provided coordinates, as abs can overflow on INT_MIN.
// Use for algorithmically-computed coordinates that guaranteedly lie within the game map.
inline unsigned distLInf(const Coord &c1, const Coord &c2)
{
    return std::max(std::abs(c1.x - c2.x), std::abs(c1.y - c2.y));
}

struct LootInfo
{
    CAmount nAmount;
    // Time span over the which this loot accumulated
    // This is merely for informative purposes, plus to make
    // hash of the loot tx unique
    int firstBlock, lastBlock;

    LootInfo() : nAmount(0), firstBlock(-1), lastBlock(-1) { }
    LootInfo(CAmount nAmount_, int nHeight)
      : nAmount(nAmount_), firstBlock(nHeight), lastBlock(nHeight)
    {}

    ADD_SERIALIZE_METHODS;

    template<typename Stream, typename Operation>
      inline void SerializationOp (Stream& s, Operation ser_action)
    {
      READWRITE (nAmount);
      READWRITE (firstBlock);
      READWRITE (lastBlock);
    }
};

struct CollectedLootInfo : public LootInfo
{
    /* Time span over which the loot was collected.  If this is a
       player refund bounty, collectedFirstBlock = -1 and collectedLastBlock
       is set to the refunding block height.  */
    int collectedFirstBlock, collectedLastBlock;
    
    CollectedLootInfo() : LootInfo(), collectedFirstBlock(-1), collectedLastBlock(-1) { }

    ADD_SERIALIZE_METHODS;

    template<typename Stream, typename Operation>
      inline void SerializationOp (Stream& s, Operation ser_action)
    {
      READWRITE (*static_cast<LootInfo*> (this));
      READWRITE (collectedFirstBlock);
      READWRITE (collectedLastBlock);
      assert (!IsRefund ());
    }

    void Collect(const LootInfo &loot, int nHeight)
    {
        assert (!IsRefund ());

        if (loot.nAmount <= 0)
            return;

        nAmount += loot.nAmount;

        if (firstBlock < 0 || loot.firstBlock < firstBlock)
            firstBlock = loot.firstBlock;
        if (loot.lastBlock > lastBlock)
            lastBlock = loot.lastBlock;

        if (collectedFirstBlock < 0)
            collectedFirstBlock = nHeight;
        collectedLastBlock = nHeight;
    }

    /* Set the loot info to a state that means "this is a player refunding tx".
       They are used to give back coins if a player is killed for staying in
       the spawn area, and encoded differently in the game transactions.
       The block height is present to make the resulting tx unique.  */
    inline void
    SetRefund (CAmount refundAmount, int nHeight)
    {
      assert (nAmount == 0);
      assert (collectedFirstBlock == -1 && collectedLastBlock == -1);
      nAmount = refundAmount;
      collectedLastBlock = nHeight;
    }

    /* Check if this is a player refund tx.  */
    inline bool
    IsRefund () const
    {
      return (nAmount > 0 && collectedFirstBlock == -1);
    }

    /* When this is a refund, return the refund block height.  */
    inline int
    GetRefundHeight () const
    {
      assert (IsRefund ());
      return collectedLastBlock;
    }
};

// for FORK_TIMESAVE
constexpr int CHARACTER_MODE_NORMAL = 6;
// difference of 2 means we can walk over (and along) the player spawn strip without logout
constexpr int CHARACTER_MODE_LOGOUT = 8;
constexpr int CHARACTER_MODE_SPECTATOR_BEGIN = 9;
inline bool CharacterIsProtected(int s)
{
    return ((s < CHARACTER_MODE_NORMAL) || (s > CHARACTER_MODE_LOGOUT));
}
inline bool CharacterSpawnProtectionAlmostFinished(int s)
{
    return (s == CHARACTER_MODE_NORMAL - 1);
}
inline bool CharacterInSpectatorMode(int s)
{
    return (s > CHARACTER_MODE_LOGOUT);
}
inline bool CharacterNoLogout(int s)
{
    return ((s != CHARACTER_MODE_LOGOUT) && (s < CHARACTER_MODE_SPECTATOR_BEGIN + 15));
}

struct CharacterState
{
    Coord coord;                        // Current coordinate
    unsigned char dir;                  // Direction of last move (for nice sprite orientation). Encoding: as on numeric keypad.
    Coord from;                         // Straight-line pathfinding for current waypoint
    WaypointVector waypoints;           // Waypoints (stored in reverse so removal of the first waypoint is fast)
    CollectedLootInfo loot;             // Loot collected by player but not banked yet
    unsigned char stay_in_spawn_area;   // Auto-kill players who stay in the spawn area too long

    CharacterState ()
      : coord(0, 0), dir(0), from(0, 0),
        stay_in_spawn_area(0)
    {}

    ADD_SERIALIZE_METHODS;

    template<typename Stream, typename Operation>
      inline void SerializationOp (Stream& s, Operation ser_action)
    {
      READWRITE (coord);
      READWRITE (dir);
      READWRITE (from);
      READWRITE (waypoints);
      READWRITE (loot);
      READWRITE (stay_in_spawn_area);
    }

    void Spawn(const GameState& state, int color, RandomGenerator &rnd);

    void StopMoving()
    {
        from = coord;
        waypoints.clear();
    }

    void MoveTowardsWaypoint();
    WaypointVector DumpPath(const WaypointVector *alternative_waypoints = NULL) const;

    /**
     * Calculate total length (in the same L-infinity sense that gives the
     * actual movement time) of the outstanding path.
     * @param altWP Optionally provide alternative waypoints (for queued moves).
     * @return Time necessary to finish current path in blocks.
     */
    unsigned TimeToDestination(const WaypointVector *altWP = NULL) const;

    /* Collect loot by this character.  This takes the carrying capacity
       into account and only collects until this limit is reached.  All
       loot amount that *remains* will be returned.  */
    CAmount CollectLoot (LootInfo newLoot, int nHeight, CAmount carryCap);

    UniValue ToJsonValue(bool has_crown) const;
};

struct PlayerState
{
    /* Colour represents player team.  */
    unsigned char color;

    /* Value locked in the general's name on the blockchain.  This is the
       initial cost plus all "game fees" paid in the mean time.  It is compared
       to the new output value given by a move tx in order to compute
       the game fee as difference.  In that sense, it is a "cache" for
       the prevout.  */
    CAmount lockedCoins;
    /* Actual value of the general in the game state.  */
    CAmount value;

    std::map<int, CharacterState> characters;   // Characters owned by the player (0 is the main character)
    int next_character_index;                   // Index of the next spawned character

    /* Number of blocks the player still lives if poisoned.  If it is 1,
       the player will be killed during the next game step.  -1 means
       that there is no poisoning yet.  It should never be 0.  */
    int remainingLife;

    std::string message;      // Last message, can be shown as speech bubble
    int message_block;        // Block number. Game visualizer can hide messages that are too old
    std::string address;      // Address for receiving rewards. Empty means receive to the name address
    std::string addressLock;  // "Admin" address for player - reward address field can only be changed, if player is transferred to addressLock

    ADD_SERIALIZE_METHODS;

    template<typename Stream, typename Operation>
      inline void SerializationOp (Stream& s, Operation ser_action)
    {
      READWRITE (color);
      READWRITE (characters);
      READWRITE (next_character_index);
      READWRITE (remainingLife);

      READWRITE (message);
      READWRITE (message_block);
      READWRITE (address);
      READWRITE (addressLock);

      READWRITE (lockedCoins);
      READWRITE (value);
    }

    PlayerState ()
      : color(0xFF), lockedCoins(0), value(-1),
        next_character_index(0), remainingLife(-1), message_block(0)
    {}

    void SpawnCharacter(const GameState& state, RandomGenerator &rnd);
    bool CanSpawnCharacter() const;
    UniValue ToJsonValue(int crown_index, bool dead = false) const;
};

struct GameState
{
    GameState(const Consensus::Params& param);

    // Reference consensus parameters in effect.
    const Consensus::Params* param;

    // Player states
    PlayerStateMap players;

    // Last chat messages of dead players (only in the current block)
    // Minimum info is stored: color, message, message_block.
    // When converting to JSON, this array is concatenated with normal players.
    std::map<PlayerID, PlayerState> dead_players_chat;

    std::map<Coord, LootInfo> loot;
    std::set<Coord> hearts;

    /* Store banks together with their remaining life time.  */
    std::map<Coord, unsigned> banks;

    Coord crownPos;
    CharacterID crownHolder;

    /* Amount of coins in the "game fund" pool.  */
    CAmount gameFund;

    // Number of steps since the game start.
    // State with nHeight==i includes moves from i-th block
    // -1 = initial game state (before genesis block)
    // 0  = game state immediately after the genesis block
    int nHeight;

    /* Block height (as per nHeight) of the last state that had a disaster.
       I. e., for a game state where disaster has just happened,
       nHeight == nDisasterHeight.  It is -1 before the first disaster
       happens.  */
    int nDisasterHeight;

    // Hash of the last block, moves from which were included
    // into this game state. This is meta-information (i.e. used
    // mainly for managing game states rather than as part of game
    // state, though it can be used as a random seed)
    /* TODO: Can we get rid of this?  */
    uint256 hashBlock;

    ADD_SERIALIZE_METHODS;

    template<typename Stream, typename Operation>
      inline void SerializationOp (Stream& s, Operation ser_action)
    {
      READWRITE (players);
      READWRITE (dead_players_chat);
      READWRITE (loot);
      READWRITE (hearts);
      READWRITE (banks);
      READWRITE (crownPos);
      READWRITE (crownHolder.player);
      if (!crownHolder.player.empty ())
        READWRITE (crownHolder.index);
      READWRITE (gameFund);

      READWRITE (nHeight);
      READWRITE (nDisasterHeight);
      READWRITE (hashBlock);
    }
    
    UniValue ToJsonValue() const;

    inline bool
    ForkInEffect (Fork type) const
    {
      return param->rules->ForkInEffect (type, nHeight);
    }

    inline bool
    TestingRules () const
    {
      return param->rules->TestingRules ();
    }

    // Helper functions
    void AddLoot(Coord coord, CAmount nAmount);
    void DivideLootAmongPlayers();
    void CollectHearts(RandomGenerator &rnd);
    void UpdateCrownState(bool &respawn_crown);
    void CollectCrown(RandomGenerator &rnd, bool respawn_crown);
    void CrownBonus(CAmount nAmount);

    /**
     * Get the number of initial characters for players created in this
     * game state.  This was initially 3, and is changed in a hardfork
     * depending on the block height.
     * @return Number of initial characters to create (including general).
     */
    unsigned GetNumInitialCharacters () const;

    /**
     * Check if a given location is a banking spot.
     * @param c The coordinate to check.
     * @return True iff it is a banking spot.
     */
    bool IsBank (const Coord& c) const;

    /* Handle loot of a killed character.  Depending on the circumstances,
       it may be dropped (with or without miner tax), refunded in a bounty
       transaction or added to the game fund.  */
    void HandleKilledLoot (const PlayerID& pId, int chInd,
                           const KilledByInfo& info, StepResult& step);

    /* For a given list of killed players, kill all their characters
       and collect the tax amount.  The killed players are removed from
       the state's list of players.  */
    void FinaliseKills (StepResult& step);

    /* Check if a disaster should happen at the current state given
       the random numbers.  */
    bool CheckForDisaster (RandomGenerator& rng) const;

    /* Perform spawn deaths.  */
    void KillSpawnArea (StepResult& step);

    /* Apply poison disaster to the state.  */
    void ApplyDisaster (RandomGenerator& rng);
    /* Decrement poison life expectation and kill players whose has
       dropped to zero.  */
    void DecrementLife (StepResult& step);

    /* Special action at the life-steal fork height:  Remove all hearts
       on the map and kill all hearted players.  */
    void RemoveHeartedCharacters (StepResult& step);

    /* Update the banks randomly (eventually).  */
    void UpdateBanks (RandomGenerator& rng);

    /* Return total amount of coins on the map (in loot and hold by players,
       including also general values).  */
    CAmount GetCoinsOnMap () const;

};

/* Encode data for a banked bounty.  This includes also the payment address
   as per the player state (may be empty if no explicit address is set), so
   that the reward-paying game tx can be constructed even if the player
   is no longer alive (e. g., killed by a disaster).  */
struct CollectedBounty
{

  CharacterID character;
  CollectedLootInfo loot;
  std::string address;

  inline CollectedBounty (const PlayerID& p, int cInd,
                          const CollectedLootInfo& l,
                          const std::string& addr)
    : character(p, cInd), loot(l), address(addr)
  {}

  /* Look up the player in the given game state and if it is still
     there, update the address from the game state.  */
  void UpdateAddress (const GameState& state);

};

/* Encode data about why or by whom a player was killed.  Possibilities
   are a player (also self-destruct), staying too long in spawn area and
   due to poisoning after a disaster.  The information is used to
   construct the game transactions.  */
struct KilledByInfo
{

  /* Actual reason for death.  Since this is also used for ordering of
     the killed-by infos, the order here is crucial and determines
     how the killed-by info will be represented in the constructed game tx.  */
  enum Reason
  {
    KILLED_DESTRUCT = 1, /* Killed by destruct / some player.  */
    KILLED_SPAWN,        /* Staying too long in spawn area.  */
    KILLED_POISON        /* Killed by poisoning.  */
  } reason;

  /* The killing character, if killed by destruct.  */
  CharacterID killer;

  inline KilledByInfo (Reason why)
    : reason(why)
  {
    assert (why != KILLED_DESTRUCT);
  }

  inline KilledByInfo (const CharacterID& ch)
    : reason(KILLED_DESTRUCT), killer(ch)
  {}

  /* See if this killing reason pays out miner tax or not.  */
  bool HasDeathTax () const;

  /* See if this killing should drop the coins.  Otherwise (e. g., for poison)
     the coins are added to the game fund.  */
  bool DropCoins (const GameState& state, const PlayerState& victim) const;

  /* See if this killing allows a refund of the general cost to the player.
     This depends on the height, since poison death refunds only after
     the life-steal fork.  */
  bool CanRefund (const GameState& state, const PlayerState& victim) const;

  /* Comparison necessary for STL containers.  */

  friend inline bool
  operator== (const KilledByInfo& a, const KilledByInfo& b)
  {
    if (a.reason != b.reason)
      return false;

    switch (a.reason)
      {
      case KILLED_DESTRUCT:
        return a.killer == b.killer;
      default:
        return true;
      }
  }

  friend inline bool
  operator< (const KilledByInfo& a, const KilledByInfo& b)
  {
    if (a.reason != b.reason)
      return (a.reason < b.reason);

    switch (a.reason)
      {
      case KILLED_DESTRUCT:
        return a.killer < b.killer;
      default:
        return false;
      }
  }

};

class StepResult
{

private:

    // The following arrays only contain killed players
    // (i.e. the main character)
    PlayerSet killedPlayers;
    KilledByMap killedBy;

public:

    std::vector<CollectedBounty> bounties;

    CAmount nTaxAmount;

    StepResult() : nTaxAmount(0) { }

    /* Insert information about a killed player.  */
    inline void
    KillPlayer (const PlayerID& victim, const KilledByInfo& killer)
    {
      killedBy.insert (std::make_pair (victim, killer));
      killedPlayers.insert (victim);
    }

    /* Read-only access to the killed player maps.  */

    inline const PlayerSet&
    GetKilledPlayers () const
    {
      return killedPlayers;
    }

    inline const KilledByMap&
    GetKilledBy () const
    {
      return killedBy;
    }

};

// All moves happen simultaneously, so this function must work identically
// for any ordering of the moves, except non-critical cases (e.g. finding
// an empty cell to spawn new player)
bool PerformStep(const GameState &inState, const StepData &stepData, GameState &outState, StepResult &stepResult);

#endif
