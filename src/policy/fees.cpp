// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2015 The Bitcoin developers
// Copyright (c) 2021 The DECENOMY Core Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "policy/fees.h"
#include "policy/policy.h"

#include "amount.h"
#include "primitives/transaction.h"
#include "streams.h"
#include "txmempool.h"
#include "util.h"

void TxConfirmStats::Initialize(std::vector<double>& defaultBuckets,
                                unsigned int maxConfirms, double _decay)
{
    decay = _decay;
    for (unsigned int i = 0; i < defaultBuckets.size(); i++) {
        buckets.push_back(defaultBuckets[i]);
        bucketMap[defaultBuckets[i]] = i;
    }
    confAvg.resize(maxConfirms);
    curBlockConf.resize(maxConfirms);
    unconfTxs.resize(maxConfirms);
    for (unsigned int i = 0; i < maxConfirms; i++) {
        confAvg[i].resize(buckets.size());
        curBlockConf[i].resize(buckets.size());
        unconfTxs[i].resize(buckets.size());
    }

    oldUnconfTxs.resize(buckets.size());
    curBlockTxCt.resize(buckets.size());
    txCtAvg.resize(buckets.size());
    curBlockVal.resize(buckets.size());
    avg.resize(buckets.size());
}

// Zero out the data for the current block
void TxConfirmStats::ClearCurrent(unsigned int nBlockHeight)
{
    for (unsigned int j = 0; j < buckets.size(); j++) {
        oldUnconfTxs[j] += unconfTxs[nBlockHeight%unconfTxs.size()][j];
        unconfTxs[nBlockHeight%unconfTxs.size()][j] = 0;
        for (unsigned int i = 0; i < curBlockConf.size(); i++)
            curBlockConf[i][j] = 0;
        curBlockTxCt[j] = 0;
        curBlockVal[j] = 0;
    }
}


void TxConfirmStats::Record(int blocksToConfirm, double val)
{
    // blocksToConfirm is 1-based
    if (blocksToConfirm < 1)
        return;
    unsigned int bucketindex = bucketMap.lower_bound(val)->second;
    for (size_t i = blocksToConfirm; i <= curBlockConf.size(); i++) {
        curBlockConf[i - 1][bucketindex]++;
    }
    curBlockTxCt[bucketindex]++;
    curBlockVal[bucketindex] += val;
}

void TxConfirmStats::UpdateMovingAverages()
{
    for (unsigned int j = 0; j < buckets.size(); j++) {
        for (unsigned int i = 0; i < confAvg.size(); i++)
            confAvg[i][j] = confAvg[i][j] * decay + curBlockConf[i][j];
        avg[j] = avg[j] * decay + curBlockVal[j];
        txCtAvg[j] = txCtAvg[j] * decay + curBlockTxCt[j];
    }
}

// returns -1 on error conditions
double TxConfirmStats::EstimateMedianVal(int confTarget, double sufficientTxVal,
                                         double successBreakPoint, bool requireGreater,
                                         unsigned int nBlockHeight)
{
    // Counters for a bucket (or range of buckets)
    double nConf = 0; // Number of tx's confirmed within the confTarget
    double totalNum = 0; // Total number of tx's that were ever confirmed
    int extraNum = 0;  // Number of tx's still in mempool for confTarget or longer

    int maxbucketindex = buckets.size() - 1;

    // requireGreater means we are looking for the lowest feerate such that all higher
    // values pass, so we start at maxbucketindex (highest feerate) and look at succesively
    // smaller buckets until we reach failure.  Otherwise, we are looking for the highest
    // feerate such that all lower values fail, and we go in the opposite direction.
    unsigned int startbucket = requireGreater ? maxbucketindex : 0;
    int step = requireGreater ? -1 : 1;

    // We'll combine buckets until we have enough samples.
    // The near and far variables will define the range we've combined
    // The best variables are the last range we saw which still had a high
    // enough confirmation rate to count as success.
    // The cur variables are the current range we're counting.
    unsigned int curNearBucket = startbucket;
    unsigned int bestNearBucket = startbucket;
    unsigned int curFarBucket = startbucket;
    unsigned int bestFarBucket = startbucket;

    bool foundAnswer = false;
    unsigned int bins = unconfTxs.size();

    // Start counting from highest(default) or lowest feerate transactions
    for (int bucket = startbucket; bucket >= 0 && bucket <= maxbucketindex; bucket += step) {
        curFarBucket = bucket;
        nConf += confAvg[confTarget - 1][bucket];
        totalNum += txCtAvg[bucket];
        for (unsigned int confct = confTarget; confct < GetMaxConfirms(); confct++)
            extraNum += unconfTxs[(nBlockHeight - confct)%bins][bucket];
        extraNum += oldUnconfTxs[bucket];
        // If we have enough transaction data points in this range of buckets,
        // we can test for success
        // (Only count the confirmed data points, so that each confirmation count
        // will be looking at the same amount of data and same bucket breaks)
        if (totalNum >= sufficientTxVal / (1 - decay)) {
            double curPct = nConf / (totalNum + extraNum);

            // Check to see if we are no longer getting confirmed at the success rate
            if (requireGreater && curPct < successBreakPoint)
                break;
            if (!requireGreater && curPct > successBreakPoint)
                break;

            // Otherwise update the cumulative stats, and the bucket variables
            // and reset the counters
            else {
                foundAnswer = true;
                nConf = 0;
                totalNum = 0;
                extraNum = 0;
                bestNearBucket = curNearBucket;
                bestFarBucket = curFarBucket;
                curNearBucket = bucket + step;
            }
        }
    }

    double median = -1;
    double txSum = 0;

    // Calculate the "average" feerate of the best bucket range that met success conditions
    // Find the bucket with the median transaction and then report the average feerate from that bucket
    // This is a compromise between finding the median which we can't since we don't save all tx's
    // and reporting the average which is less accurate
    unsigned int minBucket = bestNearBucket < bestFarBucket ? bestNearBucket : bestFarBucket;
    unsigned int maxBucket = bestNearBucket > bestFarBucket ? bestNearBucket : bestFarBucket;
    for (unsigned int j = minBucket; j <= maxBucket; j++) {
        txSum += txCtAvg[j];
    }
    if (foundAnswer && txSum != 0) {
        txSum = txSum / 2;
        for (unsigned int j = minBucket; j <= maxBucket; j++) {
            if (txCtAvg[j] < txSum)
                txSum -= txCtAvg[j];
            else { // we're in the right bucket
                median = avg[j] / txCtAvg[j];
                break;
            }
        }
    }

    LogPrint(BCLog::ESTIMATEFEE, "%3d: For conf success %s %4.2f need feerate %s: %12.5g from buckets %8g - %8g  Cur Bucket stats %6.2f%%  %8.1f/(%.1f+%d mempool)\n",
             confTarget, requireGreater ? ">" : "<", successBreakPoint,
             requireGreater ? ">" : "<", median, buckets[minBucket], buckets[maxBucket],
             100 * nConf / (totalNum + extraNum), nConf, totalNum, extraNum);

    return median;
}

void TxConfirmStats::Write(CAutoFile& fileout)
{
    fileout << decay;
    fileout << buckets;
    fileout << avg;
    fileout << txCtAvg;
    fileout << confAvg;
}

void TxConfirmStats::Read(CAutoFile& filein)
{
    // Read data file into temporary variables and do some very basic sanity checking
    std::vector<double> fileBuckets;
    std::vector<double> fileAvg;
    std::vector<std::vector<double> > fileConfAvg;
    std::vector<double> fileTxCtAvg;
    double fileDecay;
    size_t maxConfirms;
    size_t numBuckets;

    filein >> fileDecay;
    if (fileDecay <= 0 || fileDecay >= 1)
        throw std::runtime_error("Corrupt estimates file. Decay must be between 0 and 1 (non-inclusive)");
    filein >> fileBuckets;
    numBuckets = fileBuckets.size();
    if (numBuckets <= 1 || numBuckets > 1000)
        throw std::runtime_error("Corrupt estimates file. Must have between 2 and 1000 feerate buckets");
    filein >> fileAvg;
    if (fileAvg.size() != numBuckets)
        throw std::runtime_error("Corrupt estimates file. Mismatch in feerate average bucket count");
    filein >> fileTxCtAvg;
    if (fileTxCtAvg.size() != numBuckets)
        throw std::runtime_error("Corrupt estimates file. Mismatch in tx count bucket count");
    filein >> fileConfAvg;
    maxConfirms = fileConfAvg.size();
    if (maxConfirms <= 0 || maxConfirms > 6 * 24 * 7) // one week
        throw std::runtime_error("Corrupt estimates file.  Must maintain estimates for between 1 and 1008 (one week) confirms");
    for (unsigned int i = 0; i < maxConfirms; i++) {
        if (fileConfAvg[i].size() != numBuckets)
            throw std::runtime_error("Corrupt estimates file. Mismatch in feerate conf average bucket count");
    }
    // Now that we've processed the entire feerate estimate data file and not
    // thrown any errors, we can copy it to our data structures
    decay = fileDecay;
    buckets = fileBuckets;
    avg = fileAvg;
    confAvg = fileConfAvg;
    txCtAvg = fileTxCtAvg;
    bucketMap.clear();

    // Resize the current block variables which aren't stored in the data file
    // to match the number of confirms and buckets
    curBlockConf.resize(maxConfirms);
    for (unsigned int i = 0; i < maxConfirms; i++) {
        curBlockConf[i].resize(buckets.size());
    }
    curBlockTxCt.resize(buckets.size());
    curBlockVal.resize(buckets.size());

    unconfTxs.resize(maxConfirms);
    for (unsigned int i = 0; i < maxConfirms; i++) {
        unconfTxs[i].resize(buckets.size());
    }
    oldUnconfTxs.resize(buckets.size());

    for (unsigned int i = 0; i < buckets.size(); i++)
        bucketMap[buckets[i]] = i;

    LogPrint(BCLog::ESTIMATEFEE, "Reading estimates: %u buckets counting confirms up to %u blocks\n",
            numBuckets, maxConfirms);
}

unsigned int TxConfirmStats::NewTx(unsigned int nBlockHeight, double val)
{
    unsigned int bucketindex = bucketMap.lower_bound(val)->second;
    unsigned int blockIndex = nBlockHeight % unconfTxs.size();
    unconfTxs[blockIndex][bucketindex]++;
    return bucketindex;
}

void TxConfirmStats::removeTx(unsigned int entryHeight, unsigned int nBestSeenHeight, unsigned int bucketindex)
{
    //nBestSeenHeight is not updated yet for the new block
    int blocksAgo = nBestSeenHeight - entryHeight;
    if (nBestSeenHeight == 0)  // the BlockPolicyEstimator hasn't seen any blocks yet
        blocksAgo = 0;
    if (blocksAgo < 0) {
        LogPrint(BCLog::ESTIMATEFEE, "Blockpolicy error, blocks ago is negative for mempool tx\n");
        return;  //This can't happen because we call this with our best seen height, no entries can have higher
    }

    if (blocksAgo >= (int)unconfTxs.size()) {
        if (oldUnconfTxs[bucketindex] > 0)
            oldUnconfTxs[bucketindex]--;
        else
            LogPrint(BCLog::ESTIMATEFEE, "Blockpolicy error, mempool tx removed from >25 blocks,bucketIndex=%u already\n",
                     bucketindex);
    }
    else {
        unsigned int blockIndex = entryHeight % unconfTxs.size();
        if (unconfTxs[blockIndex][bucketindex] > 0)
            unconfTxs[blockIndex][bucketindex]--;
        else
            LogPrint(BCLog::ESTIMATEFEE, "Blockpolicy error, mempool tx removed from blockIndex=%u,bucketIndex=%u already\n",
                     blockIndex, bucketindex);
    }
}

void CBlockPolicyEstimator::removeTx(uint256 hash)
{
    std::map<uint256, TxStatsInfo>::iterator pos = mapMemPoolTxs.find(hash);
    if (pos == mapMemPoolTxs.end()) {
        LogPrint(BCLog::ESTIMATEFEE, "Blockpolicy error mempool tx %s not found for removeTx\n",
                 hash.ToString().c_str());
        return;
    }
    unsigned int entryHeight = pos->second.blockHeight;
    unsigned int bucketIndex = pos->second.bucketIndex;

    feeStats.removeTx(entryHeight, nBestSeenHeight, bucketIndex);
    mapMemPoolTxs.erase(hash);
}

CBlockPolicyEstimator::CBlockPolicyEstimator(const CFeeRate& _minRelayFee)
    : nBestSeenHeight(0)
{
    minTrackedFee = _minRelayFee < CFeeRate(MIN_FEERATE) ? CFeeRate(MIN_FEERATE) : _minRelayFee;
    std::vector<double> vfeelist;
    for (double bucketBoundary = minTrackedFee.GetFeePerK(); bucketBoundary <= MAX_FEERATE; bucketBoundary *= FEE_SPACING) {
        vfeelist.push_back(bucketBoundary);
    }
    vfeelist.push_back(INF_FEERATE);
    feeStats.Initialize(vfeelist, MAX_BLOCK_CONFIRMS, DEFAULT_DECAY);
}

void CBlockPolicyEstimator::processTransaction(const CTxMemPoolEntry& entry, bool fCurrentEstimate)
{
    if(entry.HasZerocoins()) {
        // Zerocoin spends/mints had fixed feerate. Skip them for the estimates.
        return;
    }

    unsigned int txHeight = entry.GetHeight();
    uint256 hash = entry.GetTx().GetHash();
    if (mapMemPoolTxs.count(hash)) {
        LogPrint(BCLog::ESTIMATEFEE, "Blockpolicy error mempool tx %s already being tracked\n",
                 hash.ToString().c_str());
    return;
    }

    if (txHeight < nBestSeenHeight) {
        // Ignore side chains and re-orgs; assuming they are random they don't
        // affect the estimate.  We'll potentially double count transactions in 1-block reorgs.
        return;
    }

    // Only want to be updating estimates when our blockchain is synced,
    // otherwise we'll miscalculate how many blocks its taking to get included.
    if (!fCurrentEstimate)
        return;

    if (!entry.WasClearAtEntry()) {
        // This transaction depends on other transactions in the mempool to
        // be included in a block before it will be able to be included, so
        // we shouldn't include it in our calculations
        return;
    }

    // Feerates are stored and reported as CAP-per-kb:
    CFeeRate feeRate(entry.GetFee(), entry.GetTxSize());

    mapMemPoolTxs[hash].blockHeight = txHeight;
    mapMemPoolTxs[hash].bucketIndex = feeStats.NewTx(txHeight, (double)feeRate.GetFeePerK());
}

void CBlockPolicyEstimator::processBlockTx(unsigned int nBlockHeight, const CTxMemPoolEntry& entry)
{
    if(entry.HasZerocoins()) {
        // Zerocoin spends/mints had fixed feerate. Skip them for the estimates.
        return;
    }

    if (!entry.WasClearAtEntry()) {
        // This transaction depended on other transactions in the mempool to
        // be included in a block before it was able to be included, so
        // we shouldn't include it in our calculations
        return;
    }

    // How many blocks did it take for miners to include this transaction?
    // blocksToConfirm is 1-based, so a transaction included in the earliest
    // possible block has confirmation count of 1
    int blocksToConfirm = nBlockHeight - entry.GetHeight();
    if (blocksToConfirm <= 0) {
        // This can't happen because we don't process transactions from a block with a height
        // lower than our greatest seen height
        LogPrint(BCLog::ESTIMATEFEE, "Blockpolicy error Transaction had negative blocksToConfirm\n");
        return;
    }

    // Feerates are stored and reported as CAP-per-kb:
    CFeeRate feeRate(entry.GetFee(), entry.GetTxSize());

    feeStats.Record(blocksToConfirm, (double)feeRate.GetFeePerK());
}

void CBlockPolicyEstimator::processBlock(unsigned int nBlockHeight,
                                         std::vector<CTxMemPoolEntry>& entries, bool fCurrentEstimate)
{
    if (nBlockHeight <= nBestSeenHeight) {
        // Ignore side chains and re-orgs; assuming they are random
        // they don't affect the estimate.
        // And if an attacker can re-org the chain at will, then
        // you've got much bigger problems than "attacker can influence
        // transaction fees."
        return;
    }
    nBestSeenHeight = nBlockHeight;

    // Only want to be updating estimates when our blockchain is synced,
    // otherwise we'll miscalculate how many blocks its taking to get included.
    if (!fCurrentEstimate)
        return;

    // Clear the current block state
    feeStats.ClearCurrent(nBlockHeight);

    // Repopulate the current block state
    for (unsigned int i = 0; i < entries.size(); i++)
        processBlockTx(nBlockHeight, entries[i]);

    // Update all exponential averages with the current block state
    feeStats.UpdateMovingAverages();

    LogPrint(BCLog::ESTIMATEFEE, "Blockpolicy after updating estimates for %u confirmed entries, new mempool map size %u\n",
             entries.size(), mapMemPoolTxs.size());
}

CFeeRate CBlockPolicyEstimator::estimateFee(int confTarget)
{
    // Return failure if trying to analyze a target we're not tracking
    if (confTarget <= 0 || (unsigned int)confTarget > feeStats.GetMaxConfirms())
        return CFeeRate(0);

    double median = feeStats.EstimateMedianVal(confTarget, SUFFICIENT_FEETXS, MIN_SUCCESS_PCT, true, nBestSeenHeight);

    if (median < 0)
        return CFeeRate(0);

    return CFeeRate(median);
}

CFeeRate CBlockPolicyEstimator::estimateSmartFee(int confTarget, int *answerFoundAtTarget, const CTxMemPool& pool)
{
    if (answerFoundAtTarget)
        *answerFoundAtTarget = confTarget;
    // Return failure if trying to analyze a target we're not tracking
    if (confTarget <= 0 || (unsigned int)confTarget > feeStats.GetMaxConfirms())
        return CFeeRate(0);

    double median = -1;
    while (median < 0 && (unsigned int)confTarget <= feeStats.GetMaxConfirms()) {
        median = feeStats.EstimateMedianVal(confTarget++, SUFFICIENT_FEETXS, MIN_SUCCESS_PCT, true, nBestSeenHeight);
    }

    if (answerFoundAtTarget)
        *answerFoundAtTarget = confTarget - 1;

    // If mempool is limiting txs , return at least the min feerate from the mempool
    CAmount minPoolFee = pool.GetMinFee(GetArg("-maxmempool", DEFAULT_MAX_MEMPOOL_SIZE) * 1000000).GetFeePerK();
    if (minPoolFee > 0 && minPoolFee > median)
        return CFeeRate(minPoolFee);

    if (median < 0)
        return CFeeRate(0);

    return CFeeRate(median);
}

double CBlockPolicyEstimator::estimatePriority(int confTarget)
{
    return -1;
}

double CBlockPolicyEstimator::estimateSmartPriority(int confTarget, int *answerFoundAtTarget, const CTxMemPool& pool)
{
    if (answerFoundAtTarget)
        *answerFoundAtTarget = confTarget;

    // If mempool is limiting txs, no priority txs are allowed
    CAmount minPoolFee = pool.GetMinFee(GetArg("-maxmempool", DEFAULT_MAX_MEMPOOL_SIZE) * 1000000).GetFeePerK();
    if (minPoolFee > 0)
        return INF_PRIORITY;

    return -1;
}

void CBlockPolicyEstimator::Write(CAutoFile& fileout)
{
    fileout << nBestSeenHeight;
    feeStats.Write(fileout);
}

void CBlockPolicyEstimator::Read(CAutoFile& filein, int nFileVersion)
{
    int nFileBestSeenHeight;
    filein >> nFileBestSeenHeight;
    feeStats.Read(filein);
    nBestSeenHeight = nFileBestSeenHeight;
    if (nFileVersion < 4029900) {
        TxConfirmStats priStats;
        priStats.Read(filein);
    }
}
