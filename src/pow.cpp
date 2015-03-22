// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "pow.h"

#include "arith_uint256.h"
#include "chain.h"
#include "primitives/block.h"
#include "uint256.h"
#include "util.h"

static const CBlockIndex*
GetLastBlockIndex(const CBlockIndex* pindex, PowAlgo algo)
{
    if (!pindex)
        return NULL;

    while (pindex && (pindex->nVersion.GetAlgo() != algo))
        pindex = pindex->pprev;

    return pindex;
}

unsigned int GetNextWorkRequired(const CBlockIndex* pindexLast, const CBlockHeader *pblock, const Consensus::Params& params)
{
    const PowAlgo algo = pblock->nVersion.GetAlgo();
    const arith_uint256 bnProofOfWorkLimit = UintToArith256(params.powLimit[algo]);
    const unsigned nProofOfWorkLimit = bnProofOfWorkLimit.GetCompact();

    if (!pindexLast)
        return nProofOfWorkLimit;
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
        return error("CheckProofOfWork(): nBits below minimum work");

    // Check proof of work matches claimed amount
    if (UintToArith256(hash) > bnTarget)
        return error("CheckProofOfWork(): hash doesn't match nBits");

    return true;
}

arith_uint256 GetBlockProof(const CBlockIndex& block)
{
    /* FIXME: Take dual-algo factor into account.  */

    arith_uint256 bnTarget;
    bool fNegative;
    bool fOverflow;
    bnTarget.SetCompact(block.nBits, &fNegative, &fOverflow);
    if (fNegative || fOverflow || bnTarget == 0)
        return 0;
    // We need to compute 2**256 / (bnTarget+1), but we can't represent 2**256
    // as it's too large for a arith_uint256. However, as 2**256 is at least as large
    // as bnTarget+1, it is equal to ((2**256 - bnTarget - 1) / (bnTarget+1)) + 1,
    // or ~bnTarget / (nTarget+1) + 1.
    arith_uint256 work = (~bnTarget / (bnTarget + 1)) + 1;

    // Apply scrypt-to-SHA ratio
    // We assume that scrypt is 2^12 times harder to mine (for the same difficulty target)
    // This only affects how a longer chain is selected in case of conflict
    switch (block.nVersion.GetAlgo())
    {
        case ALGO_SHA256D:
            break;
        case ALGO_SCRYPT:
            work <<= 12;
            break;
        default:
            assert (false);
    }

    return work;
}

int64_t GetBlockProofEquivalentTime(const CBlockIndex& to, const CBlockIndex& from, const CBlockIndex& tip, const Consensus::Params& params)
{
    arith_uint256 r;
    int sign = 1;
    if (to.nChainWork > from.nChainWork) {
        r = to.nChainWork - from.nChainWork;
    } else {
        r = from.nChainWork - to.nChainWork;
        sign = -1;
    }
    r = r * arith_uint256(params.nPowTargetSpacing) / GetBlockProof(tip);
    if (r.bits() > 63) {
        return sign * std::numeric_limits<int64_t>::max();
    }
    return sign * r.GetLow64();
}
