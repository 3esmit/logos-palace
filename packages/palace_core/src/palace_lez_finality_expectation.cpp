#include "palace_lez_finality_expectation.h"

#include <algorithm>
#include <set>

namespace palace {
namespace {

PalaceLezFinalityExpectationBuildResultV1 reject(
    const std::string& reason)
{
    return {false, reason, {}};
}

bool nonzero(const PalaceLezBytes32& value)
{
    return std::any_of(
        value.begin(),
        value.end(),
        [](const std::uint8_t byte) { return byte != 0U; });
}

std::string base58FromHex(
    const std::string& value,
    const bool requireNonzero)
{
    PalaceLezBytes32 bytes{};
    if (!PalaceLezCodec::parseBytes32Hex(value, bytes)
        || (requireNonzero && !nonzero(bytes))) {
        return {};
    }
    return PalaceLezCodec::accountIdBase58(bytes);
}

} // namespace

PalaceLezFinalityExpectationBuildResultV1
buildPalaceLezFinalityExpectationV1(
    const PalaceLezTrackedTransaction& observedTransaction,
    const PalaceLezStableAccountBatchV1& stableAccounts)
{
    if (observedTransaction.stage != PalaceLezTransactionStage::Observed
        || observedTransaction.observedBlockHeight == 0U) {
        return reject("transaction-not-observed");
    }
    if (stableAccounts.heightBefore < 0
        || stableAccounts.heightAfter < 0
        || stableAccounts.heightBefore != stableAccounts.heightAfter
        || static_cast<std::uint64_t>(stableAccounts.heightBefore)
            != observedTransaction.observedBlockHeight) {
        return reject("unstable-account-snapshot");
    }

    const PalaceLezTransactionPlanV3& plan =
        observedTransaction.plan;
    PalaceLezBytes32 transactionHash{};
    if (!plan.accepted
        || plan.accountIdsHex.empty()
        || plan.accountIdsHex.size()
            != plan.signingRequirements.size()
        || plan.accountIdsHex.size()
            != stableAccounts.accountResponseJson.size()
        || !PalaceLezCodec::parseBytes32Hex(
            observedTransaction.transactionHash, transactionHash)
        || !nonzero(transactionHash)) {
        return reject("invalid-observed-transaction");
    }

    PalaceLezExplorerTransactionExpectationV1 expectation;
    expectation.transactionHashHex =
        observedTransaction.transactionHash;
    expectation.programIdBase58 =
        base58FromHex(plan.programIdHex, true);
    expectation.instructionWords = plan.instructionWords;
    expectation.instructionWordsSha256Hex =
        PalaceLezExplorerFinalitySession::
            instructionWordsSha256Hex(plan.instructionWords);
    // The bounded explorer scanner supplies the recent-history limit. Zero
    // avoids claiming an inclusion boundary that module account reads cannot
    // prove.
    expectation.baselineBlockId = 0U;
    if (expectation.programIdBase58.empty()
        || expectation.instructionWords.empty()
        || expectation.instructionWordsSha256Hex.empty()) {
        return reject("invalid-observed-plan");
    }

    std::set<std::string> distinctAccounts;
    expectation.accountIdsBase58.reserve(
        plan.accountIdsHex.size());
    expectation.accountExpectations.reserve(
        plan.accountIdsHex.size());
    for (std::size_t index = 0U;
         index < plan.accountIdsHex.size();
         ++index) {
        const std::string accountIdBase58 =
            base58FromHex(plan.accountIdsHex[index], true);
        if (accountIdBase58.empty()
            || !distinctAccounts.insert(accountIdBase58).second) {
            return reject("invalid-or-duplicate-account-id");
        }
        const PalaceLezRawAccountV1 account =
            PalaceLezCodec::parsePublicAccountSnapshot(
                stableAccounts.accountResponseJson[index]);
        if (!account.accepted)
            return reject("invalid-account-snapshot:" + account.reason);
        if (!plan.signingRequirements[index]
            && account.programOwnerHex != plan.programIdHex) {
            return reject("account-program-owner-mismatch");
        }
        const std::string ownerBase58 =
            base58FromHex(account.programOwnerHex, false);
        if (ownerBase58.empty())
            return reject("invalid-account-program-owner");
        if (index == 0U
            && account.dataSha256Hex
                != observedTransaction.expectedRootDataSha256Hex) {
            return reject("root-post-state-mismatch");
        }

        expectation.accountIdsBase58.push_back(
            accountIdBase58);
        expectation.accountExpectations.push_back({
            accountIdBase58,
            ownerBase58,
            account.data,
            account.dataSha256Hex,
        });
    }

    return {true, "accepted", std::move(expectation)};
}

} // namespace palace
