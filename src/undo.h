// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_UNDO_H
#define BITCOIN_UNDO_H

#include "compressor.h" 
#include "names/main.h"
#include "primitives/transaction.h"
#include "serialize.h"

/** Undo information for a CTxIn
 *
 *  Contains the prevout's CTxOut being spent, and if this was the
 *  last output of the affected transaction, its metadata as well
 *  (coinbase or not, height, transaction version)
 */
class CTxInUndo
{
public:
    CTxOut txout;         // the txout data before being spent
    bool fCoinBase;       // if the outpoint was the last unspent: whether it belonged to a coinbase
    bool fGameTx;         // if the outpoint was the last unspent: whether it belonged to a game tx
    unsigned int nHeight; // if the outpoint was the last unspent: its height
    int nVersion;         // if the outpoint was the last unspent: its version

    CTxInUndo()
      : txout(), fCoinBase(false), fGameTx(false), nHeight(0), nVersion(0) {}
    CTxInUndo(const CTxOut &txoutIn, bool fCoinBaseIn = false,
              bool fGameTxIn = false, unsigned int nHeightIn = 0,
              int nVersionIn = 0)
      : txout(txoutIn), fCoinBase(fCoinBaseIn), fGameTx(fGameTxIn),
        nHeight(nHeightIn), nVersion(nVersionIn) { }

    template<typename Stream>
    void Serialize(Stream &s) const {
        ::Serialize(s, VARINT(nHeight*4+(fCoinBase ? 1 : 0)+(fGameTx ? 2 : 0)));
        if (nHeight > 0)
            ::Serialize(s, VARINT(this->nVersion));
        ::Serialize(s, CTxOutCompressor(REF(txout)));
    }

    template<typename Stream>
    void Unserialize(Stream &s) {
        unsigned int nCode = 0;
        ::Unserialize(s, VARINT(nCode));
        nHeight = nCode / 4;
        fCoinBase = nCode & 1;
        fGameTx = nCode & 2;
        if (nHeight > 0)
            ::Unserialize(s, VARINT(this->nVersion));
        ::Unserialize(s, REF(CTxOutCompressor(REF(txout))));
    }
};

/** Undo information for a CTransaction */
class CTxUndo
{
public:
    // undo information for all txins
    std::vector<CTxInUndo> vprevout;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(vprevout);
    }
};

/** Undo information for a CBlock */
class CBlockUndo
{
public:
    std::vector<CTxUndo> vtxundo; // for all but the coinbase

    /** Stack of operations done to the name database.  */
    std::vector<CNameTxUndo> vnameundo;
    /** Undo information for expired name coins.  */
    std::vector<CTxInUndo> vexpired;

    /**
     * Store game transactions.  They are not on disk anywhere else and
     * required to perform the undo.  Their position on disk in the undo
     * file is also used for looking up game tx.
     */
    std::vector<CTransactionRef> vgametx;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {

        /* Store the vgametx first.  This allows us to compute the appropriate
           offsets most easily.  */
        READWRITE(vgametx);

        READWRITE(vtxundo);
        READWRITE(vnameundo);
        READWRITE(vexpired);
    }
};

#endif // BITCOIN_UNDO_H
