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

#include "chainparams.h"
#include "game/common.h"
#include "game/db.h"
#include "game/state.h"
#include "main.h"
#include "rpcserver.h"
#include "uint256.h"

#include <univalue.h>

UniValue
game_getplayerstate (const UniValue& params, bool fHelp)
{
  if (fHelp || params.size () < 1 || params.size () > 2)
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
    if (params.size () >= 2)
      hash = uint256S (params[1].get_str ());
    else
      hash = *chainActive.Tip ()->phashBlock;

    if (mapBlockIndex.count (hash) == 0)
      throw JSONRPCError (RPC_INVALID_ADDRESS_OR_KEY, "Block not found");
  }

  GameState state(Params ().GetConsensus ());
  if (!pgameDb->get (hash, state))
    throw JSONRPCError (RPC_DATABASE_ERROR, "Failed to fetch game state");

  const PlayerID name = params[0].get_str ();
  PlayerStateMap::const_iterator mi = state.players.find (name);
  if (mi == state.players.end ())
    throw JSONRPCError (RPC_INVALID_ADDRESS_OR_KEY, "No such player");

  int crownIndex = -1;
  if (name == state.crownHolder.player)
    crownIndex = state.crownHolder.index;

  return mi->second.ToJsonValue (crownIndex);
}

UniValue
game_getstate (const UniValue& params, bool fHelp)
{
  if (fHelp || params.size () > 1)
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
    if (params.size () >= 1)
      hash = uint256S (params[0].get_str ());
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
