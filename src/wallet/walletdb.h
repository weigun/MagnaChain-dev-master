// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Copyright (c) 2016-2019 The MagnaChain Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef CELLLINK_WALLET_WALLETDB_H
#define CELLLINK_WALLET_WALLETDB_H

#include "misc/amount.h"
#include "primitives/transaction.h"
#include "wallet/db.h"
#include "key/key.h"

#include <list>
#include <stdint.h>
#include <string>
#include <utility>
#include <vector>

/**
 * Overview of wallet database classes:
 *
 * - CellDBEnv is an environment in which the database exists (has no analog in dbwrapper.h)
 * - CellWalletDBWrapper represents a wallet database (similar to CellDBWrapper in dbwrapper.h)
 * - CellDB is a low-level database transaction (similar to CellDBBatch in dbwrapper.h)
 * - CWalletDB is a modifier object for the wallet, and encapsulates a database
 *   transaction as well as methods to act on the database (no analog in
 *   dbwrapper.h)
 *
 * The latter two are named confusingly, in contrast to what the names CellDB
 * and CWalletDB suggest they are transient transaction objects and don't
 * represent the database itself.
 */

static const bool DEFAULT_FLUSHWALLET = true;

class CellAccount;
class CellAccountingEntry;
struct CellBlockLocator;
class CellKeyPool;
class CellMasterKey;
class CellScript;
class CellWallet;
class CellWalletTx;
class uint160;
class uint256;

/** Error statuses for the wallet database */
enum DBErrors
{
    DB_LOAD_OK,
    DB_CORRUPT,
    DB_NONCRITICAL_ERROR,
    DB_TOO_NEW,
    DB_LOAD_FAIL,
    DB_NEED_REWRITE
};

/* simple HD chain data model */
class CHDChain
{
public:
    uint32_t nExternalChainCounter;
    uint32_t nInternalChainCounter;
    CellKeyID masterKeyID; //!< master key hash160

    static const int VERSION_HD_BASE        = 1;
    static const int VERSION_HD_CHAIN_SPLIT = 2;
    static const int CURRENT_VERSION        = VERSION_HD_CHAIN_SPLIT;
    int nVersion;

    CHDChain() { SetNull(); }
    ADD_SERIALIZE_METHODS;
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        READWRITE(this->nVersion);
        READWRITE(nExternalChainCounter);
        READWRITE(masterKeyID);
        if (this->nVersion >= VERSION_HD_CHAIN_SPLIT)
            READWRITE(nInternalChainCounter);
    }

    void SetNull()
    {
        nVersion = CHDChain::CURRENT_VERSION;
        nExternalChainCounter = 0;
        nInternalChainCounter = 0;
        masterKeyID.SetNull();
    }
};

class CKeyMetadata
{
public:
    static const int VERSION_BASIC=1;
    static const int VERSION_WITH_HDDATA=10;
    static const int CURRENT_VERSION=VERSION_WITH_HDDATA;
    int nVersion;
    int64_t nCreateTime; // 0 means unknown
    std::string hdKeypath; //optional HD/bip32 keypath
    CellKeyID hdMasterKeyID; //id of the HD masterkey used to derive this key

    CKeyMetadata()
    {
        SetNull();
    }
    CKeyMetadata(int64_t nCreateTime_)
    {
        SetNull();
        nCreateTime = nCreateTime_;
    }

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(this->nVersion);
        READWRITE(nCreateTime);
        if (this->nVersion >= VERSION_WITH_HDDATA)
        {
            READWRITE(hdKeypath);
            READWRITE(hdMasterKeyID);
        }
    }

    void SetNull()
    {
        nVersion = CKeyMetadata::CURRENT_VERSION;
        nCreateTime = 0;
        hdKeypath.clear();
        hdMasterKeyID.SetNull();
    }
};

/** Access to the wallet database.
 * This should really be named CWalletDBBatch, as it represents a single transaction at the
 * database. It will be committed when the object goes out of scope.
 * Optionally (on by default) it will flush to disk as well.
 */
class CWalletDB
{
private:
    template <typename K, typename T>
    bool WriteIC(const K& key, const T& value, bool fOverwrite = true)
    {
        if (!batch.Write(key, value, fOverwrite)) {
            return false;
        }
        m_dbw.IncrementUpdateCounter();
        return true;
    }

    template <typename K>
    bool EraseIC(const K& key)
    {
        if (!batch.Erase(key)) {
            return false;
        }
        m_dbw.IncrementUpdateCounter();
        return true;
    }

public:
    CWalletDB(CellWalletDBWrapper& dbw, const char* pszMode = "r+", bool _fFlushOnClose = true) :
        batch(dbw, pszMode, _fFlushOnClose),
        m_dbw(dbw)
    {
    }

    bool WriteName(const std::string& strAddress, const std::string& strName);
    bool EraseName(const std::string& strAddress);

    bool WritePurpose(const std::string& strAddress, const std::string& purpose);
    bool ErasePurpose(const std::string& strAddress);

    bool WriteTx(const CellWalletTx& wtx);
    bool EraseTx(uint256 hash);

    bool WriteKey(const CellPubKey& vchPubKey, const CPrivKey& vchPrivKey, const CKeyMetadata &keyMeta);
    bool WriteCryptedKey(const CellPubKey& vchPubKey, const std::vector<unsigned char>& vchCryptedSecret, const CKeyMetadata &keyMeta);
    bool WriteMasterKey(unsigned int nID, const CellMasterKey& kMasterKey);

    bool WriteCScript(const uint160& hash, const CellScript& redeemScript);

    bool WriteWatchOnly(const CellScript &script, const CKeyMetadata &keymeta);
    bool EraseWatchOnly(const CellScript &script);

    bool WriteBestBlock(const CellBlockLocator& locator);
    bool ReadBestBlock(CellBlockLocator& locator);

    bool WriteOrderPosNext(int64_t nOrderPosNext);

    bool WriteDefaultKey(const CellPubKey& vchPubKey);

    bool ReadPool(int64_t nPool, CellKeyPool& keypool);
    bool WritePool(int64_t nPool, const CellKeyPool& keypool);
    bool ErasePool(int64_t nPool);

    bool WriteMinVersion(int nVersion);

    /// This writes directly to the database, and will not update the CellWallet's cached accounting entries!
    /// Use wallet.AddAccountingEntry instead, to write *and* update its caches.
    bool WriteAccountingEntry(const uint64_t nAccEntryNum, const CellAccountingEntry& acentry);
    bool ReadAccount(const std::string& strAccount, CellAccount& account);
    bool WriteAccount(const std::string& strAccount, const CellAccount& account);

    /// Write destination data key,value tuple to database
    bool WriteDestData(const std::string &address, const std::string &key, const std::string &value);
    /// Erase destination data tuple from wallet database
    bool EraseDestData(const std::string &address, const std::string &key);

    CellAmount GetAccountCreditDebit(const std::string& strAccount);
    void ListAccountCreditDebit(const std::string& strAccount, std::list<CellAccountingEntry>& acentries);

    DBErrors LoadWallet(CellWallet* pwallet);
    DBErrors FindWalletTx(std::vector<uint256>& vTxHash, std::vector<CellWalletTx>& vWtx);
    DBErrors ZapWalletTx(std::vector<CellWalletTx>& vWtx);
    DBErrors ZapSelectTx(std::vector<uint256>& vHashIn, std::vector<uint256>& vHashOut);
    /* Try to (very carefully!) recover wallet database (with a possible key type filter) */
    static bool Recover(const std::string& filename, void *callbackDataIn, bool (*recoverKVcallback)(void* callbackData, CellDataStream ssKey, CellDataStream ssValue), std::string& out_backup_filename);
    /* Recover convenience-function to bypass the key filter callback, called when verify fails, recovers everything */
    static bool Recover(const std::string& filename, std::string& out_backup_filename);
    /* Recover filter (used as callback), will only let keys (cryptographical keys) as KV/key-type pass through */
    static bool RecoverKeysOnlyFilter(void *callbackData, CellDataStream ssKey, CellDataStream ssValue);
    /* Function to determine if a certain KV/key-type is a key (cryptographical key) type */
    static bool IsKeyType(const std::string& strType);
    /* verifies the database environment */
    static bool VerifyEnvironment(const std::string& walletFile, const fs::path& dataDir, std::string& errorStr);
    /* verifies the database file */
    static bool VerifyDatabaseFile(const std::string& walletFile, const fs::path& dataDir, std::string& warningStr, std::string& errorStr);

    //! write the hdchain model (external chain child index counter)
    bool WriteHDChain(const CHDChain& chain);

    //! Begin a new transaction
    bool TxnBegin();
    //! Commit current transaction
    bool TxnCommit();
    //! Abort current transaction
    bool TxnAbort();
    //! Read wallet version
    bool ReadVersion(int& nVersion);
    //! Write wallet version
    bool WriteVersion(int nVersion);
private:
    CellDB batch;
    CellWalletDBWrapper& m_dbw;

    CWalletDB(const CWalletDB&);
    void operator=(const CWalletDB&);
};

//! Compacts BDB state so that wallet.dat is self-contained (if there are changes)
void MaybeCompactWalletDB();

#endif // CELLLINK_WALLET_WALLETDB_H
