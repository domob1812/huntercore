#include <game/move.h>

#include <base58.h>
#include <consensus/validation.h>
#include <game/db.h>
#include <game/map.h>
#include <game/state.h>
#include <names/common.h>
#include <names/main.h>
#include <primitives/block.h>
#include <primitives/transaction.h>
#include <script/names.h>
#include <util.h>
#include <utilstrencodings.h>
#include <validation.h>

#include <boost/foreach.hpp>
#include <boost/xpressive/xpressive_dynamic.hpp>

/* Maximum number of waypoints per character.  */
static const int MAX_WAYPOINTS = 100;

/* Number of colours in the game.  */
static const int NUM_TEAM_COLORS = 4;

/* ************************************************************************** */
/* Move.  */

bool Move::IsValid(const GameState &state) const
{
  PlayerStateMap::const_iterator mi = state.players.find (player);

  CAmount oldLocked;
  if (mi == state.players.end ())
    {
      if (!IsSpawn ())
        return false;
      oldLocked = 0;
    }
  else
    {
      if (IsSpawn ())
        return false;
      oldLocked = mi->second.lockedCoins;
    }

  assert (oldLocked >= 0 && newLocked >= 0);
  const CAmount gameFee = newLocked - oldLocked;
  const CAmount required = MinimumGameFee (*state.param, state.nHeight + 1);
  assert (required >= 0);
  if (gameFee < required)
    return error ("%s: too little game fee attached, got %lld, required %lld",
                  __func__, gameFee, required);

  return true;
}

bool ParseWaypoints(UniValue& obj, std::vector<Coord>& result, bool& bWaypoints)
{
    bWaypoints = false;
    result.clear();
    UniValue v;
    if (!obj.extractField("wp", v))
        return true;
    if (!v.isArray())
        return false;
    if (v.size() % 2)
        return false;
    int n = v.size() / 2;
    if (n > MAX_WAYPOINTS)
        return false;
    result.resize(n);
    for (int i = 0; i < n; i++)
    {
        int x = v[2 * i].get_int();
        int y = v[2 * i + 1].get_int();
        if (!IsInsideMap(x, y))
            return false;
        // Waypoints are reversed for easier deletion of current waypoint from the end of the vector
        result[n - 1 - i] = Coord(x, y);
        if (i && result[n - 1 - i] == result[n - i])
            return false; // Forbid duplicates        
    }
    bWaypoints = true;
    return true;
}

bool ParseDestruct(UniValue& obj, bool& result)
{
    result = false;
    UniValue v;
    if (!obj.extractField("destruct", v))
        return true;
    result = v.get_bool();
    return true;
}

static bool
IsValidReceiveAddress (const std::string& str)
{
  /* TODO: Allow P2SH addresses at some point in the future.  For now,
     we have to disable support to stay consensus-compatible with
     the existing client.  */

  const CTxDestination dest = DecodeDestination (str);
  return IsKeyDestination (dest);
}

bool Move::Parse(const PlayerID &p, const std::string &json)
{
    try
    {

    if (!IsValidPlayerName(p))
        return false;
        
    UniValue obj;
    if (!obj.read(json, false) || !obj.isObject())
        return false;

    UniValue v;
    if (obj.extractField ("msg", v))
        message = v.get_str();
    if (obj.extractField("address", v))
    {
        const std::string &addr = v.get_str();
        if (!addr.empty() && !IsValidReceiveAddress(addr))
            return false;
        address = addr;
    }
    if (obj.extractField("addressLock", v))
    {
        const std::string &addr = v.get_str();
        if (!addr.empty() && !IsValidReceiveAddress(addr))
            return false;
        addressLock = addr;
    }

    if (obj.extractField("color", v))
    {
        color = v.get_int();
        if (color >= NUM_TEAM_COLORS)
            return false;
        if (!obj.empty()) // Extra fields are not allowed in JSON string
            return false;
        player = p;
        return true;
    }

    const std::vector<std::string> keys = obj.getKeys ();
    std::set<int> character_indices;
    for (const auto& key : keys)
    {
        const int i = atoi(key.c_str());
        if (i < 0 || strprintf("%d", i) != key)
            return false;               // Number formatting must be strict
        if (character_indices.count(i))
            return false;               // Cannot contain duplicate character indices
        character_indices.insert(i);
        v = obj[key];
        if (!v.isObject ())
            return false;
        bool bWaypoints = false;
        std::vector<Coord> wp;
        if (!ParseWaypoints(v, wp, bWaypoints))
            return false;
        bool bDestruct;
        if (!ParseDestruct(v, bDestruct))
            return false;

        if (bDestruct)
            destruct.insert(i);
        if (bWaypoints)
            waypoints.insert(std::make_pair(i, wp));

        if (!v.empty())      // Extra fields are not allowed in JSON string
            return false;
    }
        
    player = p;
    return true;

    } catch (const std::runtime_error& exc)
    {
        /* This happens when some JSON value has a wrong type.  */
        return false;
    }
}

void Move::ApplyCommon(GameState &state) const
{
    std::map<PlayerID, PlayerState>::iterator mi = state.players.find(player);

    if (mi == state.players.end())
    {
        if (message)
        {
            PlayerState &pl = state.dead_players_chat[player];
            pl.message = *message;
            pl.message_block = state.nHeight;
        }
        return;
    }

    PlayerState &pl = mi->second;
    if (message)
    {
        pl.message = *message;
        pl.message_block = state.nHeight;
    }
    if (address)
        pl.address = *address;
    if (addressLock)
        pl.addressLock = *addressLock;
}

std::string Move::AddressOperationPermission(const GameState &state) const
{
    if (!address && !addressLock)
        return std::string();      // No address operation requested - allow

    PlayerStateMap::const_iterator mi = state.players.find(player);
    if (mi == state.players.end())
        return std::string();      // Spawn move - allow any address operation

    return mi->second.addressLock;
}

void
Move::ApplySpawn (GameState &state, RandomGenerator &rnd) const
{
  assert (state.players.count (player) == 0);

  PlayerState pl;
  assert (pl.next_character_index == 0);
  pl.color = color;

  /* This is a fresh player and name.  Set its value to the height's
     name coin amount and put the remainder in the game fee.  This prevents
     people from "overpaying" on purpose in order to get beefed-up players.
     This rule, however, is only active after the life-steal fork.  Before
     that, overpaying did, indeed, allow to set the hunter value
     arbitrarily high.  */
  if (state.ForkInEffect (FORK_LIFESTEAL))
    {
      const CAmount coinAmount = GetNameCoinAmount (*state.param, state.nHeight);
      assert (pl.lockedCoins == 0 && pl.value == -1);
      assert (newLocked >= coinAmount);
      pl.value = coinAmount;
      pl.lockedCoins = newLocked;
      state.gameFund += newLocked - coinAmount;
    }
  else
    {
      pl.value = newLocked;
      pl.lockedCoins = newLocked;
    }

  const unsigned limit = state.GetNumInitialCharacters ();
  for (unsigned i = 0; i < limit; i++)
    pl.SpawnCharacter (state, rnd);

  state.players.insert (std::make_pair (player, pl));
}

void Move::ApplyWaypoints(GameState &state) const
{
    std::map<PlayerID, PlayerState>::iterator pl;
    pl = state.players.find (player);
    if (pl == state.players.end ())
      return;

    for (const auto& p : waypoints)
    {
        std::map<int, CharacterState>::iterator mi;
        mi = pl->second.characters.find(p.first);
        if (mi == pl->second.characters.end())
            continue;
        CharacterState &ch = mi->second;
        const std::vector<Coord> &wp = p.second;

        if (ch.waypoints.empty() || wp.empty() || ch.waypoints.back() != wp.back())
            ch.from = ch.coord;
        ch.waypoints = wp;
    }
}

CAmount
Move::MinimumGameFee (const Consensus::Params& param, unsigned nHeight) const
{
  if (IsSpawn ())
    {
      const CAmount coinAmount = GetNameCoinAmount (param, nHeight);

      // fee for new hunter is 1 HUC
      if (param.rules->ForkInEffect (FORK_TIMESAVE, nHeight))
        return coinAmount + COIN;

      if (param.rules->ForkInEffect (FORK_LIFESTEAL, nHeight))
        return coinAmount + 5 * COIN;

      return coinAmount;
    }

  // destruct fee is 1 HUC
  if (param.rules->ForkInEffect (FORK_TIMESAVE, nHeight))
    return COIN * destruct.size ();

  if (param.rules->ForkInEffect (FORK_LIFESTEAL, nHeight))
    return 20 * COIN * destruct.size ();

  return 0;
}

bool
Move::IsValidPlayerName (const std::string& player)
{
  if (player.size() > MAX_NAME_LENGTH)
    return false;

  // Check player name validity
  // Can contain letters, digits, underscore, hyphen and whitespace
  // Cannot contain double whitespaces or start/end with whitespace
  using namespace boost::xpressive;
  static sregex regex = sregex::compile("^([a-zA-Z0-9_-]+ )*[a-zA-Z0-9_-]+$");
  smatch match;
  return regex_search(player, match, regex);
}

/* ************************************************************************** */
/* StepData.  */

StepData::StepData (const GameState& s)
  : state(s), dup(), nTreasureAmount(-1), newHash(), vMoves()
{
  const CAmount nSubsidy = GetBlockSubsidy (state.nHeight + 1, *state.param);
  // Miner subsidy is 10%, thus game treasure is 9 times the subsidy
  nTreasureAmount = nSubsidy * 9;
}

bool
StepData::addTransaction (const CTransaction& tx, const CCoinsView* pview,
                          CValidationState& res)
{
  if (!tx.IsNamecoin ())
    return true;

  /* Keep the moves to add to the step data here first.  This is necessary
     to prevent a situation where some moves are added already but the
     function fails later with an error.  */
  std::vector<Move> newMoves;

  for (const auto& txo : tx.vout)
    {
      const CNameScript nameOp(txo.scriptPubKey);
      if (!nameOp.isNameOp () || !nameOp.isAnyUpdate ())
        continue;

      const std::string strName = ValtypeToString (nameOp.getOpName ());
      const std::string strValue = ValtypeToString (nameOp.getOpValue ());

      if (dup.count (strName))
        return res.Invalid (error ("%s: duplicate name '%s' in block",
                                   __func__, strName.c_str ()));
      dup.insert (strName);

      Move m;
      m.newLocked = txo.nValue;

      if (!m.Parse (strName, strValue))
        return res.Invalid (error ("%s: cannot parse move %s",
                                   __func__, strValue.c_str ()));
      if (!m.IsValid (state))
        return res.Invalid (error ("%s: invalid move for player %s",
                                   __func__, strName.c_str ()));

      if (m.IsSpawn ())
        {
          if (nameOp.getNameOp () != OP_NAME_FIRSTUPDATE)
            return res.Invalid (error ("%s: spawn is not firstupdate",
                                       __func__));
        }
      else if (nameOp.getNameOp () != OP_NAME_UPDATE)
        return res.Invalid (error ("%s: firstupdate is not spawn"));

      const std::string addressLock = m.AddressOperationPermission (state);
      if (pview && !addressLock.empty ())
        {
          /* If one of inputs has address equal to addressLock, then that input
             has been signed by the address owner and thus authorizes the
             address change operation.  */
          bool found = false;
          for (const auto& txi : tx.vin)
            {
              const COutPoint prevout = txi.prevout;
              Coin coin;

              if (!pview->GetCoin (prevout, coin))
                continue;

              CTxDestination dest;
              if (ExtractDestination (coin.out.scriptPubKey, dest)
                    && EncodeDestination (dest) == addressLock)
                {
                  found = true;
                  break;
                }
            }
          if (!found)
            return res.Invalid (error ("%s: address operation denied",
                                       __func__));
        }

      newMoves.push_back (m);
    }

  vMoves.insert (vMoves.end (), newMoves.begin (), newMoves.end ());
  return true;
}

/* ************************************************************************** */

bool
PerformStep (const CBlock& block, const GameState& stateIn,
             const CCoinsView* pview, CValidationState& valid,
             StepResult& res, GameState& stateOut)
{
  StepData step(stateIn);
  for (const auto& tx : block.vtx)
    if (!step.addTransaction (*tx, pview, valid))
      return error ("%s: tx %s not accepted",
                    __func__, tx->GetHash ().GetHex ().c_str());
  step.newHash = block.GetHash ();

  if (!PerformStep (stateIn, step, stateOut, res))
    return error ("%s: game engine failed to perform step", __func__);

  return true;
}
