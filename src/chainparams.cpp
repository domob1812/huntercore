// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2017 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <chainparams.h>
#include <consensus/merkle.h>

#include <names/common.h>
#include <tinyformat.h>
#include <util.h>
#include <utilstrencodings.h>

#include <assert.h>

#include <chainparamsseeds.h>

bool CChainParams::IsHistoricBug(const uint256& txid, unsigned nHeight, BugType& type) const
{
    const std::pair<unsigned, uint256> key(nHeight, txid);
    std::map<std::pair<unsigned, uint256>, BugType>::const_iterator mi;

    mi = mapHistoricBugs.find (key);
    if (mi != mapHistoricBugs.end ())
    {
        type = mi->second;
        return true;
    }

    return false;
}

static CBlock CreateGenesisBlock(const char* pszTimestamp, const CScript& genesisOutputScript, uint32_t nTime, uint32_t nNonce, uint32_t nBits, int32_t nVersion, const CAmount& genesisReward)
{
    CMutableTransaction txNew;
    txNew.nVersion = 1;
    txNew.vin.resize(1);
    txNew.vout.resize(1);
    txNew.vin[0].scriptSig = CScript() << ValtypeFromString(std::string(pszTimestamp));
    txNew.vout[0].nValue = genesisReward;
    txNew.vout[0].scriptPubKey = genesisOutputScript;

    CBlock genesis;
    genesis.nTime    = nTime;
    genesis.nBits    = nBits;
    genesis.nNonce   = nNonce;
    genesis.nVersion = nVersion;
    genesis.vtx.push_back(MakeTransactionRef(std::move(txNew)));
    genesis.hashPrevBlock.SetNull();
    genesis.hashMerkleRoot = BlockMerkleRoot(genesis);
    return genesis;
}

/**
 * Build the genesis block.
 */
static CBlock CreateGenesisBlock(uint32_t nTime, uint32_t nNonce, uint32_t nBits, int32_t nVersion, const CAmount& genesisReward)
{
    const char *pszTimestamp =
            "\n"
            "Huntercoin genesis timestamp\n"
            "31/Jan/2014 20:10 GMT\n"
            "Bitcoin block 283440: 0000000000000001795d3c369b0746c0b5d315a6739a7410ada886de5d71ca86\n"
            "Litecoin block 506479: 77c49384e6e8dd322da0ebb32ca6c8f047d515d355e9f22b116430a888fffd38\n"
        ;
    const CScript genesisOutputScript = CScript() << OP_DUP << OP_HASH160 << ParseHex("fe2435b201d25290533bdaacdfe25dc7548b3058") << OP_EQUALVERIFY << OP_CHECKSIG;
    return CreateGenesisBlock(pszTimestamp, genesisOutputScript, nTime, nNonce, nBits, nVersion, genesisReward);
}

/**
 * Build genesis block for testnet.  In Huntercoin, it has a changed timestamp
 * and output script.
 */
static CBlock CreateTestnetGenesisBlock(uint32_t nTime, uint32_t nNonce, uint32_t nBits, int32_t nVersion, const CAmount& genesisReward)
{
    const char* pszTimestamp = "\nHuntercoin test net\n";
    const CScript genesisOutputScript = CScript() << OP_DUP << OP_HASH160 << ParseHex("7238d2df990b8e333ed28a84a8df8408f6dbcd57") << OP_EQUALVERIFY << OP_CHECKSIG;
    return CreateGenesisBlock(pszTimestamp, genesisOutputScript, nTime, nNonce, nBits, nVersion, genesisReward);
}

void CChainParams::UpdateVersionBitsParameters(Consensus::DeploymentPos d, int64_t nStartTime, int64_t nTimeout)
{
    consensus.vDeployments[d].nStartTime = nStartTime;
    consensus.vDeployments[d].nTimeout = nTimeout;
}

void CChainParams::TurnOffSegwitForUnitTests ()
{
  consensus.BIP16Height = 1000000000;
}

/**
 * Main network
 */
/**
 * What makes a good checkpoint block?
 * + Is surrounded by blocks with reasonable timestamps
 *   (no blocks before with a timestamp after, none after with
 *    timestamp before)
 * + Contains no strange transactions
 */

class CMainParams : public CChainParams {
public:
    CMainParams() {
        strNetworkID = "main";
        consensus.nSubsidyHalvingInterval = 2100000;
        /* FIXME: Set to activate the forks.  */
        consensus.BIP16Height = 1000000000;
        consensus.BIP34Height = 1000000000;
        consensus.BIP65Height = 1000000000;
        consensus.BIP66Height = 1000000000;
        consensus.powLimit[ALGO_SHA256D] = uint256S("00000000ffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
        consensus.powLimit[ALGO_SCRYPT] = uint256S("00000fffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
        consensus.nPowTargetSpacing = 60 * NUM_ALGOS;
        consensus.nPowTargetTimespan = consensus.nPowTargetSpacing * 2016;
        consensus.fPowNoRetargeting = false;
        consensus.nRuleChangeActivationThreshold = 1916; // 95% of 2016
        consensus.nMinerConfirmationWindow = 2016; // nPowTargetTimespan / nPowTargetSpacing
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].bit = 28;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nStartTime = 1199145601; // January 1, 2008
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nTimeout = 1230767999; // December 31, 2008

        // CSV (BIP68, BIP112 and BIP113) as well as segwit (BIP141, BIP143 and
        // BIP147) are deployed together with P2SH.

        // The best chain should have at least this much work.
        // The value is the chain work of the Huntercoin mainnet chain at height
        // 1,490,000, with best block hash:
        // d38feb2df0fc1b64bd3b3fe5b1e90d15a5d8ca17a13b735db381d16ce359393f
        consensus.nMinimumChainWork = uint256S("0x0000000000000000000000000000000000000000000326ede22d6f88e27b6e95");

        // By default assume that the signatures in ancestors of this block are valid.
        consensus.defaultAssumeValid = uint256S("0x4bf3a4e732a8ef2c8d93c996f9ffc9e8c9044ec687b13defb9b86cd33b7428e2"); //1500000

        consensus.nAuxpowChainId[ALGO_SHA256D] = 0x0006;
        consensus.nAuxpowChainId[ALGO_SCRYPT] = 0x0002;
        consensus.fStrictChainId = true;

        consensus.rules.reset(new Consensus::MainNetConsensus());

        /**
         * The message start string is designed to be unlikely to occur in normal data.
         * The characters are rarely used upper ASCII, not valid as UTF-8, and produce
         * a large 32-bit integer with any alignment.
         */
        pchMessageStart[0] = 0xf9;
        pchMessageStart[1] = 0xbe;
        pchMessageStart[2] = 0xb4;
        pchMessageStart[3] = 0xfe;
        nDefaultPort = 8398;
        nPruneAfterHeight = 100000;

        genesis = CreateGenesisBlock(1391199780, 1906435634u, 486604799, 1, 85000 * COIN);
        consensus.hashGenesisBlock = genesis.GetHash();
        assert(consensus.hashGenesisBlock == uint256S("0x00000000db7eb7a9e1a06cf995363dcdc4c28e8ae04827a961942657db9a1631"));
        assert(genesis.hashMerkleRoot == uint256S("0xc4ee946ffcb0bffa454782432d530bbeb8562b09594c1fbc8ceccd46ce34a754"));

        // Note that of those which support the service bits prefix, most only support a subset of
        // possible options.
        // This is fine at runtime as we'll fall back to using them as a oneshot if they don't support the
        // service bits we want, but we should get them updated to support all service bits wanted by any
        // release ASAP to avoid it where possible.
        /* FIXME: Add DNS seeds.  */
        //vSeeds.emplace_back("nmc.seed.quisquis.de");

        base58Prefixes[PUBKEY_ADDRESS] = std::vector<unsigned char>(1,40);
        base58Prefixes[SCRIPT_ADDRESS] = std::vector<unsigned char>(1,13); // FIXME: Update.
        base58Prefixes[SECRET_KEY] =     std::vector<unsigned char>(1,168);
        /* FIXME: Update these below.  */
        base58Prefixes[EXT_PUBLIC_KEY] = {0x04, 0x88, 0xB2, 0x1E};
        base58Prefixes[EXT_SECRET_KEY] = {0x04, 0x88, 0xAD, 0xE4};

        bech32_hrp = "hc";

        // FIXME: Set seeds for Huntercoin.
        //vFixedSeeds = std::vector<SeedSpec6>(pnSeed6_main, pnSeed6_main + ARRAYLEN(pnSeed6_main));

        fDefaultConsistencyChecks = false;
        fRequireStandard = true;
        fMineBlocksOnDemand = false;

        checkpointData = {
            {
                {0, uint256S("00000000db7eb7a9e1a06cf995363dcdc4c28e8ae04827a961942657db9a1631")},
            }
        };

        chainTxData = ChainTxData{
            0, // * UNIX timestamp of last checkpoint block
            0, // * total number of transactions between genesis and last checkpoint
               //   (the tx=... number in the SetBestChain debug.log lines)
            0  // * estimated number of transactions per day after checkpoint
        };

        /* disable fallback fee on mainnet */
        m_fallback_fee_enabled = false;
    }

    int DefaultCheckNameDB () const
    {
        return -1;
    }
};

/**
 * Testnet (v3)
 */
class CTestNetParams : public CChainParams {
public:
    CTestNetParams() {
        strNetworkID = "test";
        consensus.nSubsidyHalvingInterval = 2100000;
        /* FIXME: Set to activate the forks.  */
        consensus.BIP16Height = 1000000000;
        consensus.BIP34Height = 1000000000;
        consensus.BIP65Height = 1000000000;
        consensus.BIP66Height = 1000000000;
        consensus.powLimit[ALGO_SHA256D] = uint256S("000000ffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
        consensus.powLimit[ALGO_SCRYPT] = uint256S("000fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
        consensus.nPowTargetSpacing = 60 * NUM_ALGOS;
        consensus.nPowTargetTimespan = consensus.nPowTargetSpacing * 2016;
        consensus.fPowNoRetargeting = false;
        consensus.nRuleChangeActivationThreshold = 1512; // 75% for testchains
        consensus.nMinerConfirmationWindow = 2016; // nPowTargetTimespan / nPowTargetSpacing
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].bit = 28;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nStartTime = 1199145601; // January 1, 2008
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nTimeout = 1230767999; // December 31, 2008

        // CSV (BIP68, BIP112 and BIP113) as well as segwit (BIP141, BIP143 and
        // BIP147) are deployed together with P2SH.

        // The best chain should have at least this much work.
        // The value is the chain work of the Huntercoin testnet chain at height
        // 350,000, with best block hash:
        // 884920fb406847e9ebaac69305d97d6df9fa125603fd7d3e26c00a0d79c29ddc
        consensus.nMinimumChainWork = uint256S("0x00000000000000000000000000000000000000000000000000038998cea702f2");

        // By default assume that the signatures in ancestors of this block are valid.
        consensus.defaultAssumeValid = uint256S("0xdabe819758cfe24c960d335ed420f77bdaf7aa98e4beea51ea7c9f14448f6a3a"); //300000

        consensus.nAuxpowChainId[ALGO_SHA256D] = 0x0006;
        consensus.nAuxpowChainId[ALGO_SCRYPT] = 0x0002;
        consensus.fStrictChainId = false;

        consensus.rules.reset(new Consensus::TestNetConsensus());

        pchMessageStart[0] = 0xfa;
        pchMessageStart[1] = 0xbf;
        pchMessageStart[2] = 0xb5;
        pchMessageStart[3] = 0xfe;
        nDefaultPort = 18398;
        nPruneAfterHeight = 1000;

        genesis = CreateTestnetGenesisBlock(1391193136, 1997599826u, 503382015, 1, 100 * COIN);
        consensus.hashGenesisBlock = genesis.GetHash();
        assert(consensus.hashGenesisBlock == uint256S("000000492c361a01ce7558a3bfb198ea3ff2f86f8b0c2e00d26135c53f4acbf7"));
        assert(genesis.hashMerkleRoot == uint256S("28da665eada1b006bb9caf83e7541c6f995e0681debfc2540507bbfdf2d4ac84"));

        vFixedSeeds.clear();
        vSeeds.clear();
        /* FIXME: Testnet seeds?  */
        // nodes with support for servicebits filtering should be at the top
        //vSeeds.emplace_back("dnsseed.test.namecoin.webbtc.com");

        base58Prefixes[PUBKEY_ADDRESS] = std::vector<unsigned char>(1,100);
        base58Prefixes[SCRIPT_ADDRESS] = std::vector<unsigned char>(1,196); // FIXME: Update
        base58Prefixes[SECRET_KEY] =     std::vector<unsigned char>(1,228);
        /* FIXME: Update these below.  */
        base58Prefixes[EXT_PUBLIC_KEY] = {0x04, 0x35, 0x87, 0xCF};
        base58Prefixes[EXT_SECRET_KEY] = {0x04, 0x35, 0x83, 0x94};

        bech32_hrp = "th";

        // FIXME: Set seeds for Huntercoin.
        //vFixedSeeds = std::vector<SeedSpec6>(pnSeed6_test, pnSeed6_test + ARRAYLEN(pnSeed6_test));

        fDefaultConsistencyChecks = false;
        fRequireStandard = false;
        fMineBlocksOnDemand = false;


        checkpointData = {
            {
                {0, uint256S("000000492c361a01ce7558a3bfb198ea3ff2f86f8b0c2e00d26135c53f4acbf7")},
            }
        };

        chainTxData = ChainTxData{
            0,
            0,
            0
        };

        /* enable fallback fee on testnet */
        m_fallback_fee_enabled = true;

        assert(mapHistoricBugs.empty());
    }

    int DefaultCheckNameDB () const
    {
        return -1;
    }
};

/**
 * Regression test
 */
class CRegTestParams : public CChainParams {
public:
    CRegTestParams() {
        strNetworkID = "regtest";
        consensus.nSubsidyHalvingInterval = 150;
        consensus.BIP16Height = 432; // Corresponds to activation height using BIP9 rules
        consensus.BIP34Height = 100000000; // BIP34 has not activated on regtest (far in the future so block v1 are not rejected in tests)
        consensus.BIP65Height = 1351; // BIP65 activated on regtest (Used in rpc activation tests)
        consensus.BIP66Height = 1251; // BIP66 activated on regtest (Used in rpc activation tests)
        consensus.powLimit[ALGO_SHA256D] = uint256S("7fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
        consensus.powLimit[ALGO_SCRYPT] = uint256S("7fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
        consensus.nPowTargetSpacing = 60 * NUM_ALGOS;
        consensus.nPowTargetTimespan = consensus.nPowTargetSpacing * 2016;
        consensus.fPowNoRetargeting = true;
        consensus.nRuleChangeActivationThreshold = 108; // 75% for testchains
        consensus.nMinerConfirmationWindow = 144; // Faster than normal for regtest (144 instead of 2016)
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].bit = 28;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nStartTime = 0;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nTimeout = Consensus::BIP9Deployment::NO_TIMEOUT;

        // The best chain should have at least this much work.
        consensus.nMinimumChainWork = uint256S("0x00");

        // By default assume that the signatures in ancestors of this block are valid.
        consensus.defaultAssumeValid = uint256S("0x00");

        consensus.nAuxpowChainId[ALGO_SHA256D] = 0x0006;
        consensus.nAuxpowChainId[ALGO_SCRYPT] = 0x0002;
        consensus.fStrictChainId = true;

        consensus.rules.reset(new Consensus::RegTestConsensus());

        pchMessageStart[0] = 0xfa;
        pchMessageStart[1] = 0xbf;
        pchMessageStart[2] = 0xb5;
        pchMessageStart[3] = 0xda;
        nDefaultPort = 18498;
        nPruneAfterHeight = 1000;

        genesis = CreateTestnetGenesisBlock(1296688602, 2, 0x207fffff, 1, 50 * COIN);
        consensus.hashGenesisBlock = genesis.GetHash();
        assert(consensus.hashGenesisBlock == uint256S("0x3867dcd08712d9b49de33d4ab145d57ad14a78c7843c51f8c5d782d5f102fb4a"));
        assert(genesis.hashMerkleRoot == uint256S("0x71c88ed0560ee7d644deba07485c4eff571e3f86f9485692ed3966e4f0f3a59c"));

        vFixedSeeds.clear(); //!< Regtest mode doesn't have any fixed seeds.
        vSeeds.clear();      //!< Regtest mode doesn't have any DNS seeds.

        fDefaultConsistencyChecks = true;
        fRequireStandard = false;
        fMineBlocksOnDemand = true;

        checkpointData = {
            {
                {0, uint256S("3867dcd08712d9b49de33d4ab145d57ad14a78c7843c51f8c5d782d5f102fb4a")},
            }
        };

        chainTxData = ChainTxData{
            0,
            0,
            0
        };

        base58Prefixes[PUBKEY_ADDRESS] = std::vector<unsigned char>(1,100);
        base58Prefixes[SCRIPT_ADDRESS] = std::vector<unsigned char>(1,196); // FIXME: Update.
        base58Prefixes[SECRET_KEY] =     std::vector<unsigned char>(1,228);
        /* FIXME: Update below.  */
        base58Prefixes[EXT_PUBLIC_KEY] = {0x04, 0x35, 0x87, 0xCF};
        base58Prefixes[EXT_SECRET_KEY] = {0x04, 0x35, 0x83, 0x94};

        bech32_hrp = "hcrt";

        /* enable fallback fee on regtest */
        m_fallback_fee_enabled = true;

        assert(mapHistoricBugs.empty());
    }

    int DefaultCheckNameDB () const
    {
        return 0;
    }
};

static std::unique_ptr<CChainParams> globalChainParams;

const CChainParams &Params() {
    assert(globalChainParams);
    return *globalChainParams;
}

std::unique_ptr<CChainParams> CreateChainParams(const std::string& chain)
{
    if (chain == CBaseChainParams::MAIN)
        return std::unique_ptr<CChainParams>(new CMainParams());
    else if (chain == CBaseChainParams::TESTNET)
        return std::unique_ptr<CChainParams>(new CTestNetParams());
    else if (chain == CBaseChainParams::REGTEST)
        return std::unique_ptr<CChainParams>(new CRegTestParams());
    throw std::runtime_error(strprintf("%s: Unknown chain %s.", __func__, chain));
}

void SelectParams(const std::string& network)
{
    SelectBaseParams(network);
    globalChainParams = CreateChainParams(network);
}

void UpdateVersionBitsParameters(Consensus::DeploymentPos d, int64_t nStartTime, int64_t nTimeout)
{
    globalChainParams->UpdateVersionBitsParameters(d, nStartTime, nTimeout);
}

void TurnOffSegwitForUnitTests ()
{
  globalChainParams->TurnOffSegwitForUnitTests ();
}
