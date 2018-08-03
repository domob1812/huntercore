// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <primitives/pureheader.h>

#include <hash.h>
#include <scrypt/scrypt.h>
#include <utilstrencodings.h>

uint256 CPureBlockHeader::GetHash() const
{
    return SerializeHash(*this);
}

void CPureBlockHeader::SetBaseVersion(int32_t nBaseVersion, int32_t nChainId)
{
    assert(nBaseVersion >= 1 && nBaseVersion < VERSION_AUXPOW);
    assert(!IsAuxpow());
    const PowAlgo algo = GetAlgo ();
    nVersion = nBaseVersion | (nChainId * VERSION_CHAIN_START);
    SetAlgo (algo);
}

uint256 CPureBlockHeader::GetPowHash(PowAlgo algo) const
{
    /* Note: We use explicitly provided algo instead of the one returned by
       GetAlgo(), because this can be a block from foreign chain (parent block
       in merged mining) which does not encode algo in its nVersion field.  */

    switch (algo)
    {
        case ALGO_SHA256D:
            return GetHash();

        case ALGO_SCRYPT:
        {
            uint256 thash;
            // Caution: scrypt_1024_1_1_256 assumes fixed length of 80 bytes
            scrypt_1024_1_1_256(BEGIN(nVersion), BEGIN(thash));
            return thash;
        }

        default:
            assert (false);
    }
}
