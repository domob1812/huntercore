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

#include <game/state.h>

#include <core_io.h>
#include <game/map.h>
#include <game/move.h>
#include <rpc/server.h>
#include <util.h>
#include <utilstrencodings.h>

#include <functional>

namespace
{

constexpr int MAX_CHARACTERS_PER_PLAYER = 20;           // Maximum number of characters per player at the same time
constexpr int MAX_CHARACTERS_PER_PLAYER_TOTAL = 1000;   // Maximum number of characters per player in the lifetime

/* Parameters that determine when a poison-disaster will happen.  The
   probability is 1/x at each block between min and max time.  */
constexpr unsigned PDISASTER_MIN_TIME = 1440;
constexpr unsigned PDISASTER_MAX_TIME = 12 * 1440;
constexpr unsigned PDISASTER_PROBABILITY = 10000;

/* Parameters about how long a poisoned player may still live.  */
constexpr unsigned POISON_MIN_LIFE = 1;
constexpr unsigned POISON_MAX_LIFE = 50;

/* Parameters for dynamic banks after the life-steal fork.  */
constexpr unsigned DYNBANKS_NUM_BANKS = 75;
constexpr unsigned DYNBANKS_MIN_LIFE = 25;
constexpr unsigned DYNBANKS_MAX_LIFE = 100;

bool
IsOriginalSpawnAreaCoord (const Coord &c)
{
    return IsOriginalSpawnArea(c.x, c.y);
}

bool
IsWalkableCoord (const Coord &c)
{
    return IsWalkable(c.x, c.y);
}

/**
 * Keep a set of walkable tiles.  This is used for random selection of
 * one of them for spawning / dynamic bank purposes.  Note that it is
 * important how they are ordered (according to Coord::operator<) in order
 * to reach consensus on the game state.
 *
 * This is filled in from IsWalkable() whenever it is empty (on startup).  It
 * does not ever change.
 */
std::vector<Coord> walkableTiles;
// for FORK_TIMESAVE -- 2 more sets of walkable tiles
std::vector<Coord> walkableTiles_ts_players;
std::vector<Coord> walkableTiles_ts_banks;

/* Calculate carrying capacity.  This is where it is basically defined.
   It depends on the block height (taking forks changing it into account)
   and possibly properties of the player.  Returns -1 if the capacity
   is unlimited.  */
CAmount
GetCarryingCapacity (const GameState& state, bool isGeneral, bool isCrownHolder)
{
  if (!state.ForkInEffect (FORK_CARRYINGCAP) || isCrownHolder)
    return -1;

  if (state.ForkInEffect (FORK_LIFESTEAL))
    return 100 * COIN;

  if (state.ForkInEffect (FORK_LESSHEARTS))
    return 2000 * COIN;

  return (isGeneral ? 50 : 25) * COIN;
}

/* Get the destruct radius a hunter has at a certain block height.  This
   may depend on whether or not it is a general.  */
int
GetDestructRadius (const GameState& state, bool isGeneral)
{
  if (state.ForkInEffect (FORK_LESSHEARTS))
    return 1;

  return isGeneral ? 2 : 1;
}

/* Get maximum allowed stay on a bank.  */
int
MaxStayOnBank (const GameState& state)
{
  if (state.ForkInEffect (FORK_LIFESTEAL))
    return 2;

  /* Between those two forks, spawn death was disabled.  */
  if (state.ForkInEffect (FORK_CARRYINGCAP)
        && !state.ForkInEffect (FORK_LESSHEARTS))
    return -1;

  /* Return original value.  */
  return 30;
}

/* Check whether or not a heart should be dropped at the current height.  */
bool
DropHeart (const GameState& state)
{
  if (state.ForkInEffect (FORK_LIFESTEAL))
    return false;

  const int heartEvery = (state.ForkInEffect (FORK_LESSHEARTS) ? 500 : 10);
  return state.nHeight % heartEvery == 0;
}

/* Fills in a walkableTiles array, using the passed predicate in addition
   to the general IsWalkable() function to decide which coordinates should
   be put into the list.  */
void
FillWalkableArray (std::vector<Coord>& tiles,
                   const std::function<bool(int, int)>& predicate)
{
  if (tiles.empty ())
    {
      for (int x = 0; x < MAP_WIDTH; ++x)
        for (int y = 0; y < MAP_HEIGHT; ++y)
          if (IsWalkable (x, y) && predicate (x, y))
            tiles.push_back (Coord (x, y));

      /* Do not forget to sort in the order defined by operator<!  */
      std::sort (tiles.begin (), tiles.end ());
    }

  assert (!tiles.empty ());
}

/* Ensure that walkableTiles is filled.  */
void
FillWalkableTiles ()
{
  FillWalkableArray (walkableTiles_ts_players,
    [] (int x, int y)
      {
        return SpawnMap[y][x] & SPAWNMAPFLAG_PLAYER;
      });

  FillWalkableArray (walkableTiles_ts_banks,
    [] (int x, int y)
      {
        return SpawnMap[y][x] & SPAWNMAPFLAG_BANK;
      });

  FillWalkableArray (walkableTiles,
    [] (int x, int y)
      {
        return true;
      });
}

} // anonymous namespace

/* Return the minimum necessary amount of locked coins.  This replaces the
   old NAME_COIN_AMOUNT constant and makes it more dynamic, so that we can
   change it with hard forks.  */
CAmount
GetNameCoinAmount (const Consensus::Params& param, unsigned nHeight)
{
  if (param.rules->ForkInEffect (FORK_TIMESAVE, nHeight))
    return 100 * COIN;
  if (param.rules->ForkInEffect (FORK_LESSHEARTS, nHeight))
    return 200 * COIN;
  if (param.rules->ForkInEffect (FORK_POISON, nHeight))
    return 10 * COIN;
  return COIN;
}

/* ************************************************************************** */
/* KilledByInfo.  */

bool
KilledByInfo::HasDeathTax () const
{
  return reason != KILLED_SPAWN;
}

bool
KilledByInfo::DropCoins (const GameState& state,
                         const PlayerState& victim) const
{
  if (!state.ForkInEffect (FORK_LESSHEARTS))
    return true;

  /* If the player is poisoned, no dropping of coins.  Note that we have
     to allow ==0 here (despite what gamestate.h says), since that is the
     case precisely when we are killing the player right now due to poison.  */
  if (victim.remainingLife >= 0)
    return false;

  assert (victim.remainingLife == -1);
  return true;
}

bool
KilledByInfo::CanRefund (const GameState& state,
                         const PlayerState& victim) const
{
  if (!state.ForkInEffect (FORK_LESSHEARTS))
    return false;

  switch (reason)
    {
    case KILLED_SPAWN:

      /* Before life-steal fork, poisoned players were not refunded.  */
      if (!state.ForkInEffect (FORK_LIFESTEAL) && victim.remainingLife >= 0)
        return false;

      return true;

    case KILLED_POISON:
      return state.ForkInEffect (FORK_LIFESTEAL);

    default:
      return false;
    }

  assert (false);
}

/* ************************************************************************** */
/* AttackableCharacter and CharactersOnTiles.  */

void
AttackableCharacter::AttackBy (const CharacterID& attackChid,
                               const PlayerState& pl)
{
  /* Do not attack same colour.  */
  if (color == pl.color)
    return;

  assert (attackers.count (attackChid) == 0);
  attackers.insert (attackChid);
}

void
AttackableCharacter::AttackSelf (const GameState& state)
{
  if (!state.ForkInEffect (FORK_LIFESTEAL))
    {
      assert (attackers.count (chid) == 0);
      attackers.insert (chid);
    }
}

void
CharactersOnTiles::EnsureIsBuilt (const GameState& state)
{
  if (built)
    return;
  assert (tiles.empty ());

  for (const auto& p : state.players)
    for (const auto& pc : p.second.characters)
      {
        // newly spawned hunters not attackable
        if (state.ForkInEffect (FORK_TIMESAVE))
          if (CharacterIsProtected(pc.second.stay_in_spawn_area))
          {
            // printf("protection: character at x=%d y=%d is protected\n", pc.second.coord.x, pc.second.coord.y);
            continue;
          }

        AttackableCharacter a;
        a.chid = CharacterID (p.first, pc.first);
        a.color = p.second.color;
        a.drawnLife = 0;

        tiles.insert (std::make_pair (pc.second.coord, a));
      }
  built = true;
}

void
CharactersOnTiles::ApplyAttacks (const GameState& state,
                                 const std::vector<Move>& moves)
{
  for (const auto& m : moves)
    {
      if (m.destruct.empty ())
        continue;

      const PlayerStateMap::const_iterator miPl = state.players.find (m.player);
      assert (miPl != state.players.end ());
      const PlayerState& pl = miPl->second;
      for (const int i : m.destruct)
        {
          const std::map<int, CharacterState>::const_iterator miCh
            = pl.characters.find (i);
          if (miCh == pl.characters.end ())
            continue;
          const CharacterID chid(m.player, i);
          if (state.crownHolder == chid)
            continue;

          // hunters in spectator mode can't attack
          const CharacterState& ch = miCh->second;
          if ((state.ForkInEffect (FORK_TIMESAVE)) &&
              (CharacterInSpectatorMode(ch.stay_in_spawn_area)))
          {
              // printf("protection: character at x=%d y=%d can't attack\n", ch.coord.x, ch.coord.y);
              continue;
          }

          EnsureIsBuilt (state);

          const int radius = GetDestructRadius (state, i == 0);

          const Coord& c = ch.coord;
          for (int y = c.y - radius; y <= c.y + radius; y++)
            for (int x = c.x - radius; x <= c.x + radius; x++)
              {
                const std::pair<Map::iterator, Map::iterator> iters
                  = tiles.equal_range (Coord (x, y));
                for (Map::iterator it = iters.first; it != iters.second; ++it)
                  {
                    AttackableCharacter& a = it->second;
                    if (a.chid == chid)
                      a.AttackSelf (state);
                    else
                      a.AttackBy (chid, pl);
                  }
              }
        }
    }
}

void
CharactersOnTiles::DrawLife (GameState& state, StepResult& result)
{
  if (!built)
    return;

  /* Find damage amount if we have life steal in effect.  */
  const bool lifeSteal = state.ForkInEffect (FORK_LIFESTEAL);
  const CAmount damage = GetNameCoinAmount (*state.param, state.nHeight);

  for (auto& tile : tiles)
    {
      AttackableCharacter& a = tile.second;
      if (a.attackers.empty ())
        continue;
      assert (a.drawnLife == 0);

      /* Find the player state of the attacked character.  */
      PlayerStateMap::iterator vit = state.players.find (a.chid.player);
      assert (vit != state.players.end ());
      PlayerState& victim = vit->second;

      /* In case of life steal, actually draw life.  The coins are not yet
         added to the attacker, but instead their total amount is saved
         for future redistribution.  */
      if (lifeSteal)
        {
          assert (a.chid.index == 0);

          CAmount fullDamage = damage * a.attackers.size ();
          if (fullDamage > victim.value)
            fullDamage = victim.value;

          victim.value -= fullDamage;
          a.drawnLife += fullDamage;

          /* If less than the minimum amount remains, als that is drawn
             and later added to the game fund.  */
          assert (victim.value >= 0);
          if (victim.value < damage)
            {
              a.drawnLife += victim.value;
              victim.value = 0;
            }
        }
      assert (victim.value >= 0);
      assert (a.drawnLife >= 0);

      /* If we have life steal and there is remaining health, let
         the player survive.  Note that it must have at least the minimum
         value.  If "split coins" are remaining, we still kill it.  */
      if (lifeSteal && victim.value != 0)
        {
          assert (victim.value >= damage);
          continue;
        }

      if (a.chid.index == 0)
        for (std::set<CharacterID>::const_iterator at = a.attackers.begin ();
             at != a.attackers.end (); ++at)
          {
            const KilledByInfo killer(*at);
            result.KillPlayer (a.chid.player, killer);
          }

      if (victim.characters.count (a.chid.index) > 0)
        {
          assert (a.attackers.begin () != a.attackers.end ());
          const KilledByInfo& info(*a.attackers.begin ());
          state.HandleKilledLoot (a.chid.player, a.chid.index, info, result);
          victim.characters.erase (a.chid.index);
        }
    }
}

void
CharactersOnTiles::DefendMutualAttacks (const GameState& state)
{
  if (!built)
    return;

  /* Build up a set of all (directed) attacks happening.  The pairs
     mean an attack (from, to).  This is then later used to determine
     mutual attacks, and remove them accordingly.

     One can probably do this in a more efficient way, but for now this
     is how it is implemented.  */

  typedef std::pair<CharacterID, CharacterID> Attack;
  std::set<Attack> attacks;
  for (const auto& tile : tiles)
    {
      const AttackableCharacter& a = tile.second;
      for (std::set<CharacterID>::const_iterator mi = a.attackers.begin ();
           mi != a.attackers.end (); ++mi)
        attacks.insert (std::make_pair (*mi, a.chid));
    }

  for (auto& tile : tiles)
    {
      AttackableCharacter& a = tile.second;

      std::set<CharacterID> notDefended;
      for (std::set<CharacterID>::const_iterator mi = a.attackers.begin ();
           mi != a.attackers.end (); ++mi)
        {
          const Attack counterAttack(a.chid, *mi);
          if (attacks.count (counterAttack) == 0)
            notDefended.insert (*mi);
        }

      a.attackers.swap (notDefended);
    }
}

void
CharactersOnTiles::DistributeDrawnLife (RandomGenerator& rnd,
                                        GameState& state) const
{
  if (!built)
    return;

  const CAmount damage = GetNameCoinAmount (*state.param, state.nHeight);

  /* Life is already drawn.  It remains to distribute the drawn balances
     from each attacked character back to its attackers.  For this,
     we first find the still alive players and assemble them in a map.  */
  std::map<CharacterID, PlayerState*> alivePlayers;
  for (const auto& tile : tiles)
    {
      const AttackableCharacter& a = tile.second;
      assert (alivePlayers.count (a.chid) == 0);

      /* Only non-hearted characters should be around if this is called,
         since this means that life-steal is in effect.  */
      assert (a.chid.index == 0);

      const PlayerStateMap::iterator pit = state.players.find (a.chid.player);
      if (pit != state.players.end ())
        {
          PlayerState& pl = pit->second;
          assert (pl.characters.count (a.chid.index) > 0);
          alivePlayers.insert (std::make_pair (a.chid, &pl));
        }
    }

  /* Now go over all attacks and distribute life to the attackers.  */
  for (const auto& tile : tiles)
    {
      const AttackableCharacter& a = tile.second;
      if (a.attackers.empty () || a.drawnLife == 0)
        continue;

      /* Find attackers that are still alive.  We will randomly distribute
         coins to them later on.  */
      std::vector<CharacterID> alive;
      for (std::set<CharacterID>::const_iterator mi = a.attackers.begin ();
           mi != a.attackers.end (); ++mi)
        if (alivePlayers.count (*mi) > 0)
          alive.push_back (*mi);

      /* Distribute the drawn life randomly until either all is spent
         or all alive attackers have gotten some.  */
      CAmount toSpend = a.drawnLife;
      while (!alive.empty () && toSpend >= damage)
        {
          const unsigned ind = rnd.GetIntRnd (alive.size ());
          const std::map<CharacterID, PlayerState*>::iterator plIt
            = alivePlayers.find (alive[ind]);
          assert (plIt != alivePlayers.end ());

          toSpend -= damage;
          plIt->second->value += damage;

          /* Do not use a silly trick like swapping in the last element.
             We want to keep the array ordered at all times.  The order is
             important with respect to consensus, and this makes the consensus
             protocol "clearer" to describe.  */
          alive.erase (alive.begin () + ind);
        }

      /* Distribute the remaining value to the game fund.  */
      assert (toSpend >= 0);
      state.gameFund += toSpend;
    }
}

/* ************************************************************************** */
/* CharacterState and PlayerState.  */

void
CharacterState::Spawn (const GameState& state, int color, RandomGenerator &rnd)
{
  // less possible player spawn tiles
  if (state.ForkInEffect (FORK_TIMESAVE))
  {
      FillWalkableTiles ();

      const int pos = rnd.GetIntRnd (walkableTiles_ts_players.size ());
      coord = walkableTiles_ts_players[pos];

      dir = rnd.GetIntRnd (1, 8);
      if (dir >= 5)
        ++dir;
      assert (dir >= 1 && dir <= 9 && dir != 5);
  }
  /* Pick a random walkable spawn location after the life-steal fork.  */
  else if (state.ForkInEffect (FORK_LIFESTEAL))
    {
      FillWalkableTiles ();

      const int pos = rnd.GetIntRnd (walkableTiles.size ());
      coord = walkableTiles[pos];

      dir = rnd.GetIntRnd (1, 8);
      if (dir >= 5)
        ++dir;
      assert (dir >= 1 && dir <= 9 && dir != 5);
    }

  /* Use old logic with fixed spawns in the corners before the fork.  */
  else
    {
      const int pos = rnd.GetIntRnd(2 * SPAWN_AREA_LENGTH - 1);
      const int x = pos < SPAWN_AREA_LENGTH ? pos : 0;
      const int y = pos < SPAWN_AREA_LENGTH ? 0 : pos - SPAWN_AREA_LENGTH;
      switch (color)
        {
        case 0: // Yellow (top-left)
          coord = Coord(x, y);
          break;
        case 1: // Red (top-right)
          coord = Coord(MAP_WIDTH - 1 - x, y);
          break;
        case 2: // Green (bottom-right)
          coord = Coord(MAP_WIDTH - 1 - x, MAP_HEIGHT - 1 - y);
          break;
        case 3: // Blue (bottom-left)
          coord = Coord(x, MAP_HEIGHT - 1 - y);
          break;
        default:
          throw std::runtime_error("CharacterState::Spawn: incorrect color");
        }

      /* Under the regtest rules, everyone is placed into the yellow corner.
         This allows quicker fights for testing.  */
      if (state.TestingRules ())
        coord = Coord(x, y);

      // Set look-direction for the sprite
      if (coord.x == 0)
        {
          if (coord.y == 0)
            dir = 3;
          else if (coord.y == MAP_HEIGHT - 1)
            dir = 9;
          else
            dir = 6;
        }
      else if (coord.x == MAP_WIDTH - 1)
        {
          if (coord.y == 0)
            dir = 1;
          else if (coord.y == MAP_HEIGHT - 1)
            dir = 7;
          else
            dir = 4;
        }
      else if (coord.y == 0)
        dir = 2;
      else if (coord.y == MAP_HEIGHT - 1)
        dir = 8;
    }

  StopMoving();
}

// Returns direction from c1 to c2 as a number from 1 to 9 (as on the numeric keypad)
static unsigned char
GetDirection (const Coord& c1, const Coord& c2)
{
    int dx = c2.x - c1.x;
    int dy = c2.y - c1.y;
    if (dx < -1)
        dx = -1;
    else if (dx > 1)
        dx = 1;
    if (dy < -1)
        dy = -1;
    else if (dy > 1)
        dy = 1;

    return (1 - dy) * 3 + dx + 2;
}

// Simple straight-line motion
void CharacterState::MoveTowardsWaypoint()
{
    if (waypoints.empty())
    {
        from = coord;
        return;
    }
    if (coord == waypoints.back())
    {
        from = coord;
        do
        {
            waypoints.pop_back();
            if (waypoints.empty())
                return;
        } while (coord == waypoints.back());
    }

    struct Helper
    {
        static int CoordStep(int x, int target)
        {
            if (x < target)
                return x + 1;
            else if (x > target)
                return x - 1;
            else
                return x;
        }

        // Compute new 'v' coordinate using line slope information applied to the 'u' coordinate
        // 'u' is reference coordinate (largest among dx, dy), 'v' is the coordinate to be updated
        static int CoordUpd(int u, int v, int du, int dv, int from_u, int from_v)
        {
            if (dv != 0)
            {
                int tmp = (u - from_u) * dv;
                int res = (abs(tmp) + abs(du) / 2) / du;
                if (tmp < 0)
                    res = -res;
                return res + from_v;
            }
            else
                return v;
        }
    };

    Coord new_c;
    Coord target = waypoints.back();
    
    int dx = target.x - from.x;
    int dy = target.y - from.y;
    
    if (abs(dx) > abs(dy))
    {
        new_c.x = Helper::CoordStep(coord.x, target.x);
        new_c.y = Helper::CoordUpd(new_c.x, coord.y, dx, dy, from.x, from.y);
    }
    else
    {
        new_c.y = Helper::CoordStep(coord.y, target.y);
        new_c.x = Helper::CoordUpd(new_c.y, coord.x, dy, dx, from.y, from.x);
    }

    if (!IsWalkableCoord (new_c))
        StopMoving();
    else
    {
        unsigned char new_dir = GetDirection(coord, new_c);
        // If not moved (new_dir == 5), retain old direction
        if (new_dir != 5)
            dir = new_dir;
        coord = new_c;

        if (coord == target)
        {
            from = coord;
            do
            {
                waypoints.pop_back();
            } while (!waypoints.empty() && coord == waypoints.back());
        }
    }
}

std::vector<Coord> CharacterState::DumpPath(const std::vector<Coord> *alternative_waypoints /* = NULL */) const
{
    std::vector<Coord> ret;
    CharacterState tmp = *this;

    if (alternative_waypoints)
    {
        tmp.StopMoving();
        tmp.waypoints = *alternative_waypoints;
    }

    if (!tmp.waypoints.empty())
    {
        do
        {
            ret.push_back(tmp.coord);
            tmp.MoveTowardsWaypoint();
        } while (!tmp.waypoints.empty());
        if (ret.empty() || ret.back() != tmp.coord)
            ret.push_back(tmp.coord);
    }
    return ret;
}

/**
 * Calculate total length (in the same L-infinity sense that gives the
 * actual movement time) of the outstanding path.
 * @param altWP Optionally provide alternative waypoints (for queued moves).
 * @return Time necessary to finish current path in blocks.
 */
unsigned
CharacterState::TimeToDestination (const WaypointVector* altWP) const
{
  bool reverse = false;
  if (!altWP)
    {
      altWP = &waypoints;
      reverse = true;
    }

  /* In order to handle both reverse and non-reverse correctly, calculate
     first the length of the path alone and only later take the initial
     piece from coord on into account.  */

  if (altWP->empty ())
    return 0;

  unsigned res = 0;
  WaypointVector::const_iterator i = altWP->begin ();
  Coord last = *i;
  for (++i; i != altWP->end (); ++i)
    {
      res += distLInf (last, *i);
      last = *i;
    }

  if (reverse)
    res += distLInf (coord, altWP->back ());
  else
    res += distLInf (coord, altWP->front ());

  return res;
}

CAmount
CharacterState::CollectLoot (LootInfo newLoot, int nHeight, CAmount carryCap)
{
  const CAmount totalBefore = loot.nAmount + newLoot.nAmount;

  CAmount freeCap = carryCap - loot.nAmount;
  if (freeCap < 0)
    {
      /* This means that the character is carrying more than allowed
         (or carryCap == -1, which is handled later anyway).  This
         may happen during transition periods, handle it gracefully.  */
      freeCap = 0;
    }

  CAmount remaining;
  if (carryCap == -1 || newLoot.nAmount <= freeCap)
    remaining = 0;
  else
    remaining = newLoot.nAmount - freeCap;

  if (remaining > 0)
    newLoot.nAmount -= remaining;
  loot.Collect (newLoot, nHeight);

  assert (remaining >= 0 && newLoot.nAmount >= 0);
  assert (totalBefore == loot.nAmount + remaining);
  assert (carryCap == -1 || newLoot.nAmount <= freeCap);
  assert (newLoot.nAmount == 0 || carryCap == -1 || loot.nAmount <= carryCap);

  return remaining;
}

void
PlayerState::SpawnCharacter (const GameState& state, RandomGenerator &rnd)
{
  characters[next_character_index++].Spawn (state, color, rnd);
}

bool
PlayerState::CanSpawnCharacter() const
{
  return characters.size () < MAX_CHARACTERS_PER_PLAYER
          && next_character_index < MAX_CHARACTERS_PER_PLAYER_TOTAL;
}

UniValue PlayerState::ToJsonValue(int crown_index, bool dead /* = false*/) const
{
  UniValue obj(UniValue::VOBJ);
  obj.pushKV ("color", (int)color);
  obj.pushKV ("value", ValueFromAmount(value));

  /* If the character is poisoned, write that out.  Otherwise just
     leave the field off.  */
  if (remainingLife > 0)
    obj.pushKV ("poison", remainingLife);
  else
    assert (remainingLife == -1);

  if (!message.empty())
    {
      obj.pushKV ("msg", message);
      obj.pushKV ("msg_block", message_block);
    }

  if (!dead)
    {
      if (!address.empty())
        obj.pushKV ("address", address);
      if (!addressLock.empty())
        obj.pushKV ("addressLock", address);
    }
  else
    {
      // Note: not all dead players are listed - only those who sent chat
      // messages in their last move.
      assert(characters.empty());
      obj.pushKV ("dead", 1);
    }

  UniValue characterObj(UniValue::VOBJ);
  for (const auto& pc : characters)
    {
      int i = pc.first;
      const CharacterState &ch = pc.second;
      characterObj.pushKV (strprintf ("%d", i),
                           ch.ToJsonValue (i == crown_index));
    }
  obj.pushKV ("characters", characterObj);

  return obj;
}

UniValue CharacterState::ToJsonValue(bool has_crown) const
{
    UniValue obj(UniValue::VOBJ);
    obj.pushKV("x", coord.x);
    obj.pushKV("y", coord.y);
    if (!waypoints.empty())
    {
        obj.pushKV("fromX", from.x);
        obj.pushKV("fromY", from.y);
        UniValue arr(UniValue::VARR);
        for (int i = waypoints.size() - 1; i >= 0; i--)
        {
            arr.push_back(waypoints[i].x);
            arr.push_back(waypoints[i].y);
        }
        obj.pushKV("wp", arr);
    }
    obj.pushKV("dir", (int)dir);
    obj.pushKV("stay_in_spawn_area", stay_in_spawn_area);
    obj.pushKV("loot", ValueFromAmount(loot.nAmount));
    if (has_crown)
        obj.pushKV("has_crown", true);

    return obj;
}

/* ************************************************************************** */
/* GameState.  */

static void
SetOriginalBanks (std::map<Coord, unsigned>& banks)
{
  assert (banks.empty ());
  for (int d = 0; d < SPAWN_AREA_LENGTH; ++d)
    {
      banks.insert (std::make_pair (Coord (0, d), 0));
      banks.insert (std::make_pair (Coord (d, 0), 0));
      banks.insert (std::make_pair (Coord (MAP_WIDTH - 1, d), 0));
      banks.insert (std::make_pair (Coord (d, MAP_HEIGHT - 1), 0));
      banks.insert (std::make_pair (Coord (0, MAP_HEIGHT - d - 1), 0));
      banks.insert (std::make_pair (Coord (MAP_WIDTH - d - 1, 0), 0));
      banks.insert (std::make_pair (Coord (MAP_WIDTH - 1,
                                           MAP_HEIGHT - d - 1), 0));
      banks.insert (std::make_pair (Coord (MAP_WIDTH - d - 1,
                                           MAP_HEIGHT - 1), 0));
    }

  assert (banks.size () == 4 * (2 * SPAWN_AREA_LENGTH - 1));
  for (const auto& b : banks)
    {
      assert (IsOriginalSpawnAreaCoord (b.first));
      assert (b.second == 0);
    }
}

GameState::GameState(const Consensus::Params& p)
  : param(&p)
{
    crownPos.x = CROWN_START_X;
    crownPos.y = CROWN_START_Y;
    gameFund = 0;
    nHeight = -1;
    nDisasterHeight = -1;
    hashBlock.SetNull ();
    SetOriginalBanks (banks);
}

UniValue GameState::ToJsonValue() const
{
    UniValue obj(UniValue::VOBJ);

    UniValue jsonPlayers(UniValue::VOBJ);
    for (const auto& p : players)
      {
        int crown_index = p.first == crownHolder.player ? crownHolder.index : -1;
        jsonPlayers.pushKV(p.first, p.second.ToJsonValue(crown_index));
      }

    // Save chat messages of dead players
    for (const auto& p : dead_players_chat)
        jsonPlayers.pushKV(p.first, p.second.ToJsonValue(-1, true));

    obj.pushKV("players", jsonPlayers);

    UniValue jsonLoot(UniValue::VARR);
    for (const auto& p : loot)
      {
        UniValue subobj(UniValue::VOBJ);
        subobj.pushKV("x", p.first.x);
        subobj.pushKV("y", p.first.y);
        subobj.pushKV("amount", ValueFromAmount(p.second.nAmount));
        UniValue blk_rng(UniValue::VARR);
        blk_rng.push_back(p.second.firstBlock);
        blk_rng.push_back(p.second.lastBlock);
        subobj.pushKV("blockRange", blk_rng);
        jsonLoot.push_back(subobj);
      }
    obj.pushKV("loot", jsonLoot);

    UniValue jsonHearts(UniValue::VARR);
    for (const auto& c : hearts)
      {
        UniValue subobj(UniValue::VOBJ);
        subobj.pushKV ("x", c.x);
        subobj.pushKV ("y", c.y);
        jsonHearts.push_back (subobj);
      }
    obj.pushKV ("hearts", jsonHearts);

    UniValue jsonBanks(UniValue::VARR);
    for (const auto& b : banks)
      {
        UniValue subobj(UniValue::VOBJ);
        subobj.pushKV ("x", b.first.x);
        subobj.pushKV ("y", b.first.y);
        subobj.pushKV ("life", static_cast<int> (b.second));
        jsonBanks.push_back (subobj);
      }
    obj.pushKV ("banks", jsonBanks);

    UniValue jsonCrown(UniValue::VOBJ);
    jsonCrown.pushKV("x", crownPos.x);
    jsonCrown.pushKV("y", crownPos.y);
    if (!crownHolder.player.empty())
    {
        jsonCrown.pushKV("holderName", crownHolder.player);
        jsonCrown.pushKV("holderIndex", crownHolder.index);
    }
    obj.pushKV("crown", jsonCrown);

    obj.pushKV ("gameFund", ValueFromAmount (gameFund));
    obj.pushKV ("height", nHeight);
    obj.pushKV ("disasterHeight", nDisasterHeight);
    obj.pushKV ("hashBlock", hashBlock.ToString().c_str());

    return obj;
}

void GameState::AddLoot(Coord coord, CAmount nAmount)
{
    if (nAmount == 0)
        return;
    std::map<Coord, LootInfo>::iterator mi = loot.find(coord);
    if (mi != loot.end())
    {
        if ((mi->second.nAmount += nAmount) == 0)
            loot.erase(mi);
        else
            mi->second.lastBlock = nHeight;
    }
    else
        loot.insert(std::make_pair(coord, LootInfo(nAmount, nHeight)));
}

/*

We try to split loot equally among players on a loot tile.
If a character hits its carrying capacity, the remaining coins
are split among the others.  To achieve this effect, we sort
the players by increasing (remaining) capacity -- so the ones
with least remaining capacity pick their share first, and if
it fills the capacity, leave extra coins lying around for the
others to pick up.  Since they are then filled up anyway,
it won't matter if others also leave coins, so no "iteration"
is required.

Note that for indivisible amounts the order of players matters.
For equal capacity (which is particularly true before the
hardfork point), we sort by player/character.  This makes
the new logic compatible with the old one.

The class CharacterOnLootTile takes this sorting into account.

*/

class CharacterOnLootTile
{
public:

  PlayerID pid;
  int cid;

  CharacterState* ch;
  CAmount carryCap;

  /* Get remaining carrying capacity.  */
  inline CAmount
  GetRemainingCapacity () const
  {
    if (carryCap == -1)
      return -1;

    /* During periods of change in the carrying capacity, there may be
       players "overloaded".  Take care of them.  */
    if (carryCap < ch->loot.nAmount)
      return 0;

    return carryCap - ch->loot.nAmount;
  }

  friend bool operator< (const CharacterOnLootTile& a,
                         const CharacterOnLootTile& b);

};

bool
operator< (const CharacterOnLootTile& a, const CharacterOnLootTile& b)
{
  const CAmount remA = a.GetRemainingCapacity ();
  const CAmount remB = b.GetRemainingCapacity ();

  if (remA == remB)
    {
      if (a.pid != b.pid)
        return a.pid < b.pid;
      return a.cid < b.cid;
    }

  if (remA == -1)
    {
      assert (remB >= 0);
      return false;
    }
  if (remB == -1)
    {
      assert (remA >= 0);
      return true;
    }

  return remA < remB;
}

void GameState::DivideLootAmongPlayers()
{
    std::map<Coord, int> playersOnLootTile;
    std::vector<CharacterOnLootTile> collectors;
    for (auto& p : players)
      for (auto& pc : p.second.characters)
        {
          CharacterOnLootTile tileChar;

          tileChar.pid = p.first;
          tileChar.cid = pc.first;
          tileChar.ch = &pc.second;

          const bool isCrownHolder = (tileChar.pid == crownHolder.player
                                      && tileChar.cid == crownHolder.index);
          tileChar.carryCap = GetCarryingCapacity (*this, tileChar.cid == 0,
                                                   isCrownHolder);

          const Coord& coord = tileChar.ch->coord;

          // ghosting with phasing-in
          if (ForkInEffect (FORK_TIMESAVE))
            if ((((coord.x % 2) + (coord.y % 2) > 1) && (nHeight % 500 >= 300)) ||  // for 150 blocks, every 4th coin spawn is ghosted
                (((coord.x % 2) + (coord.y % 2) > 0) && (nHeight % 500 >= 450)) ||  // for 30 blocks, 3 out of 4 coin spawns are ghosted
                (nHeight % 500 >= 480))                                             // for 20 blocks, full ghosting
                     continue;

          if (loot.count (coord) > 0)
            {
              std::map<Coord, int>::iterator mi;
              mi = playersOnLootTile.find (coord);

              if (mi != playersOnLootTile.end ())
                mi->second++;
              else
                playersOnLootTile.insert (std::make_pair (coord, 1));

              collectors.push_back (tileChar);
            }
        }

    std::sort (collectors.begin (), collectors.end ());
    for (std::vector<CharacterOnLootTile>::iterator i = collectors.begin ();
         i != collectors.end (); ++i)
      {
        const Coord& coord = i->ch->coord;
        std::map<Coord, int>::iterator mi = playersOnLootTile.find (coord);
        assert (mi != playersOnLootTile.end ());

        LootInfo lootInfo = loot[coord];
        assert (mi->second > 0);
        lootInfo.nAmount /= (mi->second--);

        /* If amount was ~1e-8 and several players moved onto it, then
           some of them will get nothing.  */
        if (lootInfo.nAmount > 0)
          {
            const CAmount rem = i->ch->CollectLoot (lootInfo, nHeight,
                                                    i->carryCap);
            AddLoot (coord, rem - lootInfo.nAmount);
          }
      }
}

void GameState::UpdateCrownState(bool &respawn_crown)
{
    respawn_crown = false;
    if (crownHolder.player.empty())
        return;

    std::map<PlayerID, PlayerState>::const_iterator mi = players.find(crownHolder.player);
    if (mi == players.end())
    {
        // Player is dead, drop the crown
        crownHolder = CharacterID();
        return;
    }

    const PlayerState &pl = mi->second;
    std::map<int, CharacterState>::const_iterator mi2 = pl.characters.find(crownHolder.index);
    if (mi2 == pl.characters.end())
    {
        // Character is dead, drop the crown
        crownHolder = CharacterID();
        return;
    }

    if (IsBank (mi2->second.coord))
    {
        // Character entered spawn area, drop the crown
        crownHolder = CharacterID();
        respawn_crown = true;
    }
    else
    {
        // Update crown position to character position
        crownPos = mi2->second.coord;
    }
}

void
GameState::CrownBonus (CAmount nAmount)
{
  if (!crownHolder.player.empty ())
    {
      PlayerState& p = players[crownHolder.player];
      CharacterState& ch = p.characters[crownHolder.index];

      const LootInfo crownLoot(nAmount, nHeight);
      const CAmount cap = GetCarryingCapacity (*this, crownHolder.index == 0,
                                               true);
      const CAmount rem = ch.CollectLoot (crownLoot, nHeight, cap);

      /* We keep to the logic of "crown on the floor -> game fund" and
         don't distribute coins that can not be hold by the crown holder
         due to carrying capacity to the map.  */
      gameFund += rem;
    }
  else
    gameFund += nAmount;
}

unsigned
GameState::GetNumInitialCharacters () const
{
  return (ForkInEffect (FORK_POISON) ? 1 : 3);
}

bool
GameState::IsBank (const Coord& c) const
{
  assert (!banks.empty ());
  return banks.count (c) > 0;
}

CAmount
GameState::GetCoinsOnMap () const
{
  CAmount onMap = 0;
  for (const auto& l : loot)
    onMap += l.second.nAmount;
  for (const auto& p : players)
    {
      onMap += p.second.value;
      for (const auto& pc : p.second.characters)
        onMap += pc.second.loot.nAmount;
    }

  return onMap;
}

void GameState::CollectHearts(RandomGenerator &rnd)
{
    std::map<Coord, std::vector<PlayerState*> > playersOnHeartTile;
    for (std::map<PlayerID, PlayerState>::iterator mi = players.begin(); mi != players.end(); mi++)
    {
        PlayerState *pl = &mi->second;
        if (!pl->CanSpawnCharacter())
            continue;
        for (const auto& pc : pl->characters)
          {
            const CharacterState &ch = pc.second;

            if (hearts.count(ch.coord))
                playersOnHeartTile[ch.coord].push_back(pl);
          }
    }
    for (std::map<Coord, std::vector<PlayerState*> >::iterator mi = playersOnHeartTile.begin(); mi != playersOnHeartTile.end(); mi++)
    {
        const Coord &c = mi->first;
        std::vector<PlayerState*> &v = mi->second;
        int n = v.size();
        int i;
        for (;;)
        {
            if (!n)
            {
                i = -1;
                break;
            }
            i = n == 1 ? 0 : rnd.GetIntRnd(n);
            if (v[i]->CanSpawnCharacter())
                break;
            v.erase(v.begin() + i);
            n--;
        }
        if (i >= 0)
        {
            v[i]->SpawnCharacter(*this, rnd);
            hearts.erase(c);
        }
    }
}

void GameState::CollectCrown(RandomGenerator &rnd, bool respawn_crown)
{
    if (!crownHolder.player.empty())
    {
        assert(!respawn_crown);
        return;
    }

    if (respawn_crown)
    {   
        int a = rnd.GetIntRnd(NUM_CROWN_LOCATIONS);
        crownPos.x = CrownSpawn[2 * a];
        crownPos.y = CrownSpawn[2 * a + 1];
    }

    std::vector<CharacterID> charactersOnCrownTile;
    for (const auto& pl : players)
      for (const auto& pc : pl.second.characters)
        if (pc.second.coord == crownPos)
          charactersOnCrownTile.push_back(CharacterID(pl.first, pc.first));
    int n = charactersOnCrownTile.size();
    if (!n)
        return;
    int i = n == 1 ? 0 : rnd.GetIntRnd(n);
    crownHolder = charactersOnCrownTile[i];
}

// Loot is pushed out from the spawn area to avoid some ambiguities with banking rules (as spawn areas are also banks)
// Note: the map must be constructed in such a way that there are no obstacles near spawn areas
static Coord
PushCoordOutOfSpawnArea(const Coord &c)
{
    if (!IsOriginalSpawnAreaCoord (c))
        return c;
    if (c.x == 0)
    {
        if (c.y == 0)
            return Coord(c.x + 1, c.y + 1);
        else if (c.y == MAP_HEIGHT - 1)
            return Coord(c.x + 1, c.y - 1);
        else
            return Coord(c.x + 1, c.y);
    }
    else if (c.x == MAP_WIDTH - 1)
    {
        if (c.y == 0)
            return Coord(c.x - 1, c.y + 1);
        else if (c.y == MAP_HEIGHT - 1)
            return Coord(c.x - 1, c.y - 1);
        else
            return Coord(c.x - 1, c.y);
    }
    else if (c.y == 0)
        return Coord(c.x, c.y + 1);
    else if (c.y == MAP_HEIGHT - 1)
        return Coord(c.x, c.y - 1);
    else
        return c;     // Should not happen
}

void
GameState::HandleKilledLoot (const PlayerID& pId, int chInd,
                             const KilledByInfo& info, StepResult& step)
{
  const PlayerStateMap::const_iterator mip = players.find (pId);
  assert (mip != players.end ());
  const PlayerState& pc = mip->second;
  assert (pc.value >= 0);
  const std::map<int, CharacterState>::const_iterator mic
    = pc.characters.find (chInd);
  assert (mic != pc.characters.end ());
  const CharacterState& ch = mic->second;

  /* If refunding is possible, do this for the locked amount right now.
     Later on, exclude the amount from further considerations.  */
  bool refunded = false;
  if (chInd == 0 && info.CanRefund (*this, pc))
    {
      CollectedLootInfo collectedLoot;
      collectedLoot.SetRefund (pc.value, nHeight);
      CollectedBounty b(pId, chInd, collectedLoot, pc.address);
      step.bounties.push_back (b);
      refunded = true;
    }

  /* Calculate loot.  If we kill a general, take the locked coin amount
     into account, as well.  When life-steal is in effect, the value
     should already be drawn to zero (unless we have a cause of death
     that refunds).  */
  CAmount nAmount = ch.loot.nAmount;
  if (chInd == 0 && !refunded)
    {
      assert (!ForkInEffect (FORK_LIFESTEAL) || pc.value == 0);
      nAmount += pc.value;
    }

  /* Apply the miner tax: 4%.  */
  if (info.HasDeathTax ())
    {
      const CAmount nTax = nAmount / 25;
      step.nTaxAmount += nTax;
      nAmount -= nTax;
    }

  /* If requested (and the corresponding fork is in effect), add the coins
     to the game fund instead of dropping them.  */
  if (!info.DropCoins (*this, pc))
    {
      gameFund += nAmount;
      return;
    }

  /* Just drop the loot.  Push the coordinate out of spawn if applicable.
     After the life-steal fork with dynamic banks, we no longer push.  */
  Coord lootPos = ch.coord;
  if (!ForkInEffect (FORK_LIFESTEAL))
    lootPos = PushCoordOutOfSpawnArea (lootPos);
  AddLoot (lootPos, nAmount);
}

void
GameState::FinaliseKills (StepResult& step)
{
  const PlayerSet& killedPlayers = step.GetKilledPlayers ();
  const KilledByMap& killedBy = step.GetKilledBy ();

  /* Kill depending characters.  */
  for (const auto& victim : killedPlayers)
    {
      const PlayerState& victimState = players.find (victim)->second;

      /* Take a look at the killed info to determine flags for handling
         the player loot.  */
      const KilledByMap::const_iterator iter = killedBy.find (victim);
      assert (iter != killedBy.end ());
      const KilledByInfo& info = iter->second;

      /* Kill all alive characters of the player.  */
      for (const auto& pc : victimState.characters)
        HandleKilledLoot (victim, pc.first, info, step);
    }

  /* Erase killed players from the state.  */
  for (const auto& victim : killedPlayers)
    players.erase (victim);
}

bool
GameState::CheckForDisaster (RandomGenerator& rng) const
{
  /* Before the hardfork, nothing should happen.  */
  if (!ForkInEffect (FORK_POISON))
    return false;

  /* Enforce max/min times.  */
  const int dist = nHeight - nDisasterHeight;
  assert (dist > 0);
  if (static_cast<unsigned> (dist) < PDISASTER_MIN_TIME)
    return false;
  if (static_cast<unsigned> (dist) >= PDISASTER_MAX_TIME)
    return true;

  /* Check random chance.  */
  return (rng.GetIntRnd (PDISASTER_PROBABILITY) == 0);
}

void
GameState::KillSpawnArea (StepResult& step)
{
  /* Even if spawn death is disabled after the corresponding softfork,
     we still want to do the loop (but not actually kill players)
     because it keeps stay_in_spawn_area up-to-date.  */

  for (auto& p : players)
    {
      std::set<int> toErase;
      for (auto& pc : p.second.characters)
        {
          const int i = pc.first;
          CharacterState &ch = pc.second;

          // process logout timer
          if (ForkInEffect (FORK_TIMESAVE))
          {
              if (IsBank (ch.coord))
              {
                  ch.stay_in_spawn_area = CHARACTER_MODE_LOGOUT; // hunters will never be on bank tile while in spectator mode
              }
              else if (SpawnMap[ch.coord.y][ch.coord.x] & SPAWNMAPFLAG_PLAYER)
              {
                  if (CharacterSpawnProtectionAlmostFinished(ch.stay_in_spawn_area))
                  {
                      // enter spectator mode if standing still
                      // notes : - movement will put the hunter in normal mode (when movement is processed)
                      //         - right now (in KillSpawnArea) waypoint updates are not yet applied for current block,
                      //           i.e. (ch.waypoints.empty()) is always true
                      ch.stay_in_spawn_area = CHARACTER_MODE_SPECTATOR_BEGIN;
                  }
                  else
                  {
                      // give new hunters 10 blocks more thinking time before ghosting ends
                      if ((nHeight % 500 < 490) || (ch.stay_in_spawn_area > 0))
                          ch.stay_in_spawn_area++;
                  }
              }
              else if (CharacterIsProtected(ch.stay_in_spawn_area)) // catch all (for hunters who spawned pre-fork)
              {
                  ch.stay_in_spawn_area++;
              }

              if (CharacterNoLogout(ch.stay_in_spawn_area))
                  continue;
          }
          else // pre-fork
          {
              if (!IsBank (ch.coord))
                {
                  ch.stay_in_spawn_area = 0;
                  continue;
                }

              /* Make sure to increment the counter in every case.  */
              assert (IsBank (ch.coord));
              const int maxStay = MaxStayOnBank (*this);
              if (ch.stay_in_spawn_area++ < maxStay || maxStay == -1)
                continue;
          }

          /* Handle the character's loot and kill the player.  */
          const KilledByInfo killer(KilledByInfo::KILLED_SPAWN);
          HandleKilledLoot (p.first, i, killer, step);
          if (i == 0)
            step.KillPlayer (p.first, killer);

          /* Cannot erase right now, because it will invalidate the
             iterator 'pc'.  */
          toErase.insert(i);
        }
      for (const int i : toErase)
        p.second.characters.erase(i);
    }
}

void
GameState::ApplyDisaster (RandomGenerator& rng)
{
  /* Set random life expectations for every player on the map.  */
  for (auto& p : players)
    {
      /* Disasters should be so far apart, that all currently alive players
         are not yet poisoned.  Check this.  In case we introduce a general
         expiry, this can be changed accordingly -- but make sure that
         poisoning doesn't actually *increase* the life expectation.  */
      assert (p.second.remainingLife == -1);

      p.second.remainingLife = rng.GetIntRnd (POISON_MIN_LIFE, POISON_MAX_LIFE);
    }

  /* Remove all hearts from the map.  */
  if (ForkInEffect (FORK_LESSHEARTS))
    hearts.clear ();

  /* Reset disaster counter.  */
  nDisasterHeight = nHeight;
}

void
GameState::DecrementLife (StepResult& step)
{
  for (auto& p : players)
    {
      if (p.second.remainingLife == -1)
        continue;

      assert (p.second.remainingLife > 0);
      --p.second.remainingLife;

      if (p.second.remainingLife == 0)
        {
          const KilledByInfo killer(KilledByInfo::KILLED_POISON);
          step.KillPlayer (p.first, killer);
        }
    }
}

void
GameState::RemoveHeartedCharacters (StepResult& step)
{
  assert (param->rules->IsForkHeight (FORK_LIFESTEAL, nHeight));

  /* Get rid of all hearts on the map.  */
  hearts.clear ();

  /* Immediately kill all hearted characters.  */
  for (auto& p : players)
    {
      std::set<int> toErase;
      for (const auto& pc : p.second.characters)
        {
          const int i = pc.first;
          if (i == 0)
            continue;

          const KilledByInfo info(KilledByInfo::KILLED_POISON);
          HandleKilledLoot (p.first, i, info, step);

          /* Cannot erase right now, because it will invalidate the
             iterator 'pc'.  */
          toErase.insert (i);
        }
      for (const int i : toErase)
        p.second.characters.erase (i);
    }
}

void
GameState::UpdateBanks (RandomGenerator& rng)
{
  if (!ForkInEffect (FORK_LIFESTEAL))
    return;

  std::map<Coord, unsigned> newBanks;

  /* Create initial set of banks at the fork itself.  */
  if (param->rules->IsForkHeight (FORK_LIFESTEAL, nHeight))
    assert (newBanks.empty ());

  /* Decrement life of existing banks and remove the ones that
     have run out.  */
  else
    {
      assert (banks.size () == DYNBANKS_NUM_BANKS);
      assert (newBanks.empty ());

      for (const auto& b : banks)
      {
        assert (b.second >= 1);

        // reset all banks as to not break things,
        // e.g. "assert (optionsSet.count (b.first) == 1)"
        if (param->rules->IsForkHeight (FORK_TIMESAVE, nHeight))
          continue;

        /* Banks with life=1 run out now.  Since banking is done before
           updating the banks in PerformStep, this means that banks that have
           life=1 and are reached in the next turn are still available.  */
        if (b.second > 1)
          newBanks.insert (std::make_pair (b.first, b.second - 1));
      }
    }

  /* Re-create banks that are missing now.  */

  assert (newBanks.size () <= DYNBANKS_NUM_BANKS);

  // less possible bank spawn tiles
  if (ForkInEffect (FORK_TIMESAVE))
  {
    FillWalkableTiles ();
    std::set<Coord> optionsSet(walkableTiles_ts_banks.begin (), walkableTiles_ts_banks.end ());
    for (const auto& b : newBanks)
    {
      assert (optionsSet.count (b.first) == 1);
      optionsSet.erase (b.first);
    }
    assert (optionsSet.size () + newBanks.size () == walkableTiles_ts_banks.size ());

    std::vector<Coord> options(optionsSet.begin (), optionsSet.end ());
    for (unsigned cnt = newBanks.size (); cnt < DYNBANKS_NUM_BANKS; ++cnt)
    {
      const int ind = rng.GetIntRnd (options.size ());
      const int life = rng.GetIntRnd (DYNBANKS_MIN_LIFE, DYNBANKS_MAX_LIFE);
      const Coord& c = options[ind];

      assert (newBanks.count (c) == 0);
      newBanks.insert (std::make_pair (c, life));

      /* Do not use a silly trick like swapping in the last element.
         We want to keep the array ordered at all times.  The order is
         important with respect to consensus, and this makes the consensus
         protocol "clearer" to describe.  */
      options.erase (options.begin () + ind);
    }
  }
  else // pre-fork
  {
    FillWalkableTiles ();
    std::set<Coord> optionsSet(walkableTiles.begin (), walkableTiles.end ());
    for (const auto& b : newBanks)
    {
      assert (optionsSet.count (b.first) == 1);
      optionsSet.erase (b.first);
    }
    assert (optionsSet.size () + newBanks.size () == walkableTiles.size ());

    std::vector<Coord> options(optionsSet.begin (), optionsSet.end ());
    for (unsigned cnt = newBanks.size (); cnt < DYNBANKS_NUM_BANKS; ++cnt)
    {
      const int ind = rng.GetIntRnd (options.size ());
      const int life = rng.GetIntRnd (DYNBANKS_MIN_LIFE, DYNBANKS_MAX_LIFE);
      const Coord& c = options[ind];

      assert (newBanks.count (c) == 0);
      newBanks.insert (std::make_pair (c, life));

      /* Do not use a silly trick like swapping in the last element.
         We want to keep the array ordered at all times.  The order is
         important with respect to consensus, and this makes the consensus
         protocol "clearer" to describe.  */
      options.erase (options.begin () + ind);
    }
  }

  banks.swap (newBanks);
  assert (banks.size () == DYNBANKS_NUM_BANKS);
}

/* ************************************************************************** */

void
CollectedBounty::UpdateAddress (const GameState& state)
{
  const PlayerID& p = character.player;
  const PlayerStateMap::const_iterator i = state.players.find (p);
  if (i == state.players.end ())
    return;

  address = i->second.address;
}

bool PerformStep(const GameState &inState, const StepData &stepData, GameState &outState, StepResult &stepResult)
{
    for (const auto& m : stepData.vMoves)
        if (!m.IsValid(inState))
            return false;

    outState = inState;

    /* Initialise basic stuff.  The disaster height is set to the old
       block's for now, but it may be reset later when we decide that
       a disaster happens at this block.  */
    outState.nHeight = inState.nHeight + 1;
    outState.nDisasterHeight = inState.nDisasterHeight;
    outState.hashBlock = stepData.newHash;
    outState.dead_players_chat.clear();

    stepResult = StepResult();

    /* Pay out game fees (except for spawns) to the game fund.  This also
       keeps track of the total fees paid into the game world by moves.  */
    CAmount moneyIn = 0;
    for (const auto& m : stepData.vMoves)
      if (!m.IsSpawn ())
        {
          const PlayerStateMap::iterator mi = outState.players.find (m.player);
          assert (mi != outState.players.end ());
          assert (m.newLocked >= mi->second.lockedCoins);
          const CAmount newFee = m.newLocked - mi->second.lockedCoins;
          outState.gameFund += newFee;
          moneyIn += newFee;
          mi->second.lockedCoins = m.newLocked;
        }
      else
        moneyIn += m.newLocked;

    // Apply attacks
    CharactersOnTiles attackedTiles;
    attackedTiles.ApplyAttacks (outState, stepData.vMoves);
    if (outState.ForkInEffect (FORK_LIFESTEAL))
      attackedTiles.DefendMutualAttacks (outState);
    attackedTiles.DrawLife (outState, stepResult);

    // Kill players who stay too long in the spawn area
    outState.KillSpawnArea (stepResult);

    /* Decrement poison life expectation and kill players when it
       has dropped to zero.  */
    outState.DecrementLife (stepResult);

    /* Finalise the kills.  */
    outState.FinaliseKills (stepResult);

    /* Special rule for the life-steal fork:  When it takes effect,
       remove all hearted characters from the map.  Also heart creation
       is disabled, so no hearted characters will ever be present
       afterwards.  */
    if (outState.param->rules->IsForkHeight (FORK_LIFESTEAL, outState.nHeight))
      outState.RemoveHeartedCharacters (stepResult);

    /* Apply updates to target coordinate.  This ignores already
       killed players.  */
    for (const auto& m : stepData.vMoves)
        if (!m.IsSpawn())
            m.ApplyWaypoints(outState);

    // For all alive players perform path-finding
    for (auto& p : outState.players)
      for (auto& pc : p.second.characters)
        {
            // can't move in spectator mode, moving will lose spawn protection
            if ((outState.ForkInEffect (FORK_TIMESAVE)) &&
                ( ! (pc.second.waypoints.empty()) ))
            {
                if (CharacterInSpectatorMode(pc.second.stay_in_spawn_area))
                    pc.second.StopMoving();
                else
                    pc.second.stay_in_spawn_area = CHARACTER_MODE_NORMAL;
            }
            pc.second.MoveTowardsWaypoint();
        }

    bool respawn_crown = false;
    outState.UpdateCrownState(respawn_crown);

    // Caution: banking must not depend on the randomized events, because they depend on the hash -
    // miners won't be able to compute tax amount if it depends on the hash.

    // Banking
    for (auto& p : outState.players)
      for (auto& pc : p.second.characters)
        {
            int i = pc.first;
            CharacterState &ch = pc.second;

            // player spawn tiles work like banks (for the purpose of banking)
            if (((ch.loot.nAmount > 0) && (outState.IsBank (ch.coord))) ||
                ((outState.ForkInEffect (FORK_TIMESAVE)) && (ch.loot.nAmount > 0) && (IsInsideMap(ch.coord.x, ch.coord.y)) && (SpawnMap[ch.coord.y][ch.coord.x] & SPAWNMAPFLAG_PLAYER)))
            {
                // Tax from banking: 10%
                CAmount nTax = ch.loot.nAmount / 10;
                stepResult.nTaxAmount += nTax;
                ch.loot.nAmount -= nTax;

                CollectedBounty b(p.first, i, ch.loot, p.second.address);
                stepResult.bounties.push_back (b);
                ch.loot = CollectedLootInfo();
            }
        }

    // Miners set hashBlock to 0 in order to compute tax and include it into the coinbase.
    // At this point the tax is fully computed, so we can return.
    if (outState.hashBlock.IsNull ())
        return true;

    RandomGenerator rnd(outState.hashBlock);

    /* Decide about whether or not this will be a disaster.  It should be
       the first action done with the RNG, so that it is possible to
       verify whether or not a block hash leads to a disaster
       relatively easily.  */
    const bool isDisaster = outState.CheckForDisaster (rnd);
    if (isDisaster)
      {
        LogPrint (BCLog::GAME, "Disaster happening at @%d.\n",
                  outState.nHeight);
        outState.ApplyDisaster (rnd);
        assert (outState.nHeight == outState.nDisasterHeight);
      }

    /* Transfer life from attacks.  This is done randomly, but the decision
       about who dies is non-random and already set above.  */
    if (outState.ForkInEffect (FORK_LIFESTEAL))
      attackedTiles.DistributeDrawnLife (rnd, outState);

    // Spawn new players
    for (const auto& m : stepData.vMoves)
        if (m.IsSpawn())
            m.ApplySpawn(outState, rnd);

    // Apply address & message updates
    for (const auto& m : stepData.vMoves)
        m.ApplyCommon(outState);

    /* In the (rare) case that a player collected a bounty, is still alive
       and changed the reward address at the same time, make sure that the
       bounty is paid to the new address to match the old network behaviour.  */
    for (auto& bounty : stepResult.bounties)
      bounty.UpdateAddress (outState);

    // Set colors for dead players, so their messages can be shown in the chat window
    for (auto& p : outState.dead_players_chat)
      {
        std::map<PlayerID, PlayerState>::const_iterator mi = inState.players.find(p.first);
        assert(mi != inState.players.end());
        const PlayerState &pl = mi->second;
        p.second.color = pl.color;
      }

    // Drop a random rewards onto the harvest areas
    const CAmount nCrownBonus
      = CROWN_BONUS * stepData.nTreasureAmount / TOTAL_HARVEST;
    CAmount nTotalTreasure = 0;
    for (int i = 0; i < NUM_HARVEST_AREAS; i++)
    {
        int a = rnd.GetIntRnd(HarvestAreaSizes[i]);
        Coord harvest(HarvestAreas[i][2 * a], HarvestAreas[i][2 * a + 1]);
        const CAmount nTreasure
          = HarvestPortions[i] * stepData.nTreasureAmount / TOTAL_HARVEST;
        outState.AddLoot(harvest, nTreasure);
        nTotalTreasure += nTreasure;
    }
    assert(nTotalTreasure + nCrownBonus == stepData.nTreasureAmount);

    // Players collect loot
    outState.DivideLootAmongPlayers();
    outState.CrownBonus(nCrownBonus);

    /* Update the banks.  */
    outState.UpdateBanks (rnd);

    /* Drop heart onto the map.  They are not dropped onto the original
       spawn area for historical reasons.  After the life-steal fork,
       we simply remove this check (there are no hearts anyway).  */
    if (DropHeart (outState))
    {
        assert (!outState.ForkInEffect (FORK_LIFESTEAL));

        Coord heart;
        do
        {
            heart.x = rnd.GetIntRnd(MAP_WIDTH);
            heart.y = rnd.GetIntRnd(MAP_HEIGHT);
        } while (!IsWalkableCoord (heart) || IsOriginalSpawnAreaCoord (heart));
        outState.hearts.insert(heart);
    }

    outState.CollectHearts(rnd);
    outState.CollectCrown(rnd, respawn_crown);

    /* Compute total money out of the game world via bounties paid.  */
    CAmount moneyOut = stepResult.nTaxAmount;
    for (const auto& b : stepResult.bounties)
      moneyOut += b.loot.nAmount;

    /* Compare total money before and after the step.  If there is a mismatch,
       we have a bug in the logic.  Better not accept the new game state.  */
    const CAmount moneyBefore = inState.GetCoinsOnMap () + inState.gameFund;
    const CAmount moneyAfter = outState.GetCoinsOnMap () + outState.gameFund;
    if (moneyBefore + stepData.nTreasureAmount + moneyIn
          != moneyAfter + moneyOut)
      {
        LogPrintf ("Old game state: %ld (@%d)\n", moneyBefore, inState.nHeight);
        LogPrintf ("New game state: %ld\n", moneyAfter);
        LogPrintf ("Money in:  %ld\n", moneyIn);
        LogPrintf ("Money out: %ld\n", moneyOut);
        LogPrintf ("Treasure placed: %ld\n", stepData.nTreasureAmount);
        return error ("total amount before and after step mismatch");
      }

    return true;
}
