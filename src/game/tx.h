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

#ifndef GAME_TX_H
#define GAME_TX_H

#include <names/common.h>

#include <vector>

class CBlockUndo;
class CCoinsView;
class CCoinsViewCache;
class CScript;
class CTransaction;
class StepResult;

// Opcodes for scriptSig that acts as coinbase for game-generated transactions.
// They serve merely for information purposes, so the client can know why it got this transaction.
// In the future, for some really complex transactions, this data can be encoded in scriptPubKey
// followed by OP_DROPs.
enum
{

    // Syntax (scriptSig):
    //     victim GAMEOP_KILLED_BY killer1 killer2 ... killerN
    // Player can be killed simultaneously by multiple other players.
    // If N = 0, player was killed for staying too long in spawn area.
    GAMEOP_KILLED_BY = 1,

    // Syntax (scriptSig):
    //     player GAMEOP_COLLECTED_BOUNTY characterIndex firstBlock lastBlock collectedFirstBlock collectedLastBlock
    // vin.size() == vout.size(), they correspond to each other, i.e. a dummy input is used
    // to hold info about the corresponding output in its scriptSig
    // (alternatively we could add vout index to the scriptSig, to allow more complex transactions
    // with arbitrary input assignments, or store it in scriptPubKey of the tx-out instead)
    GAMEOP_COLLECTED_BOUNTY = 2,

    // Syntax (scriptSig):
    //     victim GAMEOP_KILLED_POISON
    // Player was killed due to poisoning
    GAMEOP_KILLED_POISON = 3,

    // Syntax (scriptSig):
    //     player GAMEOP_REFUND characterIndex height
    // This is a tx to refund a player's coins after staying long
    // in the spawn area.  characterIndex is usually 0, but keep it
    // here for future extensibility.
    GAMEOP_REFUND = 4,

};

/**
 * Construct the game transactions corresponding to the given step result.
 * The coins view is used to look up names and their coins / addresses.
 */
bool CreateGameTransactions (const CCoinsView& view, unsigned nHeight,
                             const StepResult& stepResult,
                             std::vector<CTransactionRef>& vGameTx);

/**
 * Apply game transactions to the coins view and name db.
 */
void ApplyGameTransactions (const std::vector<CTransactionRef>& vGameTx,
                            const StepResult& stepResult, unsigned nHeight,
                            CCoinsViewCache& view, CBlockUndo& undo);

/**
 * Find the name of the player involved in a scriptSig of a game tx.
 */
bool NameFromGameTransactionInput (const CScript& scriptSig, valtype& name);

#endif
