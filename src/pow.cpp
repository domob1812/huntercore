// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2017 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <pow.h>

#include <arith_uint256.h>
#include <chain.h>
#include <primitives/block.h>
#include <uint256.h>

static const CBlockIndex*
GetLastBlockIndex(const CBlockIndex* pindex, PowAlgo algo)
{
    assert(pindex != nullptr);

    while (pindex && (pindex->GetAlgo() != algo))
        pindex = pindex->pprev;

    return pindex;
}

unsigned int GetNextWorkRequired(const CBlockIndex* pindexLast, const CBlockHeader *pblock, const Consensus::Params& params)
{
    const PowAlgo algo = pblock->GetAlgo();
    const arith_uint256 bnProofOfWorkLimit = UintToArith256(params.powLimit[algo]);
    const unsigned nProofOfWorkLimit = bnProofOfWorkLimit.GetCompact();

    assert(pindexLast != nullptr);
    if (params.fPowNoRetargeting)
        return pindexLast->nBits;

    const CBlockIndex* pindexPrev = GetLastBlockIndex(pindexLast, algo);
    if (!pindexPrev || !pindexPrev->pprev)
        return nProofOfWorkLimit; // first block
    const CBlockIndex* pindexPrevPrev = GetLastBlockIndex(pindexPrev->pprev, algo);
    if (!pindexPrevPrev || !pindexPrevPrev->pprev)
        return nProofOfWorkLimit; // second block

    const int64_t nActualSpacing = pindexPrev->GetBlockTime() - pindexPrevPrev->GetBlockTime();

    // ppcoin: target change every block
    // ppcoin: retarget with exponential moving toward target spacing
    arith_uint256 bnNew;
    bnNew.SetCompact(pindexPrev->nBits);

    /* The old computation here was:
      
          bnNew *= (nInterval - 1) * nTargetSpacing + 2 * nActualSpacing;
          bnNew /= (nInterval + 1) * nTargetSpacing;

       This, however, may exceed 256 bits for a low difficulty in the
       intermediate step.  We can rewrite it:

          A = (nInterval + 1) nTargetSpacing
          bnNew *= A + 2 (nActualSpacing - nTargetSpacing)
          bnNew /= A

       Or also:

          A = (nInterval + 1) nTargetSpacing
          B = (nActualSpacing - nTargetSpacing)
          bnNew = (bnNew A + bnNew 2 B) / A = bnNew + (2 bnNew B) / A

      To compute (2 bnNew B) / A without overflowing, let

          bnNew = P * A + R.

      Then

          (2 bnNew B) / A = 2 P B + (2 R B) / A.

      Assuming that A is not too large (which it definitely isn't in comparison
      to 256 bits), also (2 R B) does not overflow before the divide.

    */

    const int64_t nInterval = params.DifficultyAdjustmentInterval();

    const int64_t a = (nInterval + 1) * params.nPowTargetSpacing;
    const int64_t b = nActualSpacing - params.nPowTargetSpacing;
    const arith_uint256 p = bnNew / a;
    const arith_uint256 r = bnNew - p * a;

    /* Make sure to get the division right for negative b!  Division is
       not "preserved" under two's complement.  */
    if (b >= 0)
        bnNew += 2 * p * b + (2 * r * b) / a;
    else
        bnNew -= 2 * p * (-b) + (2 * r * (-b)) / a;

    if (bnNew > bnProofOfWorkLimit)
        bnNew = bnProofOfWorkLimit;

    return bnNew.GetCompact();
}

bool CheckProofOfWork(uint256 hash, unsigned int nBits, PowAlgo algo, const Consensus::Params& params)
{
    bool fNegative;
    bool fOverflow;
    arith_uint256 bnTarget;

    bnTarget.SetCompact(nBits, &fNegative, &fOverflow);

    // Check range
    if (fNegative || bnTarget == 0 || fOverflow || bnTarget > UintToArith256(params.powLimit[algo]))
        return false;

    // Check proof of work matches claimed amount
    if (UintToArith256(hash) > bnTarget)
        return false;

    return true;
}
