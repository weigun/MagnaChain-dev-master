// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Copyright (c) 2016-2019 The MagnaChain Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef CELLLINK_WALLET_WALLET_H
#define CELLLINK_WALLET_WALLET_H

#include "misc/amount.h"
#include "policy/feerate.h"
#include "io/streams.h"
#include "misc/tinyformat.h"
#include "ui/ui_interface.h"
#include "utils/utilstrencodings.h"
#include "validation/validationinterface.h"
#include "script/ismine.h"
#include "script/sign.h"
#include "wallet/crypter.h"
#include "wallet/walletdb.h"
#include "wallet/rpcwallet.h"
#include "chain/branchchain.h"
#include "coding/base58.h"

#include <algorithm>
#include <atomic>
#include <map>
#include <set>
#include <stdexcept>
#include <stdint.h>
#include <string>
#include <utility>
#include <vector>

typedef CellWallet* CWalletRef;
extern std::vector<CWalletRef> vpwallets;

/**
 * Settings
 */
extern CellFeeRate payTxFee;
extern unsigned int nTxConfirmTarget;
extern bool bSpendZeroConfChange;
extern bool fWalletRbf;

static const unsigned int DEFAULT_KEYPOOL_SIZE = 1000;
//! -paytxfee default
static const CellAmount DEFAULT_TRANSACTION_FEE = 0;
//! -fallbackfee default
static const CellAmount DEFAULT_FALLBACK_FEE = 20000;
//! -m_discard_rate default
static const CellAmount DEFAULT_DISCARD_FEE = 10000;
//! -mintxfee default
static const CellAmount DEFAULT_TRANSACTION_MINFEE = 1000;
//! minimum recommended increment for BIP 125 replacement txs
static const CellAmount WALLET_INCREMENTAL_RELAY_FEE = 5000;
//! target minimum change amount
static const CellAmount MIN_CHANGE = CENT;
//! final minimum change amount after paying for fees
static const CellAmount MIN_FINAL_CHANGE = MIN_CHANGE/2;
//! Default for -spendzeroconfchange
static const bool DEFAULT_SPEND_ZEROCONF_CHANGE = true;
//! Default for -walletrejectlongchains
static const bool DEFAULT_WALLET_REJECT_LONG_CHAINS = false;
//! -txconfirmtarget default
static const unsigned int DEFAULT_TX_CONFIRM_TARGET = 6;
//! -walletrbf default
static const bool DEFAULT_WALLET_RBF = false;
static const bool DEFAULT_WALLETBROADCAST = true;
static const bool DEFAULT_DISABLE_WALLET = false;
//! if set, all keys will be derived by using BIP32
static const bool DEFAULT_USE_HD_WALLET = true;

extern const char * DEFAULT_WALLET_DAT;

static const int64_t TIMESTAMP_MIN = 0;

class CellBlockIndex;
class CellCoinControl;
class CellOutput;
class CellReserveKey;
class CellScript;
class CellScheduler;
class CellTxMemPool;
class CellBlockPolicyEstimator;
class CellWalletTx;
struct FeeCalculation;
enum class FeeEstimateMode;
class LuaStateExtraData;

/** (client) version numbers for particular wallet features */
enum WalletFeature
{
    FEATURE_BASE = 10500, // the earliest version new wallets supports (only useful for getinfo's clientversion output)

    FEATURE_WALLETCRYPT = 40000, // wallet encryption
    FEATURE_COMPRPUBKEY = 60000, // compressed public keys

    FEATURE_HD = 130000, // Hierarchical key derivation after BIP32 (HD Wallet)

    FEATURE_HD_SPLIT = 139900, // Wallet with HD chain split (change outputs will use m/0'/1'/k)

    FEATURE_LATEST = FEATURE_COMPRPUBKEY // HD is optional, use FEATURE_COMPRPUBKEY as latest version
};


/** A key pool entry */
class CellKeyPool
{
public:
    int64_t nTime;
    CellPubKey vchPubKey;
    bool fInternal; // for change outputs

    CellKeyPool();
    CellKeyPool(const CellPubKey& vchPubKeyIn, bool internalIn);

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        int nVersion = s.GetVersion();
        if (!(s.GetType() & SER_GETHASH))
            READWRITE(nVersion);
        READWRITE(nTime);
        READWRITE(vchPubKey);
        if (ser_action.ForRead()) {
            try {
                READWRITE(fInternal);
            }
            catch (std::ios_base::failure&) {
                /* flag as external address if we can't read the internal boolean
                   (this will be the case for any wallet before the HD chain split version) */
                fInternal = false;
            }
        }
        else {
            READWRITE(fInternal);
        }
    }
};

/** Address book data */
class CellAddressBookData
{
public:
    std::string name;
    std::string purpose;

    CellAddressBookData() : purpose("unknown") {}

    typedef std::map<std::string, std::string> StringMap;
    StringMap destdata;
};

struct CellRecipient
{
    CellScript scriptPubKey;
    CellAmount nAmount;
    bool fSubtractFeeFromAmount;
};

typedef std::map<std::string, std::string> mapValue_t;


static inline void ReadOrderPos(int64_t& nOrderPos, mapValue_t& mapValue)
{
    if (!mapValue.count("n"))
    {
        nOrderPos = -1; // TODO: calculate elsewhere
        return;
    }
    nOrderPos = atoi64(mapValue["n"].c_str());
}


static inline void WriteOrderPos(const int64_t& nOrderPos, mapValue_t& mapValue)
{
    if (nOrderPos == -1)
        return;
    mapValue["n"] = i64tostr(nOrderPos);
}

struct CellOutputEntry
{
    CellTxDestination destination;
    CellAmount amount;
    int vout;
};

/** A transaction with a merkle branch linking it to the block chain. */
class CellMerkleTx
{
private:
  /** Constant used in hashBlock to indicate tx has been abandoned */
    static const uint256 ABANDON_HASH;

public:
    CellTransactionRef tx;
    uint256 hashBlock;

    /* An nIndex == -1 means that hashBlock (in nonzero) refers to the earliest
     * block in the chain we know this or any in-wallet dependency conflicts
     * with. Older clients interpret nIndex == -1 as unconfirmed for backward
     * compatibility.
     */
    int nIndex;

    CellMerkleTx()
    {
        SetTx(MakeTransactionRef());
        Init();
    }

    CellMerkleTx(CellTransactionRef arg)
    {
        SetTx(std::move(arg));
        Init();
    }

    /** Helper conversion operator to allow passing CellMerkleTx where CellTransaction is expected.
     *  TODO: adapt callers and remove this operator. */
    operator const CellTransaction&() const { return *tx; }

    void Init()
    {
        hashBlock = uint256();
        nIndex = -1;
    }

    void SetTx(CellTransactionRef arg)
    {
        tx = std::move(arg);
    }

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        std::vector<uint256> vMerkleBranch; // For compatibility with older versions.
        READWRITE(tx);
        READWRITE(hashBlock);
        READWRITE(vMerkleBranch);
        READWRITE(nIndex);
    }

    void SetMerkleBranch(const CellBlockIndex* pIndex, int posInBlock);

    /**
     * Return depth of transaction in blockchain:
     * <0  : conflicts with a transaction this deep in the blockchain
     *  0  : in memory pool, waiting to be included in a block
     * >=1 : this many blocks deep in the main chain
     */
    int GetDepthInMainChain(const CellBlockIndex* &pindexRet) const;
    int GetDepthInMainChain() const { const CellBlockIndex *pindexRet; return GetDepthInMainChain(pindexRet); }
    bool IsInMainChain() const { const CellBlockIndex *pindexRet; return GetDepthInMainChain(pindexRet) > 0; }
    int GetBlocksToMaturity() const;
    int GetBlocksToMaturityForCoinCreateBranch() const;
    /** Pass this transaction to the mempool. Fails if absolute fee exceeds absurd fee. */
    bool AcceptToMemoryPool(const CellAmount& nAbsurdFee, CellValidationState& state, bool executeSmartContract, bool* pfMissingInputs = nullptr);
    bool hashUnset() const { return (hashBlock.IsNull() || hashBlock == ABANDON_HASH); }
    bool isAbandoned() const { return (hashBlock == ABANDON_HASH); }
    void setAbandoned() { hashBlock = ABANDON_HASH; }

    const uint256& GetHash() const { return tx->GetHash(); }
    bool IsCoinBase() const { return tx->IsCoinBase(); }
};

/** 
 * A transaction with a bunch of additional info that only the owner cares about.
 * It includes any unrecorded transactions needed to link it back to the block chain.
 */
class CellWalletTx : public CellMerkleTx
{
private:
    const CellWallet* pwallet;

public:
    /**
     * Key/value map with information about the transaction.
     *
     * The following keys can be read and written through the map and are
     * serialized in the wallet database:
     *
     *     "comment", "to"   - comment strings provided to sendtoaddress,
     *                         sendfrom, sendmany wallet RPCs
     *     "replaces_txid"   - txid (as HexStr) of transaction replaced by
     *                         bumpfee on transaction created by bumpfee
     *     "replaced_by_txid" - txid (as HexStr) of transaction created by
     *                         bumpfee on transaction replaced by bumpfee
     *     "from", "message" - obsolete fields that could be set in UI prior to
     *                         2011 (removed in commit 4d9b223)
     *
     * The following keys are serialized in the wallet database, but shouldn't
     * be read or written through the map (they will be temporarily added and
     * removed from the map during serialization):
     *
     *     "fromaccount"     - serialized strFromAccount value
     *     "n"               - serialized nOrderPos value
     *     "timesmart"       - serialized nTimeSmart value
     *     "spent"           - serialized vfSpent value that existed prior to
     *                         2014 (removed in commit 93a18a3)
     */
    mapValue_t mapValue;
    std::vector<std::pair<std::string, std::string> > vOrderForm;
    unsigned int fTimeReceivedIsTxTime;
    unsigned int nTimeReceived; //!< time received by this node
    /**
     * Stable timestamp that never changes, and reflects the order a transaction
     * was added to the wallet. Timestamp is based on the block time for a
     * transaction added as part of a block, or else the time when the
     * transaction was received if it wasn't part of a block, with the timestamp
     * adjusted in both cases so timestamp order matches the order transactions
     * were added to the wallet. More details can be found in
     * CellWallet::ComputeTimeSmart().
     */
    unsigned int nTimeSmart;
    /**
     * From me flag is set to 1 for transactions that were created by the wallet
     * on this magnachain node, and set to 0 for transactions that were created
     * externally and came in through the network or sendrawtransaction RPC.
     */
    char fFromMe;
    std::string strFromAccount;
    int64_t nOrderPos; //!< position in ordered transaction list

    // memory only
    mutable bool fDebitCached;
    mutable bool fCreditCached;
    mutable bool fImmatureCreditCached;
    mutable bool fAvailableCreditCached;
    mutable bool fWatchDebitCached;
    mutable bool fWatchCreditCached;
    mutable bool fImmatureWatchCreditCached;
    mutable bool fAvailableWatchCreditCached;
    mutable bool fChangeCached;
    mutable CellAmount nDebitCached;
    mutable CellAmount nCreditCached;
    mutable CellAmount nImmatureCreditCached;
    mutable CellAmount nAvailableCreditCached;
    mutable CellAmount nWatchDebitCached;
    mutable CellAmount nWatchCreditCached;
    mutable CellAmount nImmatureWatchCreditCached;
    mutable CellAmount nAvailableWatchCreditCached;
    mutable CellAmount nChangeCached;
	// temp data for contract
	int32_t nVersion = CellTransaction::CURRENT_VERSION;//special version

	// temp data for branch
	std::string branchVSeeds;
	std::string branchSeedSpec6;
	//trans
	std::string sendToBranchid;
	std::string sendToTxHexData;
	//uint64_t inAmount;
    std::string fromBranchId;

    std::shared_ptr<const CellSpvProof> pPMT;
    std::vector<unsigned char> fromTx;
    std::shared_ptr<ContractData> pContractData;
    std::shared_ptr<const ReportData> pReportData;
    std::shared_ptr<ProveData> pProveData;

    bool isDataTransaction; // transaction can be fee only, no transfer
    std::shared_ptr<CellBranchBlockInfo> pBranchBlockData;

    uint256 reporttxid;
    uint256 coinpreouthash;
    uint256 provetxid;

    CellWalletTx()
    {
        Init(nullptr);
    }

    CellWalletTx(const CellWallet* pwalletIn, CellTransactionRef arg) : CellMerkleTx(std::move(arg))
    {
        Init(pwalletIn);
    }

    void Init(const CellWallet* pwalletIn)
    {
        pwallet = pwalletIn;
        mapValue.clear();
        vOrderForm.clear();
        fTimeReceivedIsTxTime = false;
        nTimeReceived = 0;
        nTimeSmart = 0;
        fFromMe = false;
        strFromAccount.clear();
        fDebitCached = false;
        fCreditCached = false;
        fImmatureCreditCached = false;
        fAvailableCreditCached = false;
        fWatchDebitCached = false;
        fWatchCreditCached = false;
        fImmatureWatchCreditCached = false;
        fAvailableWatchCreditCached = false;
        fChangeCached = false;
        nDebitCached = 0;
        nCreditCached = 0;
        nImmatureCreditCached = 0;
        nAvailableCreditCached = 0;
        nWatchDebitCached = 0;
        nWatchCreditCached = 0;
        nAvailableWatchCreditCached = 0;
        nImmatureWatchCreditCached = 0;
        nChangeCached = 0;
        nOrderPos = -1;
        isDataTransaction = false;
    }

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        if (ser_action.ForRead())
            Init(nullptr);
        char fSpent = false;

        if (!ser_action.ForRead())
        {
            mapValue["fromaccount"] = strFromAccount;

            WriteOrderPos(nOrderPos, mapValue);

            if (nTimeSmart)
                mapValue["timesmart"] = strprintf("%u", nTimeSmart);
        }

        READWRITE(*(CellMerkleTx*)this);
        std::vector<CellMerkleTx> vUnused; //!< Used to be vtxPrev
        READWRITE(vUnused);
        READWRITE(mapValue);
        READWRITE(vOrderForm);
        READWRITE(fTimeReceivedIsTxTime);
        READWRITE(nTimeReceived);
        READWRITE(fFromMe);
        READWRITE(fSpent);

        if (ser_action.ForRead())
        {
            strFromAccount = mapValue["fromaccount"];

            ReadOrderPos(nOrderPos, mapValue);

            nTimeSmart = mapValue.count("timesmart") ? (unsigned int)atoi64(mapValue["timesmart"]) : 0;
        }

        mapValue.erase("fromaccount");
        mapValue.erase("spent");
        mapValue.erase("n");
        mapValue.erase("timesmart");
    }

    //! make sure balances are recalculated
    void MarkDirty()
    {
        fCreditCached = false;
        fAvailableCreditCached = false;
        fImmatureCreditCached = false;
        fWatchDebitCached = false;
        fWatchCreditCached = false;
        fAvailableWatchCreditCached = false;
        fImmatureWatchCreditCached = false;
        fDebitCached = false;
        fChangeCached = false;
    }

    void BindWallet(CellWallet *pwalletIn)
    {
        pwallet = pwalletIn;
        MarkDirty();
    }

    //! filter decides which addresses will count towards the debit
    CellAmount GetDebit(const isminefilter& filter) const;
    CellAmount GetCredit(const isminefilter& filter) const;
    CellAmount GetImmatureCredit(bool fUseCache=true) const;
    CellAmount GetAvailableCredit(bool fUseCache=true) const;
    CellAmount GetImmatureWatchOnlyCredit(const bool& fUseCache=true) const;
    CellAmount GetAvailableWatchOnlyCredit(const bool& fUseCache=true) const;
    CellAmount GetChange() const;

    void GetAmounts(std::list<CellOutputEntry>& listReceived,
                    std::list<CellOutputEntry>& listSent, CellAmount& nFee, std::string& strSentAccount, const isminefilter& filter) const;

    bool IsFromMe(const isminefilter& filter) const
    {
        return (GetDebit(filter) > 0);
    }

    // True if only scriptSigs are different
    bool IsEquivalentTo(const CellWalletTx& tx) const;

    bool IsSmartContract() const {
        return nVersion == CellTransaction::PUBLISH_CONTRACT_VERSION || nVersion == CellTransaction::CALL_CONTRACT_VERSION;
    }

    bool InMempool() const;
    bool IsTrusted() const;

    int64_t GetTxTime() const;
    int GetRequestCount() const;

    // RelayWalletTransaction may only be called if fBroadcastTransactions!
    bool RelayWalletTransaction(CellConnman* connman);

    std::set<uint256> GetConflicts() const;
};


class CellInputCoin {
public:
    CellInputCoin(const CellWalletTx* walletTx, unsigned int i)
    {
        if (!walletTx)
            throw std::invalid_argument("walletTx should not be null");
        if (i >= walletTx->tx->vout.size())
            throw std::out_of_range("The output index is out of range");

        outpoint = CellOutPoint(walletTx->GetHash(), i);
        txout = walletTx->tx->vout[i];
    }

    CellOutPoint outpoint;
    CellTxOut txout;

    bool operator<(const CellInputCoin& rhs) const {
        return outpoint < rhs.outpoint;
    }

    bool operator!=(const CellInputCoin& rhs) const {
        return outpoint != rhs.outpoint;
    }

    bool operator==(const CellInputCoin& rhs) const {
        return outpoint == rhs.outpoint;
    }
};

class CellOutput
{
public:
    const CellWalletTx *tx;
    int i;
    int nDepth;

    /** Whether we have the private keys to spend this output */
    bool fSpendable;

    /** Whether we know how to spend this output, ignoring the lack of keys */
    bool fSolvable;

    /**
     * Whether this output is considered safe to spend. Unconfirmed transactions
     * from outside keys and unconfirmed replacement transactions are considered
     * unsafe and will not be used to fund new spending transactions.
     */
    bool fSafe;

    CellOutput(const CellWalletTx *txIn, int iIn, int nDepthIn, bool fSpendableIn, bool fSolvableIn, bool fSafeIn)
    {
        tx = txIn; i = iIn; nDepth = nDepthIn; fSpendable = fSpendableIn; fSolvable = fSolvableIn; fSafe = fSafeIn;
    }

    std::string ToString() const;
};




/** Private key that includes an expiration date in case it never gets used. */
class CWalletKey
{
public:
    CPrivKey vchPrivKey;
    int64_t nTimeCreated;
    int64_t nTimeExpires;
    std::string strComment;
    //! todo: add something to note what created it (user, getnewaddress, change)
    //!   maybe should have a map<string, string> property map

    CWalletKey(int64_t nExpires=0);

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        int nVersion = s.GetVersion();
        if (!(s.GetType() & SER_GETHASH))
            READWRITE(nVersion);
        READWRITE(vchPrivKey);
        READWRITE(nTimeCreated);
        READWRITE(nTimeExpires);
        READWRITE(LIMITED_STRING(strComment, 65536));
    }
};

/**
 * Internal transfers.
 * Database key is acentry<account><counter>.
 */
class CellAccountingEntry
{
public:
    std::string strAccount;
    CellAmount nCreditDebit;
    int64_t nTime;
    std::string strOtherAccount;
    std::string strComment;
    mapValue_t mapValue;
    int64_t nOrderPos; //!< position in ordered transaction list
    uint64_t nEntryNo;

    CellAccountingEntry()
    {
        SetNull();
    }

    void SetNull()
    {
        nCreditDebit = 0;
        nTime = 0;
        strAccount.clear();
        strOtherAccount.clear();
        strComment.clear();
        nOrderPos = -1;
        nEntryNo = 0;
    }

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        int nVersion = s.GetVersion();
        if (!(s.GetType() & SER_GETHASH))
            READWRITE(nVersion);
        //! Note: strAccount is serialized as part of the key, not here.
        READWRITE(nCreditDebit);
        READWRITE(nTime);
        READWRITE(LIMITED_STRING(strOtherAccount, 65536));

        if (!ser_action.ForRead())
        {
            WriteOrderPos(nOrderPos, mapValue);

            if (!(mapValue.empty() && _ssExtra.empty()))
            {
                CellDataStream ss(s.GetType(), s.GetVersion());
                ss.insert(ss.begin(), '\0');
                ss << mapValue;
                ss.insert(ss.end(), _ssExtra.begin(), _ssExtra.end());
                strComment.append(ss.str());
            }
        }

        READWRITE(LIMITED_STRING(strComment, 65536));

        size_t nSepPos = strComment.find("\0", 0, 1);
        if (ser_action.ForRead())
        {
            mapValue.clear();
            if (std::string::npos != nSepPos)
            {
                CellDataStream ss(std::vector<char>(strComment.begin() + nSepPos + 1, strComment.end()), s.GetType(), s.GetVersion());
                ss >> mapValue;
                _ssExtra = std::vector<char>(ss.begin(), ss.end());
            }
            ReadOrderPos(nOrderPos, mapValue);
        }
        if (std::string::npos != nSepPos)
            strComment.erase(nSepPos);

        mapValue.erase("n");
    }

private:
    std::vector<char> _ssExtra;
};


/** 
 * A CellWallet is an extension of a keystore, which also maintains a set of transactions and balances,
 * and provides the ability to create new transactions.
 */
class CellWallet : public CellCryptoKeyStore, public CellValidationInterface
{
private:
    static std::atomic<bool> fFlushScheduled;
    std::atomic<bool> fAbortRescan;
    std::atomic<bool> fScanningWallet;

    /**
     * Select a set of coins such that nValueRet >= nTargetValue and at least
     * all coins from coinControl are selected; Never select unconfirmed coins
     * if they are not ours
     */
    bool SelectCoins(const std::vector<CellOutput>& vAvailableCoins, const CellAmount& nTargetValue, std::set<CellInputCoin>& setCoinsRet, CellAmount& nValueRet, const CellCoinControl *coinControl = nullptr) const;

    CWalletDB *pwalletdbEncryption;

    //! the current wallet version: clients below this version are not able to load the wallet
    int nWalletVersion;

    //! the maximum wallet format version: memory-only variable that specifies to what version this wallet may be upgraded
    int nWalletMaxVersion;

    int64_t nNextResend;
    int64_t nLastResend;
    bool fBroadcastTransactions;

    /**
     * Used to keep track of spent outpoints, and
     * detect and report conflicts (double-spends or
     * mutated transactions where the mutant gets mined).
     */
    typedef std::multimap<CellOutPoint, uint256> TxSpends;
    TxSpends mapTxSpends;
    void AddToSpends(const CellOutPoint& outpoint, const uint256& wtxid);
    void AddToSpends(const uint256& wtxid);

    /* Mark a transaction (and its in-wallet descendants) as conflicting with a particular block. */
    void MarkConflicted(const uint256& hashBlock, const uint256& hashTx);

    void SyncMetaData(std::pair<TxSpends::iterator, TxSpends::iterator>);

    /* Used by TransactionAddedToMemorypool/BlockConnected/Disconnected.
     * Should be called with pindexBlock and posInBlock if this is for a transaction that is included in a block. */
    void SyncTransaction(const CellTransactionRef& tx, const CellBlockIndex *pindex = nullptr, int posInBlock = 0);

    /* the HD chain data model (external chain counters) */
    CHDChain hdChain;

    /* HD derive new child key (on internal or external chain) */
    void DeriveNewChildKey(CWalletDB &walletdb, CKeyMetadata& metadata, CellKey& secret, bool internal = false);

    std::set<int64_t> setInternalKeyPool;
    std::set<int64_t> setExternalKeyPool;
    int64_t m_max_keypool_index;
    std::map<CellKeyID, int64_t> m_pool_key_to_index;

    int64_t nTimeFirstKey;

    /**
     * Private version of AddWatchOnly method which does not accept a
     * timestamp, and which will reset the wallet's nTimeFirstKey value to 1 if
     * the watch key did not previously have a timestamp associated with it.
     * Because this is an inherited virtual method, it is accessible despite
     * being marked private, but it is marked private anyway to encourage use
     * of the other AddWatchOnly which accepts a timestamp and sets
     * nTimeFirstKey more intelligently for more efficient rescans.
     */
    bool AddWatchOnly(const CellScript& dest) override;

    std::unique_ptr<CellWalletDBWrapper> dbw;

protected:
	bool fFastMode;
	bool fFakeWallet;
public:
    /*
     * Main wallet lock.
     * This lock protects all the fields added by CellWallet.
     */
    mutable CellCriticalSection cs_wallet;

	CellLinkAddress _senderAddr;

    /** Get database handle used by this wallet. Ideally this function would
     * not be necessary.
     */
    CellWalletDBWrapper& GetDBHandle()
    {
        return *dbw;
    }

    /** Get a name for this wallet for logging/debugging purposes.
     */
    std::string GetName() const
    {
        if (dbw) {
            return dbw->GetName();
        } else {
            return "dummy";
        }
    }

    void LoadKeyPool(int64_t nIndex, const CellKeyPool &keypool);

    // Map from Key ID (for regular keys) or Script ID (for watch-only keys) to
    // key metadata.
    std::map<CellTxDestination, CKeyMetadata> mapKeyMetadata;

    typedef std::map<unsigned int, CellMasterKey> MasterKeyMap;
    MasterKeyMap mapMasterKeys;
    unsigned int nMasterKeyMaxID;

    // Create wallet with dummy database handle
    CellWallet(): dbw(new CellWalletDBWrapper())
    {
        SetNull();
    }

    // Create wallet with passed-in database handle
    CellWallet(std::unique_ptr<CellWalletDBWrapper> dbw_in) : dbw(std::move(dbw_in))
    {
        SetNull();
    }

    ~CellWallet()
    {
        delete pwalletdbEncryption;
        pwalletdbEncryption = nullptr;
    }

    void SetNull()
    {
        nWalletVersion = FEATURE_BASE;
        nWalletMaxVersion = FEATURE_BASE;
        nMasterKeyMaxID = 0;
        pwalletdbEncryption = nullptr;
        nOrderPosNext = 0;
        nAccountingEntryNumber = 0;
        nNextResend = 0;
        nLastResend = 0;
        m_max_keypool_index = 0;
        nTimeFirstKey = 0;
        fBroadcastTransactions = false;
        nRelockTime = 0;
        fAbortRescan = false;
        fScanningWallet = false;
		fFastMode = false;
		fFakeWallet = false;
    }

    std::map<uint256, CellWalletTx> mapWallet;
    std::list<CellAccountingEntry> laccentries;

    typedef std::pair<CellWalletTx*, CellAccountingEntry*> TxPair;
    typedef std::multimap<int64_t, TxPair > TxItems;
    TxItems wtxOrdered;

    int64_t nOrderPosNext;
    uint64_t nAccountingEntryNumber;
    std::map<uint256, int> mapRequestCount;

    std::map<CellTxDestination, CellAddressBookData> mapAddressBook;

    CellPubKey vchDefaultKey;

    std::set<CellOutPoint> setLockedCoins;

    const CellWalletTx* GetWalletTx(const uint256& hash) const;

    //! check whether we are allowed to upgrade (or already support) to the named feature
    bool CanSupportFeature(enum WalletFeature wf) const { AssertLockHeld(cs_wallet); return nWalletMaxVersion >= wf; }

    /**
     * populate vCoins with vector of available COutputs.
     */
	void AvailableCoins(std::vector<CellOutput>& vCoins, const CellTxDestination* dest = nullptr, bool fOnlySafe = true, const CellCoinControl *coinControl = nullptr, const CellAmount& nMinimumAmount = 1, const CellAmount& nMaximumAmount = MAX_MONEY, const CellAmount& nMinimumSumAmount = MAX_MONEY, const uint64_t& nMaximumCount = 0, const int& nMinDepth = 0, const int& nMaxDepth = 9999999) const;
    void AvailableMortgageCoins(std::vector<CellOutput>& vCoins, bool fOnlySafe = true, branch_script_type bsptype = BST_MORTGAGE_COIN, const CellCoinControl *coinControl = nullptr, const CellAmount& nMinimumAmount = 1, const CellAmount& nMaximumAmount = MAX_MONEY, const CellAmount& nMinimumSumAmount = MAX_MONEY, const uint64_t& nMaximumCount = 0, const int& nMinDepth = 0, const int& nMaxDepth = 9999999);

    /**
     * Return list of available coins and locked coins grouped by non-change output address.
     */
    std::map<CellTxDestination, std::vector<CellOutput>> ListCoins() const;

    /**
     * Find non-change parent output.
     */
    const CellTxOut& FindNonChangeParentOutput(const CellTransaction& tx, int output) const;

    /**
     * Shuffle and select coins until nTargetValue is reached while avoiding
     * small change; This method is stochastic for some inputs and upon
     * completion the coin set and corresponding actual target value is
     * assembled
     */
    bool SelectCoinsMinConf(const CellAmount& nTargetValue, int nConfMine, int nConfTheirs, uint64_t nMaxAncestors, std::vector<CellOutput> vCoins, std::set<CellInputCoin>& setCoinsRet, CellAmount& nValueRet) const;

    bool IsSpent(const uint256& hash, unsigned int n) const;

    bool IsLockedCoin(uint256 hash, unsigned int n) const;
    void LockCoin(const CellOutPoint& output);
    void UnlockCoin(const CellOutPoint& output);
    void UnlockAllCoins();
    void ListLockedCoins(std::vector<CellOutPoint>& vOutpts) const;

    /*
     * Rescan abort properties
     */
    void AbortRescan() { fAbortRescan = true; }
    bool IsAbortingRescan() { return fAbortRescan; }
    bool IsScanning() { return fScanningWallet; }

    /**
     * keystore implementation
     * Generate a new key
     */
    CellPubKey GenerateNewKey(CWalletDB& walletdb, bool internal = false);
    //! Adds a key to the store, and saves it to disk.
    bool AddKeyPubKey(const CellKey& key, const CellPubKey &pubkey) override;
    bool AddKeyPubKeyWithDB(CWalletDB &walletdb,const CellKey& key, const CellPubKey &pubkey);
    //! Adds a key to the store, without saving it to disk (used by LoadWallet)
    bool LoadKey(const CellKey& key, const CellPubKey &pubkey) { return CellCryptoKeyStore::AddKeyPubKey(key, pubkey); }
    //! Load metadata (used by LoadWallet)
    bool LoadKeyMetadata(const CellTxDestination& pubKey, const CKeyMetadata &metadata);

    bool LoadMinVersion(int nVersion) { AssertLockHeld(cs_wallet); nWalletVersion = nVersion; nWalletMaxVersion = std::max(nWalletMaxVersion, nVersion); return true; }
    void UpdateTimeFirstKey(int64_t nCreateTime);

    //! Adds an encrypted key to the store, and saves it to disk.
    bool AddCryptedKey(const CellPubKey &vchPubKey, const std::vector<unsigned char> &vchCryptedSecret) override;
    //! Adds an encrypted key to the store, without saving it to disk (used by LoadWallet)
    bool LoadCryptedKey(const CellPubKey &vchPubKey, const std::vector<unsigned char> &vchCryptedSecret);
    bool AddCScript(const CellScript& redeemScript) override;
    bool LoadCScript(const CellScript& redeemScript);

    //! Adds a destination data tuple to the store, and saves it to disk
    bool AddDestData(const CellTxDestination &dest, const std::string &key, const std::string &value);
    //! Erases a destination data tuple in the store and on disk
    bool EraseDestData(const CellTxDestination &dest, const std::string &key);
    //! Adds a destination data tuple to the store, without saving it to disk
    bool LoadDestData(const CellTxDestination &dest, const std::string &key, const std::string &value);
    //! Look up a destination data tuple in the store, return true if found false otherwise
    bool GetDestData(const CellTxDestination &dest, const std::string &key, std::string *value) const;
    //! Get all destination values matching a prefix.
    std::vector<std::string> GetDestValues(const std::string& prefix) const;

    //! Adds a watch-only address to the store, and saves it to disk.
    bool AddWatchOnly(const CellScript& dest, int64_t nCreateTime);
    bool RemoveWatchOnly(const CellScript &dest) override;
    //! Adds a watch-only address to the store, without saving it to disk (used by LoadWallet)
    bool LoadWatchOnly(const CellScript &dest);

    //! Holds a timestamp at which point the wallet is scheduled (externally) to be relocked. Caller must arrange for actual relocking to occur via Lock().
    int64_t nRelockTime;

    bool Unlock(const SecureString& strWalletPassphrase);
    bool ChangeWalletPassphrase(const SecureString& strOldWalletPassphrase, const SecureString& strNewWalletPassphrase);
    bool EncryptWallet(const SecureString& strWalletPassphrase);

    void GetKeyBirthTimes(std::map<CellTxDestination, int64_t> &mapKeyBirth) const;
    unsigned int ComputeTimeSmart(const CellWalletTx& wtx) const;

    /** 
     * Increment the next transaction order id
     * @return next transaction order id
     */
    int64_t IncOrderPosNext(CWalletDB *pwalletdb = nullptr);
    DBErrors ReorderTransactions();
    bool AccountMove(std::string strFrom, std::string strTo, CellAmount nAmount, std::string strComment = "");
    bool GetAccountPubkey(CellPubKey &pubKey, std::string strAccount, bool bForceNew = false);

    void MarkDirty();
    bool AddToWallet(const CellWalletTx& wtxIn, bool fFlushOnClose=true);
    bool LoadToWallet(const CellWalletTx& wtxIn);
    void TransactionAddedToMempool(const CellTransactionRef& tx) override;
    void BlockConnected(const std::shared_ptr<const CellBlock>& pblock, const CellBlockIndex *pindex, const std::vector<CellTransactionRef>& vtxConflicted) override;
    void BlockDisconnected(const std::shared_ptr<const CellBlock>& pblock) override;
    bool AddToWalletIfInvolvingMe(const CellTransactionRef& tx, const CellBlockIndex* pIndex, int posInBlock, bool fUpdate);
    int64_t RescanFromTime(int64_t startTime, bool update);
    CellBlockIndex* ScanForWalletTransactions(CellBlockIndex* pindexStart, bool fUpdate = false);
    void ReacceptWalletTransactions();
    void ResendWalletTransactions(int64_t nBestBlockTime, CellConnman* connman) override;
    // ResendWalletTransactionsBefore may only be called if fBroadcastTransactions!
    std::vector<uint256> ResendWalletTransactionsBefore(int64_t nTime, CellConnman* connman);
    CellAmount GetBalance() const;
    CellAmount GetUnconfirmedBalance() const;
    CellAmount GetImmatureBalance() const;
    CellAmount GetWatchOnlyBalance() const;
    CellAmount GetUnconfirmedWatchOnlyBalance() const;
    CellAmount GetImmatureWatchOnlyBalance() const;
    CellAmount GetLegacyBalance(const isminefilter& filter, int minDepth, const std::string* account) const;
    CellAmount GetAvailableBalance(const CellCoinControl* coinControl = nullptr) const;

    /**
     * Insert additional inputs into the transaction by
     * calling CreateTransaction();
     */
    bool FundTransaction(CellMutableTransaction& tx, CellAmount& nFeeRet, int& nChangePosInOut, std::string& strFailReason, bool lockUnspents, const std::set<int>& setSubtractFeeFromOutputs, CellCoinControl);
    bool SignTransaction(CellMutableTransaction& tx);

    /**
     * Create a new transaction paying the recipients with a set of coins
     * selected by SelectCoins(); Also create the change output, when needed
     * @note passing nChangePosInOut as -1 will result in setting a random position
     */
    bool CreateTransaction(const std::vector<CellRecipient>& vecSend, CellWalletTx& wtxNew, CellReserveKey& reservekey, CellAmount& nFeeRet, int& nChangePosInOut,
                           std::string& strFailReason, const CellCoinControl& coin_control, bool sign = true, SmartLuaState* sls = nullptr );
    bool CommitTransaction(CellWalletTx& wtxNew, CellReserveKey& reservekey, CellConnman* connman, CellValidationState& state);

    void ListAccountCreditDebit(const std::string& strAccount, std::list<CellAccountingEntry>& entries);
    bool AddAccountingEntry(const CellAccountingEntry&);
    bool AddAccountingEntry(const CellAccountingEntry&, CWalletDB *pwalletdb);
    template <typename ContainerType>
    bool DummySignTx(CellMutableTransaction &txNew, const ContainerType &coins) const;

    static CellFeeRate minTxFee;
    static CellFeeRate fallbackFee;
    static CellFeeRate m_discard_rate;
    /**
     * Estimate the minimum fee considering user set parameters
     * and the required fee
     */
    static CellAmount GetMinimumFee(unsigned int nTxBytes, const CellCoinControl& coin_control, const CellTxMemPool& pool, const CellBlockPolicyEstimator& estimator, FeeCalculation *feeCalc, CellMutableTransaction* tx = nullptr, SmartLuaState* sls = nullptr);
    /**
     * Return the minimum required fee taking into account the
     * floating relay fee and user set minimum transaction fee
     */
    static CellAmount GetRequiredFee(unsigned int nTxBytes);

    bool NewKeyPool();
    size_t KeypoolCountExternalKeys();
    bool TopUpKeyPool(unsigned int kpSize = 0);
    void ReserveKeyFromKeyPool(int64_t& nIndex, CellKeyPool& keypool, bool fRequestedInternal);
    void KeepKey(int64_t nIndex);
    void ReturnKey(int64_t nIndex, bool fInternal, const CellPubKey& pubkey);
    bool GetKeyFromPool(CellPubKey &key, bool internal = false);
    int64_t GetOldestKeyPoolTime();
    /**
     * Marks all keys in the keypool up to and including reserve_key as used.
     */
    void MarkReserveKeysAsUsed(int64_t keypool_id);
    const std::map<CellKeyID, int64_t>& GetAllReserveKeys() const { return m_pool_key_to_index; }

    std::set< std::set<CellTxDestination> > GetAddressGroupings();
    std::map<CellTxDestination, CellAmount> GetAddressBalances();

    std::set<CellTxDestination> GetAccountAddresses(const std::string& strAccount) const;

    isminetype IsMine(const CellTxIn& txin) const;
    /**
     * Returns amount of debit if the input matches the
     * filter, otherwise returns 0
     */
    CellAmount GetDebit(const CellTxIn& txin, const isminefilter& filter) const;
	isminetype IsMine(const CellTxOut& txout) const;
    CellAmount GetCredit(const CellTxOut& txout, const isminefilter& filter) const;
    bool IsChange(const CellTxOut& txout) const;
    CellAmount GetChange(const CellTxOut& txout) const;
    bool IsMine(const CellTransaction& tx) const;
    /** should probably be renamed to IsRelevantToMe */
    bool IsFromMe(const CellTransaction& tx) const;
    CellAmount GetDebit(const CellTransaction& tx, const isminefilter& filter) const;
    /** Returns whether all of the inputs match the filter */
    bool IsAllFromMe(const CellTransaction& tx, const isminefilter& filter) const;
    CellAmount GetCredit(const CellTransaction& tx, const isminefilter& filter) const;
    CellAmount GetChange(const CellTransaction& tx) const;
    void SetBestChain(const CellBlockLocator& loc) override;

    DBErrors LoadWallet(bool& fFirstRunRet);
    DBErrors ZapWalletTx(std::vector<CellWalletTx>& vWtx);
    DBErrors ZapSelectTx(std::vector<uint256>& vHashIn, std::vector<uint256>& vHashOut);

    bool SetAddressBook(const CellTxDestination& address, const std::string& strName, const std::string& purpose);

    bool DelAddressBook(const CellTxDestination& address);

    const std::string& GetAccountName(const CellScript& scriptPubKey) const;

    void Inventory(const uint256 &hash) override
    {
        {
            LOCK(cs_wallet);
            std::map<uint256, int>::iterator mi = mapRequestCount.find(hash);
            if (mi != mapRequestCount.end())
                (*mi).second++;
        }
    }

    void GetScriptForMining(std::shared_ptr<CReserveScript> &script);
    
    unsigned int GetKeyPoolSize()
    {
        AssertLockHeld(cs_wallet); // set{Ex,In}ternalKeyPool
        return setInternalKeyPool.size() + setExternalKeyPool.size();
    }

    bool SetDefaultKey(const CellPubKey &vchPubKey);

    //! signify that a particular wallet feature is now used. this may change nWalletVersion and nWalletMaxVersion if those are lower
    bool SetMinVersion(enum WalletFeature, CWalletDB* pwalletdbIn = nullptr, bool fExplicit = false);

    //! change which version we're allowed to upgrade to (note that this does not immediately imply upgrading to that format)
    bool SetMaxVersion(int nVersion);

    //! get the current wallet format (the oldest client version guaranteed to understand this wallet)
    int GetVersion() { LOCK(cs_wallet); return nWalletVersion; }

    //! Get wallet transactions that conflict with given transaction (spend same outputs)
    std::set<uint256> GetConflicts(const uint256& txid) const;

    //! Check if a given transaction has any of its outputs spent by another transaction in the wallet
    bool HasWalletSpend(const uint256& txid) const;

    //! Flush wallet (bitdb flush)
    void Flush(bool shutdown=false);

    //! Responsible for reading and validating the -wallet arguments and verifying the wallet database.
    //  This function will perform salvage on the wallet if requested, as long as only one wallet is
    //  being loaded (CellWallet::ParameterInteraction forbids -salvagewallet, -zapwallettxes or -upgradewallet with multiwallet).
    static bool Verify();
    
    /** 
     * Address book entry changed.
     * @note called with lock cs_wallet held.
     */
    boost::signals2::signal<void (CellWallet *wallet, const CellTxDestination
            &address, const std::string &label, bool isMine,
            const std::string &purpose,
            ChangeType status)> NotifyAddressBookChanged;

    /** 
     * Wallet transaction added, removed or updated.
     * @note called with lock cs_wallet held.
     */
    boost::signals2::signal<void (CellWallet *wallet, const uint256 &hashTx,
            ChangeType status)> NotifyTransactionChanged;

    /** Show progress e.g. for rescan */
    boost::signals2::signal<void (const std::string &title, int nProgress)> ShowProgress;

    /** Watch-only address added */
    boost::signals2::signal<void (bool fHaveWatchOnly)> NotifyWatchonlyChanged;

    /** Inquire whether this wallet broadcasts transactions. */
    bool GetBroadcastTransactions() const { return fBroadcastTransactions; }
    /** Set whether this wallet broadcasts transactions. */
    void SetBroadcastTransactions(bool broadcast) { fBroadcastTransactions = broadcast; }

    /** Return whether transaction can be abandoned */
    bool TransactionCanBeAbandoned(const uint256& hashTx) const;

    /* Mark a transaction (and it in-wallet descendants) as abandoned so its inputs may be respent. */
    bool AbandonTransaction(const uint256& hashTx);

    /** Mark a transaction as replaced by another transaction (e.g., BIP 125). */
    bool MarkReplaced(const uint256& originalHash, const uint256& newHash);

    /* Returns the wallets help message */
    static std::string GetWalletHelpString(bool showDebug);

    /* Initializes the wallet, returns a new CellWallet instance or a null pointer in case of an error */
    static CellWallet* CreateWalletFromFile(const std::string walletFile);
    static bool InitLoadWallet();

    /**
     * Wallet post-init setup
     * Gives the wallet a chance to register repetitive tasks and complete post-init tasks
     */
    void postInitProcess(CellScheduler& scheduler);

    /* Wallets parameter interaction */
    static bool ParameterInteraction();

    bool BackupWallet(const std::string& strDest);

    /* Set the HD chain model (chain child index counters) */
    bool SetHDChain(const CHDChain& chain, bool memonly);
    const CHDChain& GetHDChain() const { return hdChain; }

    /* Returns true if HD is enabled */
    bool IsHDEnabled() const;

    /* Generates a new HD master key (will not be activated) */
    CellPubKey GenerateNewHDMasterKey();
    
    /* Set the current HD master key (will reset the chain child index counters)
       Sets the master key's version based on the current wallet version (so the
       caller must ensure the current wallet version is correct before calling
       this function). */
    bool SetHDMasterKey(const CellPubKey& key);
};

/** A key allocated from the key pool. */
class CellReserveKey : public CReserveScript
{
protected:
    CellWallet* pwallet;
    int64_t nIndex;
    CellPubKey vchPubKey;
    bool fInternal;
public:
    CellReserveKey(CellWallet* pwalletIn)
    {
        nIndex = -1;
        pwallet = pwalletIn;
        fInternal = false;
    }

    CellReserveKey() = default;
    CellReserveKey(const CellReserveKey&) = delete;
    CellReserveKey& operator=(const CellReserveKey&) = delete;

    ~CellReserveKey()
    {
        ReturnKey();
    }

    void ReturnKey();
    bool GetReservedKey(CellPubKey &pubkey, bool internal = false);
    void KeepKey();
    void KeepScript() override { KeepKey(); }
};


/** 
 * Account information.
 * Stored in wallet with key "acc"+string account name.
 */
class CellAccount
{
public:
    CellPubKey vchPubKey;

    CellAccount()
    {
        SetNull();
    }

    void SetNull()
    {
        vchPubKey = CellPubKey();
    }

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        int nVersion = s.GetVersion();
        if (!(s.GetType() & SER_GETHASH))
            READWRITE(nVersion);
        READWRITE(vchPubKey);
    }
};

// Helper for producing a bunch of max-sized low-S signatures (eg 72 bytes)
// ContainerType is meant to hold pair<CellWalletTx *, int>, and be iterable
// so that each entry corresponds to each vIn, in order.
template <typename ContainerType>
bool CellWallet::DummySignTx(CellMutableTransaction &txNew, const ContainerType &coins) const
{
    // Fill in dummy signatures for fee calculation.
    int nIn = 0;
    for (const auto& coin : coins)
    {
        const CellScript& scriptPubKey = coin.txout.scriptPubKey;
        SignatureData sigdata;

        if (!ProduceSignature(DummySignatureCreator(this), scriptPubKey, sigdata) && !fFakeWallet)
        {
            return false;
        } else {
            UpdateTransaction(txNew, nIn, sigdata);
        }

        nIn++;
    }
    return true;
}

//for other users, who keep their private keys
class CellFakeWallet :public CellWallet
{
public:
	CellFakeWallet() {
		fFakeWallet = true;
	}

	std::set<CellKeyID> m_ownKeys;
	bool HaveKey(const CellKeyID &address) const override
	{
		bool result;
		{
			result = (m_ownKeys.count(address) > 0);
		}
		return result;
	}
};

class CellCoinsViewCache;
void GetAvailableMortgageCoinsInMemPool(const CellKeyStore& keystore, std::vector<CellOutput>& vecOutput, std::map<uint256, CellWalletTx>& mapTempWallet, CellCoinsViewCache& view);
#endif // CELLLINK_WALLET_WALLET_H
