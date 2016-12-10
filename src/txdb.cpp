// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2015 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "txdb.h"

#include "chainparams.h"
#include "game/db.h"
#include "game/state.h"
#include "hash.h"
#include "pow.h"
#include "uint256.h"
#include "validation.h"

#include "script/names.h"

#include <stdint.h>

#include <boost/thread.hpp>

using namespace std;

static const char DB_COINS = 'c';
static const char DB_BLOCK_FILES = 'f';
static const char DB_TXINDEX = 't';
static const char DB_BLOCK_INDEX = 'b';

static const char DB_NAME = 'n';
static const char DB_NAME_HISTORY = 'h';

static const char DB_BEST_BLOCK = 'B';
static const char DB_FLAG = 'F';
static const char DB_REINDEX_FLAG = 'R';
static const char DB_LAST_BLOCK = 'l';


CCoinsViewDB::CCoinsViewDB(size_t nCacheSize, bool fMemory, bool fWipe) : db(GetDataDir() / "chainstate", nCacheSize, fMemory, fWipe, true) 
{
}

bool CCoinsViewDB::GetCoins(const uint256 &txid, CCoins &coins) const {
    return db.Read(make_pair(DB_COINS, txid), coins);
}

bool CCoinsViewDB::HaveCoins(const uint256 &txid) const {
    return db.Exists(make_pair(DB_COINS, txid));
}

uint256 CCoinsViewDB::GetBestBlock() const {
    uint256 hashBestChain;
    if (!db.Read(DB_BEST_BLOCK, hashBestChain))
        return uint256();
    return hashBestChain;
}

bool CCoinsViewDB::GetName(const valtype &name, CNameData& data) const {
    return db.Read(std::make_pair(DB_NAME, name), data);
}

bool CCoinsViewDB::GetNameHistory(const valtype &name, CNameHistory& data) const {
    assert (fNameHistory);
    return db.Read(std::make_pair(DB_NAME_HISTORY, name), data);
}

class CDbNameIterator : public CNameIterator
{

private:

    /* The backing LevelDB iterator.  */
    CDBIterator* iter;

public:

    ~CDbNameIterator();

    /**
     * Construct a new name iterator for the database.
     * @param db The database to create the iterator for.
     */
    CDbNameIterator(const CDBWrapper& db);

    /* Implement iterator methods.  */
    void seek (const valtype& start);
    bool next (valtype& name, CNameData& data);

};

CDbNameIterator::~CDbNameIterator() {
    delete iter;
}

CDbNameIterator::CDbNameIterator(const CDBWrapper& db)
    : iter(const_cast<CDBWrapper*>(&db)->NewIterator())
{
    seek(valtype());
}

void CDbNameIterator::seek(const valtype& start) {
    iter->Seek(std::make_pair(DB_NAME, start));
}

bool CDbNameIterator::next(valtype& name, CNameData& data) {
    if (!iter->Valid())
        return false;

    std::pair<char, valtype> key;
    if (!iter->GetKey(key) || key.first != DB_NAME)
        return false;
    name = key.second;

    if (!iter->GetValue(data))
        return error("%s : failed to read data from iterator", __func__);

    iter->Next ();
    return true;
}

CNameIterator* CCoinsViewDB::IterateNames() const {
    return new CDbNameIterator(db);
}

bool CCoinsViewDB::BatchWrite(CCoinsMap &mapCoins, const uint256 &hashBlock, const CNameCache &names) {
    CDBBatch batch(db);
    size_t count = 0;
    size_t changed = 0;
    for (CCoinsMap::iterator it = mapCoins.begin(); it != mapCoins.end();) {
        if (it->second.flags & CCoinsCacheEntry::DIRTY) {
            if (it->second.coins.IsPruned())
                batch.Erase(make_pair(DB_COINS, it->first));
            else
                batch.Write(make_pair(DB_COINS, it->first), it->second.coins);
            changed++;
        }
        count++;
        CCoinsMap::iterator itOld = it++;
        mapCoins.erase(itOld);
    }
    if (!hashBlock.IsNull())
        batch.Write(DB_BEST_BLOCK, hashBlock);

    names.writeBatch(batch);

    LogPrint("coindb", "Committing %u changed transactions (out of %u) to coin database...\n", (unsigned int)changed, (unsigned int)count);
    return db.WriteBatch(batch);
}

CBlockTreeDB::CBlockTreeDB(size_t nCacheSize, bool fMemory, bool fWipe) : CDBWrapper(GetDataDir() / "blocks" / "index", nCacheSize, fMemory, fWipe) {
}

bool CBlockTreeDB::ReadBlockFileInfo(int nFile, CBlockFileInfo &info) {
    return Read(make_pair(DB_BLOCK_FILES, nFile), info);
}

bool CBlockTreeDB::WriteReindexing(bool fReindexing) {
    if (fReindexing)
        return Write(DB_REINDEX_FLAG, '1');
    else
        return Erase(DB_REINDEX_FLAG);
}

bool CBlockTreeDB::ReadReindexing(bool &fReindexing) {
    fReindexing = Exists(DB_REINDEX_FLAG);
    return true;
}

bool CBlockTreeDB::ReadLastBlockFile(int &nFile) {
    return Read(DB_LAST_BLOCK, nFile);
}

CCoinsViewCursor *CCoinsViewDB::Cursor() const
{
    CCoinsViewDBCursor *i = new CCoinsViewDBCursor(const_cast<CDBWrapper*>(&db)->NewIterator(), GetBestBlock());
    /* It seems that there are no "const iterators" for LevelDB.  Since we
       only need read operations on it, use a const-cast to get around
       that restriction.  */
    i->pcursor->Seek(DB_COINS);
    // Cache key of first record
    i->pcursor->GetKey(i->keyTmp);
    return i;
}

bool CCoinsViewDBCursor::GetKey(uint256 &key) const
{
    // Return cached key
    if (keyTmp.first == DB_COINS) {
        key = keyTmp.second;
        return true;
    }
    return false;
}

bool CCoinsViewDBCursor::GetValue(CCoins &coins) const
{
    return pcursor->GetValue(coins);
}

unsigned int CCoinsViewDBCursor::GetValueSize() const
{
    return pcursor->GetValueSize();
}

bool CCoinsViewDBCursor::Valid() const
{
    return keyTmp.first == DB_COINS;
}

void CCoinsViewDBCursor::Next()
{
    pcursor->Next();
    if (!pcursor->Valid() || !pcursor->GetKey(keyTmp))
        keyTmp.first = 0; // Invalidate cached key after last record so that Valid() and GetKey() return false
}

bool CBlockTreeDB::WriteBatchSync(const std::vector<std::pair<int, const CBlockFileInfo*> >& fileInfo, int nLastFile, const std::vector<const CBlockIndex*>& blockinfo) {
    CDBBatch batch(*this);
    for (std::vector<std::pair<int, const CBlockFileInfo*> >::const_iterator it=fileInfo.begin(); it != fileInfo.end(); it++) {
        batch.Write(make_pair(DB_BLOCK_FILES, it->first), *it->second);
    }
    batch.Write(DB_LAST_BLOCK, nLastFile);
    for (std::vector<const CBlockIndex*>::const_iterator it=blockinfo.begin(); it != blockinfo.end(); it++) {
        batch.Write(make_pair(DB_BLOCK_INDEX, (*it)->GetBlockHash()), CDiskBlockIndex(*it));
    }
    return WriteBatch(batch, true);
}

bool CCoinsViewDB::ValidateNameDB(CGameDB& gameDb) const
{
    /* Skip for genesis block, since there is no game state available yet
       (test would fail below).  There's not really anything to verify
       for the genesis block anyway.  */
    const uint256 blockHash = GetBestBlock();
    if (blockHash.IsNull())
        return true;

    /* It seems that there are no "const iterators" for LevelDB.  Since we
       only need read operations on it, use a const-cast to get around
       that restriction.  */
    boost::scoped_ptr<CDBIterator> pcursor(const_cast<CDBWrapper*>(&db)->NewIterator());
    pcursor->SeekToFirst();

    /* Loop over the total database and read interesting
       things to memory.  We later use that to check
       everything against each other.  */

    std::set<valtype> namesTotal;
    std::set<valtype> namesInDB;
    std::set<valtype> namesWithHistory;
    std::map<valtype, CAmount> namesInUTXO;

    for (; pcursor->Valid(); pcursor->Next())
    {
        boost::this_thread::interruption_point();
        char chType;
        if (!pcursor->GetKey(chType))
            continue;

        switch (chType)
        {
        case DB_COINS:
        {
            CCoins coins;
            if (!pcursor->GetValue(coins))
                return error("%s : failed to read coins", __func__);

            BOOST_FOREACH(const CTxOut& txout, coins.vout)
                if (!txout.IsNull())
                {
                    const CNameScript nameOp(txout.scriptPubKey);
                    if (nameOp.isNameOp() && nameOp.isAnyUpdate())
                    {
                        const valtype& name = nameOp.getOpName();
                        if (namesInUTXO.count(name) > 0)
                            return error("%s : name %s duplicated in UTXO set",
                                         __func__, ValtypeToString(name).c_str());
                        namesInUTXO.insert(std::make_pair(nameOp.getOpName(),
                                                          txout.nValue));
                    }
                }
            break;
        }

        case DB_NAME:
        {
            std::pair<char, valtype> key;
            if (!pcursor->GetKey(key) || key.first != DB_NAME)
                return error("%s : failed to read DB_NAME key", __func__);
            const valtype& name = key.second;

            CNameData data;
            if (!pcursor->GetValue(data))
                return error("%s : failed to read name value", __func__);

            if (namesTotal.count(name) > 0)
                return error("%s : name %s duplicated in name index",
                             __func__, ValtypeToString(name).c_str());
            namesTotal.insert(name);
            
            assert(namesInDB.count(name) == 0);
            if (!data.isDead ())
                namesInDB.insert(name);
            break;
        }

        case DB_NAME_HISTORY:
        {
            std::pair<char, valtype> key;
            if (!pcursor->GetKey(key) || key.first != DB_NAME_HISTORY)
                return error("%s : failed to read DB_NAME_HISTORY key",
                             __func__);
            const valtype& name = key.second;

            if (namesWithHistory.count(name) > 0)
                return error("%s : name %s has duplicate history",
                             __func__, ValtypeToString(name).c_str());
            namesWithHistory.insert(name);
            break;
        }

        default:
            break;
        }
    }

    std::map<valtype, CAmount> namesInGame;
    GameState state(Params().GetConsensus());
    if (!gameDb.get(blockHash, state))
        return error("%s : failed to read game state", __func__);
    for (PlayerStateMap::const_iterator mi = state.players.begin();
         mi != state.players.end(); ++mi)
    {
        const valtype cur = ValtypeFromString(mi->first);
        if (namesInGame.count(cur) > 0)
            return error("%s : name %s is duplicate in the game state",
                         __func__, mi->first.c_str());
        namesInGame.insert(std::make_pair(cur, mi->second.lockedCoins));
    }

    /* Now verify the collected data.  */

    assert (namesTotal.size() >= namesInDB.size());

    if (namesInGame != namesInUTXO)
        return error("%s : game state and name DB mismatch", __func__);

    BOOST_FOREACH(const valtype& name, namesInDB)
        if (namesInUTXO.count(name) == 0)
            return error("%s : name '%s' in DB but not UTXO set",
                         __func__, ValtypeToString(name).c_str());
    BOOST_FOREACH(const PAIRTYPE(valtype, CAmount)& pair, namesInUTXO)
        if (namesInDB.count(pair.first) == 0)
            return error("%s : name '%s' in UTXO set but not DB",
                         __func__, ValtypeToString(pair.first).c_str());

    if (fNameHistory)
    {
        BOOST_FOREACH(const valtype& name, namesWithHistory)
            if (namesTotal.count(name) == 0)
                return error("%s : history entry for name '%s' not in main DB",
                             __func__, ValtypeToString(name).c_str());
    } else if (!namesWithHistory.empty ())
        return error("%s : name_history entries in DB, but"
                     " -namehistory not set", __func__);

    LogPrintf("Checked name database, %u living player names, %u total.\n",
              namesInDB.size(), namesTotal.size());
    LogPrintf("Names with history: %u\n", namesWithHistory.size());

    return true;
}

void
CNameCache::writeBatch (CDBBatch& batch) const
{
  for (EntryMap::const_iterator i = entries.begin ();
       i != entries.end (); ++i)
    batch.Write (std::make_pair (DB_NAME, i->first), i->second);

  for (std::set<valtype>::const_iterator i = deleted.begin ();
       i != deleted.end (); ++i)
    batch.Erase (std::make_pair (DB_NAME, *i));

  assert (fNameHistory || history.empty ());
  for (std::map<valtype, CNameHistory>::const_iterator i = history.begin ();
       i != history.end (); ++i)
    if (i->second.empty ())
      batch.Erase (std::make_pair (DB_NAME_HISTORY, i->first));
    else
      batch.Write (std::make_pair (DB_NAME_HISTORY, i->first), i->second);
}

bool CBlockTreeDB::ReadTxIndex(const uint256 &txid, CDiskTxPos &pos) {
    return Read(make_pair(DB_TXINDEX, txid), pos);
}

bool CBlockTreeDB::WriteTxIndex(const std::vector<std::pair<uint256, CDiskTxPos> >&vect) {
    CDBBatch batch(*this);
    for (std::vector<std::pair<uint256,CDiskTxPos> >::const_iterator it=vect.begin(); it!=vect.end(); it++)
        batch.Write(make_pair(DB_TXINDEX, it->first), it->second);
    return WriteBatch(batch);
}

bool CBlockTreeDB::WriteFlag(const std::string &name, bool fValue) {
    return Write(std::make_pair(DB_FLAG, name), fValue ? '1' : '0');
}

bool CBlockTreeDB::ReadFlag(const std::string &name, bool &fValue) {
    char ch;
    if (!Read(std::make_pair(DB_FLAG, name), ch))
        return false;
    fValue = ch == '1';
    return true;
}

bool CBlockTreeDB::LoadBlockIndexGuts(boost::function<CBlockIndex*(const uint256&)> insertBlockIndex)
{
    std::unique_ptr<CDBIterator> pcursor(NewIterator());

    pcursor->Seek(make_pair(DB_BLOCK_INDEX, uint256()));

    // Load mapBlockIndex
    while (pcursor->Valid()) {
        boost::this_thread::interruption_point();
        std::pair<char, uint256> key;
        if (pcursor->GetKey(key) && key.first == DB_BLOCK_INDEX) {
            CDiskBlockIndex diskindex;
            if (pcursor->GetValue(diskindex)) {
                // Construct block index object
                CBlockIndex* pindexNew = insertBlockIndex(diskindex.GetBlockHash());
                pindexNew->pprev          = insertBlockIndex(diskindex.hashPrev);
                pindexNew->nHeight        = diskindex.nHeight;
                pindexNew->nFile          = diskindex.nFile;
                pindexNew->nDataPos       = diskindex.nDataPos;
                pindexNew->nUndoPos       = diskindex.nUndoPos;
                pindexNew->nVersion       = diskindex.nVersion;
                pindexNew->hashMerkleRoot = diskindex.hashMerkleRoot;
                pindexNew->nTime          = diskindex.nTime;
                pindexNew->nBits          = diskindex.nBits;
                pindexNew->nNonce         = diskindex.nNonce;
                pindexNew->nStatus        = diskindex.nStatus;
                pindexNew->nTx            = diskindex.nTx;

                /* Bitcoin checks the PoW here.  We don't do this because
                   the CDiskBlockIndex does not contain the auxpow.
                   This check isn't important, since the data on disk should
                   already be valid and can be trusted.  */

                pcursor->Next();
            } else {
                return error("LoadBlockIndex() : failed to read value");
            }
        } else {
            break;
        }
    }

    return true;
}
