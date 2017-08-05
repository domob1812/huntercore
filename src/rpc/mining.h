// Copyright (c) 2017 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_RPC_MINING_H
#define BITCOIN_RPC_MINING_H

#include "pow.h"
#include "script/script.h"

#include <string>

#include <univalue.h>

/** Generate blocks (mine) */
UniValue generateBlocks(std::shared_ptr<CReserveScript> coinbaseScript, int nGenerate, PowAlgo algo, uint64_t nMaxTries, bool keepScript);

/** Check bounds on a command line confirm target */
unsigned int ParseConfirmTarget(const UniValue& value);

/* Creation and submission of auxpow blocks.  */
UniValue AuxMiningCreateBlock(const CScript& scriptPubKey, PowAlgo algo);
bool AuxMiningSubmitBlock(const std::string& hashHex,
                          const std::string& auxpowHex);

#endif
