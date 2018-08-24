// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2017 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_CONSENSUS_PARAMS_H
#define BITCOIN_CONSENSUS_PARAMS_H

#include <amount.h>
#include <uint256.h>
#include <limits>
#include <map>
#include <string>

#include <memory>

/* Handle fork heights.  The function checks whether a fork is in effect
   at the given height -- and may use different heights for testnet
   and mainnet, or for a "testing mode".  */
enum Fork
{

  /* Poison disaster, increased general cost 1 HUC -> 10 HUC, just general
     as initial character.  */
  FORK_POISON,

  /* Maximum carrying-capacity introduced, removed spawn death,
     new-style name registration, stricter rule checks for transaction
     version and auxpow (in parallel to Namecoin).  */
  FORK_CARRYINGCAP,

  /* Update parameters (general 10 HUC -> 200 HUC, carrying capacity increased
     to 2000 HUC, heart spawn rate reduced to 1/500, general explosion
     radius only 1).  */
  FORK_LESSHEARTS,

  /* Implement "life steal".  This adds a game fee for destructs (5 HUC),
     completely disables hearts and removes all "hearted" hunters.  It also
     randomises spawn and banking locations.  */
  FORK_LIFESTEAL,

    /* "timesave"  This makes hunters and banks spawn always near harvest areas.
       It also adds protection for newly spawned hunters and a spectator mode.
       Fee for a new hunter and destruct fee is set to 1 HUC.
       The refundable fee per hunter is set to 100 HUC   */
  FORK_TIMESAVE,

};

/** Dual-algo PoW algorithms.  */
enum PowAlgo
{
  ALGO_SHA256D = 0,
  ALGO_SCRYPT,
  NUM_ALGOS
};

namespace Consensus {

/**
 * Interface for classes that define consensus behaviour in more
 * complex ways than just by a set of constants.
 */
class ConsensusRules
{
public:

    /* Check whether a given fork is in effect at the height.  */
    virtual bool ForkInEffect(Fork type, unsigned nHeight) const = 0;

    /* Check whether the height is *exactly* when the fork starts to take
       effect.  This is used sometimes to trigger special events.  */
    inline bool
    IsForkHeight (Fork type, unsigned nHeight) const
    {
      if (nHeight == 0)
        return false;

      return ForkInEffect (type, nHeight) && !ForkInEffect (type, nHeight - 1);
    }

    /* Return whether this is regtest mode, for which we change the
       game rules in order to allow easier testing.  */
    virtual bool TestingRules() const = 0;

};

class MainNetConsensus : public ConsensusRules
{
public:

    bool ForkInEffect(Fork type, unsigned nHeight) const
    {
        switch (type)
        {
            case FORK_POISON:
                return nHeight >= 255000;
            case FORK_CARRYINGCAP:
                return nHeight >= 500000;
            case FORK_LESSHEARTS:
                return nHeight >= 590000;
            case FORK_LIFESTEAL:
                return nHeight >= 795000;
            case FORK_TIMESAVE:
                return nHeight >= 1521500;
            default:
                assert (false);
        }
    }

    bool TestingRules() const
    {
        return false;
    }

};

class TestNetConsensus : public MainNetConsensus
{
public:

    bool ForkInEffect(Fork type, unsigned nHeight) const
    {
        switch (type)
        {
            case FORK_POISON:
                return nHeight >= 190000;
            case FORK_CARRYINGCAP:
                return nHeight >= 200000;
            case FORK_LESSHEARTS:
                return nHeight >= 240000;
            case FORK_LIFESTEAL:
                return nHeight >= 301000;
            case FORK_TIMESAVE:
                return nHeight >= 331500;
            default:
                assert (false);
        }
    }

};

class RegTestConsensus : public TestNetConsensus
{
public:

    bool TestingRules() const
    {
        return true;
    }

};

enum DeploymentPos
{
    DEPLOYMENT_TESTDUMMY,
    // NOTE: Also add new deployments to VersionBitsDeploymentInfo in versionbits.cpp
    MAX_VERSION_BITS_DEPLOYMENTS
};

/**
 * Struct for each individual consensus rule change using BIP9.
 */
struct BIP9Deployment {
    /** Bit position to select the particular bit in nVersion. */
    int bit;
    /** Start MedianTime for version bits miner confirmation. Can be a date in the past */
    int64_t nStartTime;
    /** Timeout/expiry MedianTime for the deployment attempt. */
    int64_t nTimeout;

    /** Constant for nTimeout very far in the future. */
    static constexpr int64_t NO_TIMEOUT = std::numeric_limits<int64_t>::max();

    /** Special value for nStartTime indicating that the deployment is always active.
     *  This is useful for testing, as it means tests don't need to deal with the activation
     *  process (which takes at least 3 BIP9 intervals). Only tests that specifically test the
     *  behaviour during activation cannot use this. */
    static constexpr int64_t ALWAYS_ACTIVE = -1;
};

/**
 * Parameters that influence chain consensus.
 */
struct Params {
    uint256 hashGenesisBlock;
    int nSubsidyHalvingInterval;
    /** Block height at with BIP16 becomes active */
    int BIP16Height;
    /** Block height at which BIP34 becomes active */
    int BIP34Height;
    /** Block height at which BIP65 becomes active */
    int BIP65Height;
    /** Block height at which BIP66 becomes active */
    int BIP66Height;
    /**
     * Minimum blocks including miner confirmation of the total of 2016 blocks in a retargeting period,
     * (nPowTargetTimespan / nPowTargetSpacing) which is also used for BIP9 deployments.
     * Examples: 1916 for 95%, 1512 for testchains.
     */
    uint32_t nRuleChangeActivationThreshold;
    uint32_t nMinerConfirmationWindow;
    BIP9Deployment vDeployments[MAX_VERSION_BITS_DEPLOYMENTS];
    /** Proof of work parameters */
    uint256 powLimit[NUM_ALGOS];
    bool fPowNoRetargeting;
    int64_t nPowTargetSpacing;
    int64_t nPowTargetTimespan;
    int64_t DifficultyAdjustmentInterval() const { return nPowTargetTimespan / nPowTargetSpacing; }
    uint256 nMinimumChainWork;
    uint256 defaultAssumeValid;

    /** Auxpow parameters */
    int32_t nAuxpowChainId[NUM_ALGOS];
    bool fStrictChainId;

    /** Consensus rule interface.  */
    std::unique_ptr<ConsensusRules> rules;

    /**
     * Check whether or not to allow legacy blocks at the given height.
     * @param nHeight Height of the block to check.
     * @return True if it is allowed to have a legacy version.
     */
    bool AllowLegacyBlocks(unsigned nHeight) const
    {
        return nHeight == 0;
    }
};
} // namespace Consensus

#endif // BITCOIN_CONSENSUS_PARAMS_H
