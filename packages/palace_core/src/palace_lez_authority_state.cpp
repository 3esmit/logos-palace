#include "palace_lez_authority_state.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <map>
#include <set>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace palace {
namespace {

constexpr std::size_t kMaximumAccounts = 512U;
constexpr std::size_t kMaximumAccountResponseBytes = 256U * 1024U;
constexpr std::size_t kMaximumHistoryActions = 256U;
constexpr std::size_t kMaximumNetworkIdBytes = 128U;
const std::string kSystemProgramOwnerHex(64U, '0');

struct DecodedBundleV1 {
    PalaceLezNamedFinalizedAccountV1 root;
    std::vector<PalaceLezNamedFinalizedAccountV1> children;
};

PalaceLezAuthorityStateUpdateV1 reject(const std::string& reason)
{
    return {false, reason};
}

bool isLowerHex(const std::string& value, const std::size_t size)
{
    return value.size() == size
        && std::all_of(
            value.begin(),
            value.end(),
            [](const unsigned char byte) {
                return (byte >= '0' && byte <= '9')
                    || (byte >= 'a' && byte <= 'f');
            });
}

bool isNonzeroHex64(const std::string& value)
{
    return isLowerHex(value, 64U)
        && value != std::string(64U, '0');
}

bool validScope(const PalaceLezAuthorityBundleScopeV1& scope)
{
    return !scope.networkId.empty()
        && scope.networkId.size() <= kMaximumNetworkIdBytes
        && std::all_of(
            scope.networkId.begin(),
            scope.networkId.end(),
            [](const unsigned char byte) {
                return byte >= 0x21U && byte <= 0x7eU;
            })
        && isNonzeroHex64(scope.programIdHex)
        && isNonzeroHex64(scope.rootAccountIdHex)
        && PalaceLezCodec::deriveRootPda(scope.programIdHex)
            == scope.rootAccountIdHex;
}

bool sameScope(
    const PalaceLezAuthorityBundleScopeV1& left,
    const PalaceLezAuthorityBundleScopeV1& right)
{
    return left.networkId == right.networkId
        && left.programIdHex == right.programIdHex
        && left.rootAccountIdHex == right.rootAccountIdHex;
}

bool validCheckpoint(
    const PalaceLezAuthorityBundleCheckpointV1& checkpoint)
{
    return checkpoint.finalizedBlockId > 0U
        && checkpoint.finalizedBlockId
            == checkpoint.finalizedBlockHeight
        && checkpoint.finalizedBlockHeight
            <= static_cast<std::uint64_t>(
                std::numeric_limits<std::int64_t>::max())
        && isNonzeroHex64(checkpoint.finalizedBlockHashHex);
}

bool sameCheckpoint(
    const PalaceLezAuthorityBundleCheckpointV1& left,
    const PalaceLezAuthorityBundleCheckpointV1& right)
{
    return left.finalizedBlockId == right.finalizedBlockId
        && left.finalizedBlockHeight == right.finalizedBlockHeight
        && left.finalizedBlockHashHex == right.finalizedBlockHashHex
        && left.lastOrderedActionId == right.lastOrderedActionId;
}

bool validExpectation(
    const PalaceLezAuthorityBundleExpectationV1& expectation)
{
    return validScope(expectation.scope)
        && (!expectation.checkpoint.has_value()
            || validCheckpoint(*expectation.checkpoint));
}

std::string accountIdBase58(const std::string& accountIdHex)
{
    PalaceLezBytes32 bytes{};
    if (!PalaceLezCodec::parseBytes32Hex(accountIdHex, bytes))
        return {};
    return PalaceLezCodec::accountIdBase58(bytes);
}

std::uint64_t instructionActionId(
    const PalaceLezInstructionV3& instruction)
{
    return std::visit(
        [](const auto& value) -> std::uint64_t {
            using Value = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<Value, PalaceLezInitializeV3>)
                return 0U;
            else
                return value.orderedActionId;
        },
        instruction.payload);
}

bool isInitialize(const PalaceLezInstructionV3& instruction)
{
    return std::holds_alternative<PalaceLezInitializeV3>(
        instruction.payload);
}

bool samePlan(
    const PalaceLezTransactionPlanV3& left,
    const PalaceLezTransactionPlanV3& right)
{
    return left.accepted && right.accepted
        && left.programIdHex == right.programIdHex
        && left.rootAccountIdHex == right.rootAccountIdHex
        && left.accountIdsHex == right.accountIdsHex
        && left.signingRequirements == right.signingRequirements
        && left.instructionWords == right.instructionWords;
}

PalaceLezAuthorityStateUpdateV1 validatePlan(
    const PalaceLezTransactionPlanV3& plan,
    const std::uint64_t orderedActionId)
{
    if (!plan.accepted || plan.accountIdsHex.size() < 2U
        || plan.accountIdsHex.size() > kMaximumAccounts
        || plan.accountIdsHex.size()
            != plan.signingRequirements.size()
        || plan.accountIdsHex.front() != plan.rootAccountIdHex
        || !isNonzeroHex64(plan.programIdHex)
        || !isNonzeroHex64(plan.rootAccountIdHex)) {
        return reject("invalid-transaction-plan");
    }

    const PalaceLezWireInstruction decoded =
        PalaceLezCodec::decodeInstruction(plan.instructionWords);
    const PalaceLezWireInstruction embedded =
        PalaceLezCodec::encodeInstruction(plan.instruction);
    if (!decoded.accepted || !embedded.accepted
        || embedded.words != plan.instructionWords
        || instructionActionId(decoded.instruction)
            != orderedActionId) {
        return reject("invalid-transaction-plan");
    }
    const PalaceLezTransactionPlanV3 rebuilt =
        PalaceLezCodec::buildTransaction(
            plan.programIdHex,
            plan.accountIdsHex[1],
            decoded.instruction);
    if (!samePlan(plan, rebuilt))
        return reject("invalid-transaction-plan");

    std::set<std::string> accountIds;
    for (const std::string& accountId : plan.accountIdsHex) {
        if (!isNonzeroHex64(accountId)
            || !accountIds.insert(accountId).second) {
            return reject("invalid-or-duplicate-account-id");
        }
    }
    return {true, "accepted"};
}

PalaceLezAuthorityStateUpdateV1 decodeBundle(
    const PalaceLezFinalizedAuthorityBundleV1& bundle,
    const PalaceLezAuthorityBundleExpectationV1& expectation,
    DecodedBundleV1& decoded)
{
    if (!validExpectation(expectation)
        || !validScope(bundle.scope)) {
        return reject("invalid-authority-scope");
    }
    if (!sameScope(bundle.scope, expectation.scope))
        return reject("authority-scope-mismatch");
    if (!validCheckpoint(bundle.checkpoint))
        return reject("invalid-authority-checkpoint");
    if (expectation.checkpoint.has_value()
        && !sameCheckpoint(
            bundle.checkpoint, *expectation.checkpoint)) {
        return reject("authority-checkpoint-mismatch");
    }
    if (bundle.accounts.empty()
        || bundle.accounts.size() > kMaximumAccounts) {
        return reject("invalid-authority-account-count");
    }

    DecodedBundleV1 candidate;
    bool foundRoot = false;
    std::set<std::string> accountIds;
    for (const PalaceLezFinalizedAuthorityAccountV1& stored :
         bundle.accounts) {
        if (!isNonzeroHex64(stored.accountIdHex)
            || stored.responseJson.empty()
            || stored.responseJson.size()
                > kMaximumAccountResponseBytes
            || !accountIds.insert(stored.accountIdHex).second) {
            return reject("invalid-or-duplicate-authority-account");
        }
        PalaceLezNamedFinalizedAccountV1 named;
        named.accountIdHex = stored.accountIdHex;
        named.account = PalaceLezCodec::decodePublicAccount(
            stored.responseJson, bundle.scope.programIdHex);
        if (!named.account.accepted)
            return reject("invalid-authority-account:" + named.account.reason);

        if (stored.accountIdHex
            == bundle.scope.rootAccountIdHex) {
            if (foundRoot
                || named.account.recordType
                    != PalaceLezRecordTypeV3::PalaceRoot) {
                return reject("invalid-authority-root");
            }
            candidate.root = std::move(named);
            foundRoot = true;
        } else {
            if (named.account.recordType
                == PalaceLezRecordTypeV3::PalaceRoot) {
                return reject("unexpected-authority-root");
            }
            candidate.children.push_back(std::move(named));
        }
    }
    if (!foundRoot)
        return reject("missing-authority-root");
    const auto* root = std::get_if<PalaceLezRootRecordV3>(
        &candidate.root.account.record);
    if (root == nullptr
        || root->lastOrderedActionId
            != bundle.checkpoint.lastOrderedActionId) {
        return reject("authority-root-checkpoint-mismatch");
    }

    const PalaceLezAuthorityProjectionResultV1 projected =
        projectFinalizedLezAuthorityV1(
            candidate.root, candidate.children);
    if (!projected.accepted)
        return reject("authority-projection-rejected:" + projected.reason);

    decoded = std::move(candidate);
    return {true, "accepted"};
}

PalaceLezAuthorityStateUpdateV1 buildProjection(
    const PalaceLezFinalizedAuthorityBundleV1& bundle,
    const PalaceLezAuthorityBundleExpectationV1& expectation,
    AuthorityProjection& projection)
{
    DecodedBundleV1 decoded;
    const PalaceLezAuthorityStateUpdateV1 validation =
        decodeBundle(bundle, expectation, decoded);
    if (!validation.accepted)
        return validation;

    const PalaceLezAuthorityProjectionResultV1 projected =
        projectFinalizedLezAuthorityV1(
            decoded.root, decoded.children);
    if (!projected.accepted)
        return reject("authority-projection-rejected:" + projected.reason);
    if (!projection.replaceFinalized(
            projected.snapshot,
            static_cast<std::int64_t>(
                bundle.checkpoint.finalizedBlockHeight))) {
        return reject("authority-snapshot-rejected");
    }
    return {true, "accepted"};
}

PalaceLezAuthorityStateUpdateV1 validateTrackedTransaction(
    const PalaceLezTrackedTransaction& transaction)
{
    if (transaction.stage != PalaceLezTransactionStage::Finalized
        || transaction.observedBlockHeight == 0U
        || !isNonzeroHex64(transaction.transactionHash)
        || !isNonzeroHex64(
            transaction.expectedRootDataSha256Hex)) {
        return reject("transaction-not-finalized");
    }
    return validatePlan(transaction.plan, transaction.orderedActionId);
}

PalaceLezAuthorityStateUpdateV1 validateCertificate(
    const PalaceLezTrackedTransaction& transaction,
    const PalaceLezExplorerFinalityCertificateV1& certificate)
{
    const PalaceLezTransactionPlanV3& plan = transaction.plan;
    if (certificate.certificateVersion() != 1U
        || certificate.transactionHashHex()
            != transaction.transactionHash
        || certificate.programIdBase58()
            != accountIdBase58(plan.programIdHex)
        || certificate.accountIdsBase58().size()
            != plan.accountIdsHex.size()
        || certificate.accountEvidence().size()
            != plan.accountIdsHex.size()
        || certificate.instructionWords()
            != plan.instructionWords
        || certificate.instructionWordsSha256Hex()
            != PalaceLezExplorerFinalitySession::
                instructionWordsSha256Hex(
                    plan.instructionWords)
        || certificate.finalizedBlockId() == 0U
        || certificate.finalizedBlockId()
            != certificate.finalizedBlockHeight()
        || certificate.finalizedBlockHeight()
            > transaction.observedBlockHeight
        || certificate.finalizedBlockHeight()
            > static_cast<std::uint64_t>(
                std::numeric_limits<std::int64_t>::max())
        || !isNonzeroHex64(
            certificate.finalizedBlockHashHex())) {
        return reject("finality-certificate-mismatch");
    }
    for (std::size_t index = 0U;
         index < plan.accountIdsHex.size();
         ++index) {
        const std::string expectedId =
            accountIdBase58(plan.accountIdsHex[index]);
        if (expectedId.empty()
            || certificate.accountIdsBase58()[index]
                != expectedId
            || certificate.accountEvidence()[index]
                    .accountIdBase58
                != expectedId
            || certificate.accountEvidence()[index]
                    .programOwnerBase58.empty()
            || !isNonzeroHex64(
                certificate.accountEvidence()[index]
                    .dataSha256Hex)) {
            return reject("finality-certificate-account-mismatch");
        }
    }
    if (certificate.accountEvidence().front().dataSha256Hex
        != transaction.expectedRootDataSha256Hex) {
        return reject("finality-certificate-root-mismatch");
    }
    return {true, "accepted"};
}

PalaceLezAuthorityStateUpdateV1 validateStableBatch(
    const PalaceLezStableAccountBatchV1& stableAccounts,
    const std::uint64_t minimumFinalizedHeight,
    const std::size_t expectedAccounts)
{
    if (stableAccounts.heightBefore <= 0
        || stableAccounts.heightAfter
            != stableAccounts.heightBefore
        || static_cast<std::uint64_t>(
            stableAccounts.heightBefore)
            < minimumFinalizedHeight) {
        return reject("unstable-authority-account-batch");
    }
    if (stableAccounts.accountResponseJson.size()
        != expectedAccounts) {
        return reject("authority-account-count-mismatch");
    }
    return {true, "accepted"};
}

PalaceLezAuthorityStateUpdateV1 validateHistory(
    const PalaceLezExplorerHistoryResultV1& history,
    const PalaceLezAuthorityBundleScopeV1& scope,
    std::set<std::string>& programAccounts,
    std::set<std::string>& signerAccounts)
{
    if (history.resultVersion != 1U
        || history.actions.empty()
        || history.actions.size() > kMaximumHistoryActions
        || history.uniqueAccountIdsHex.empty()
        || history.uniqueAccountIdsHex.size() > kMaximumAccounts
        || history.latestFinalizedBlockId == 0U
        || !isNonzeroHex64(
            history.latestFinalizedBlockHashHex)) {
        return reject("invalid-finalized-history");
    }

    std::set<std::string> transactionHashes;
    std::set<std::string> uniqueAccounts;
    std::vector<std::string> expectedUniqueAccounts;
    std::uint64_t expectedActionId = 0U;
    for (std::size_t actionIndex = 0U;
         actionIndex < history.actions.size();
         ++actionIndex) {
        const PalaceLezFinalizedActionV3& action =
            history.actions[actionIndex];
        if (action.orderedActionId != expectedActionId
            || (actionIndex == 0U
                && !isInitialize(action.instruction))
            || (actionIndex != 0U
                && isInitialize(action.instruction))
            || !isNonzeroHex64(action.transactionHash)
            || !transactionHashes.insert(
                    action.transactionHash).second) {
            return reject("non-chronological-finalized-history");
        }
        const PalaceLezTransactionPlanV3 plan =
            PalaceLezCodec::buildTransaction(
                scope.programIdHex,
                action.accountIdsHex.size() > 1U
                    ? action.accountIdsHex[1]
                    : std::string{},
                action.instruction);
        const PalaceLezAuthorityStateUpdateV1 planValidation =
            validatePlan(plan, action.orderedActionId);
        if (!planValidation.accepted
            || plan.accountIdsHex != action.accountIdsHex
            || plan.rootAccountIdHex
                != scope.rootAccountIdHex) {
            return reject("finalized-history-plan-mismatch");
        }
        for (std::size_t index = 0U;
             index < plan.accountIdsHex.size();
             ++index) {
            const std::string& accountId =
                plan.accountIdsHex[index];
            if (uniqueAccounts.insert(accountId).second)
                expectedUniqueAccounts.push_back(accountId);
            if (plan.signingRequirements[index])
                signerAccounts.insert(accountId);
            else
                programAccounts.insert(accountId);
        }
        if (actionIndex + 1U < history.actions.size()) {
            if (expectedActionId
                == std::numeric_limits<std::uint64_t>::max()) {
                return reject("non-chronological-finalized-history");
            }
            ++expectedActionId;
        }
    }
    if (expectedUniqueAccounts
        != history.uniqueAccountIdsHex) {
        return reject("finalized-history-account-order-mismatch");
    }
    return {true, "accepted"};
}

} // namespace

PalaceLezAuthorityStateUpdateV1
restoreFinalizedLezAuthorityStateV1(
    AuthorityProjection& destination,
    const PalaceLezFinalizedAuthorityBundleV1& bundle,
    const PalaceLezAuthorityBundleExpectationV1& expectation)
{
    AuthorityProjection candidate;
    const PalaceLezAuthorityStateUpdateV1 result =
        buildProjection(bundle, expectation, candidate);
    if (!result.accepted)
        return result;
    destination = std::move(candidate);
    return {true, "accepted"};
}

PalaceLezAuthorityStateUpdateV1
applyFinalizedLezAuthorityActionV1(
    AuthorityProjection& destination,
    PalaceLezFinalizedAuthorityBundleV1& bundle,
    const PalaceLezAuthorityBundleExpectationV1& priorExpectation,
    const PalaceLezTrackedTransaction& finalizedTransaction,
    const PalaceLezExplorerFinalityCertificateV1& certificate,
    const PalaceLezStableAccountBatchV1& stableAccounts)
{
    const PalaceLezAuthorityStateUpdateV1 tracked =
        validateTrackedTransaction(finalizedTransaction);
    if (!tracked.accepted)
        return tracked;
    const PalaceLezTransactionPlanV3& plan =
        finalizedTransaction.plan;
    if (!validExpectation(priorExpectation)
        || !validScope(bundle.scope)
        || !sameScope(bundle.scope, priorExpectation.scope)
        || plan.programIdHex != bundle.scope.programIdHex
        || plan.rootAccountIdHex
            != bundle.scope.rootAccountIdHex) {
        return reject("authority-action-scope-mismatch");
    }

    const bool initializeAction =
        finalizedTransaction.orderedActionId == 0U
        && isInitialize(plan.instruction);
    if (initializeAction) {
        if (priorExpectation.checkpoint.has_value()
            || !bundle.accounts.empty()
            || bundle.checkpoint.finalizedBlockId != 0U
            || bundle.checkpoint.finalizedBlockHeight != 0U
            || !bundle.checkpoint.finalizedBlockHashHex.empty()
            || bundle.checkpoint.lastOrderedActionId != 0U) {
            return reject("invalid-genesis-authority-bundle");
        }
    } else {
        if (!priorExpectation.checkpoint.has_value())
            return reject("authority-checkpoint-required");
        AuthorityProjection priorProjection;
        const PalaceLezAuthorityStateUpdateV1 prior =
            buildProjection(
                bundle, priorExpectation, priorProjection);
        if (!prior.accepted)
            return prior;
        if (bundle.checkpoint.lastOrderedActionId
                == std::numeric_limits<std::uint64_t>::max()
            || finalizedTransaction.orderedActionId
                != bundle.checkpoint.lastOrderedActionId + 1U) {
            return reject("authority-action-sequence-mismatch");
        }
    }
    if ((finalizedTransaction.orderedActionId == 0U)
        != initializeAction) {
        return reject("authority-action-sequence-mismatch");
    }

    const PalaceLezAuthorityStateUpdateV1 certified =
        validateCertificate(finalizedTransaction, certificate);
    if (!certified.accepted)
        return certified;
    const PalaceLezAuthorityStateUpdateV1 stable =
        validateStableBatch(
            stableAccounts,
            certificate.finalizedBlockHeight(),
            plan.accountIdsHex.size());
    if (!stable.accepted)
        return stable;
    if (!initializeAction
        && (certificate.finalizedBlockId()
                < bundle.checkpoint.finalizedBlockId
            || (certificate.finalizedBlockId()
                    == bundle.checkpoint.finalizedBlockId
                && certificate.finalizedBlockHashHex()
                    != bundle.checkpoint.finalizedBlockHashHex))) {
        return reject("authority-checkpoint-regression");
    }

    PalaceLezFinalizedAuthorityBundleV1 candidate = bundle;
    std::map<std::string, std::size_t> storedAccounts;
    for (std::size_t index = 0U;
         index < candidate.accounts.size();
         ++index) {
        storedAccounts.emplace(
            candidate.accounts[index].accountIdHex, index);
    }

    const std::string programOwnerBase58 =
        accountIdBase58(plan.programIdHex);
    for (std::size_t index = 0U;
         index < plan.accountIdsHex.size();
         ++index) {
        const std::string& response =
            stableAccounts.accountResponseJson[index];
        if (response.empty()
            || response.size() > kMaximumAccountResponseBytes) {
            return reject("invalid-authority-account-response");
        }
        const PalaceLezRawAccountV1 raw =
            PalaceLezCodec::parsePublicAccountSnapshot(response);
        if (!raw.accepted)
            return reject("invalid-authority-account:" + raw.reason);

        const std::string ownerBase58 =
            accountIdBase58(raw.programOwnerHex);
        const PalaceLezExplorerFinalityAccountEvidenceV1& evidence =
            certificate.accountEvidence()[index];
        if (ownerBase58.empty()
            || evidence.programOwnerBase58 != ownerBase58
            || evidence.dataSha256Hex != raw.dataSha256Hex) {
            return reject("authority-account-certificate-mismatch");
        }
        if (raw.programOwnerHex != plan.programIdHex) {
            if (!plan.signingRequirements[index]
                || raw.programOwnerHex
                    != kSystemProgramOwnerHex) {
                return reject("authority-account-owner-mismatch");
            }
            continue;
        }
        if (ownerBase58 != programOwnerBase58) {
            return reject("authority-account-owner-mismatch");
        }
        const PalaceLezPublicAccountV3 decoded =
            PalaceLezCodec::decodePublicAccount(
                response, plan.programIdHex);
        if (!decoded.accepted)
            return reject("invalid-authority-account:" + decoded.reason);

        const auto stored = storedAccounts.find(
            plan.accountIdsHex[index]);
        if (stored == storedAccounts.end()) {
            if (candidate.accounts.size() >= kMaximumAccounts)
                return reject("authority-account-capacity-exceeded");
            storedAccounts.emplace(
                plan.accountIdsHex[index],
                candidate.accounts.size());
            candidate.accounts.push_back(
                {plan.accountIdsHex[index], response});
        } else {
            candidate.accounts[stored->second].responseJson =
                response;
        }
    }

    candidate.checkpoint = {
        certificate.finalizedBlockId(),
        certificate.finalizedBlockHeight(),
        certificate.finalizedBlockHashHex(),
        finalizedTransaction.orderedActionId,
    };
    PalaceLezAuthorityBundleExpectationV1 candidateExpectation;
    candidateExpectation.scope = candidate.scope;
    candidateExpectation.checkpoint = candidate.checkpoint;
    AuthorityProjection candidateProjection;
    const PalaceLezAuthorityStateUpdateV1 projected =
        buildProjection(
            candidate,
            candidateExpectation,
            candidateProjection);
    if (!projected.accepted)
        return projected;

    bundle = std::move(candidate);
    destination = std::move(candidateProjection);
    return {true, "accepted"};
}

PalaceLezAuthorityStateUpdateV1
rebuildFinalizedLezAuthorityStateV1(
    AuthorityProjection& destination,
    PalaceLezFinalizedAuthorityBundleV1& bundle,
    const PalaceLezAuthorityBundleScopeV1& scope,
    const PalaceLezExplorerHistoryResultV1& history,
    const PalaceLezStableAccountBatchV1& stableAccounts,
    const PalaceLezAuthorityBundleCheckpointV1& checkpoint)
{
    if (!validScope(scope))
        return reject("invalid-authority-scope");
    if (!validCheckpoint(checkpoint)
        || checkpoint.finalizedBlockId
            != history.latestFinalizedBlockId
        || checkpoint.finalizedBlockHashHex
            != history.latestFinalizedBlockHashHex) {
        return reject("finalized-history-checkpoint-mismatch");
    }

    std::set<std::string> programAccounts;
    std::set<std::string> signerAccounts;
    const PalaceLezAuthorityStateUpdateV1 historyValidation =
        validateHistory(
            history, scope, programAccounts, signerAccounts);
    if (!historyValidation.accepted)
        return historyValidation;
    if (checkpoint.lastOrderedActionId
        != history.actions.back().orderedActionId) {
        return reject("finalized-history-checkpoint-mismatch");
    }
    const PalaceLezAuthorityStateUpdateV1 stable =
        validateStableBatch(
            stableAccounts,
            checkpoint.finalizedBlockHeight,
            history.uniqueAccountIdsHex.size());
    if (!stable.accepted)
        return stable;

    PalaceLezFinalizedAuthorityBundleV1 candidate;
    candidate.scope = scope;
    candidate.checkpoint = checkpoint;
    candidate.accounts.reserve(programAccounts.size());
    for (std::size_t index = 0U;
         index < history.uniqueAccountIdsHex.size();
         ++index) {
        const std::string& accountId =
            history.uniqueAccountIdsHex[index];
        const std::string& response =
            stableAccounts.accountResponseJson[index];
        if (response.empty()
            || response.size() > kMaximumAccountResponseBytes) {
            return reject("invalid-authority-account-response");
        }
        const PalaceLezRawAccountV1 raw =
            PalaceLezCodec::parsePublicAccountSnapshot(response);
        if (!raw.accepted)
            return reject("invalid-authority-account:" + raw.reason);
        if (raw.programOwnerHex != scope.programIdHex) {
            if (programAccounts.count(accountId) != 0U
                || signerAccounts.count(accountId) == 0U
                || raw.programOwnerHex
                    != kSystemProgramOwnerHex) {
                return reject("authority-account-owner-mismatch");
            }
            continue;
        }
        const PalaceLezPublicAccountV3 decoded =
            PalaceLezCodec::decodePublicAccount(
                response, scope.programIdHex);
        if (!decoded.accepted)
            return reject("invalid-authority-account:" + decoded.reason);
        candidate.accounts.push_back({accountId, response});
    }

    PalaceLezAuthorityBundleExpectationV1 expectation;
    expectation.scope = scope;
    expectation.checkpoint = checkpoint;
    AuthorityProjection candidateProjection;
    const PalaceLezAuthorityStateUpdateV1 projected =
        buildProjection(
            candidate, expectation, candidateProjection);
    if (!projected.accepted)
        return projected;

    bundle = std::move(candidate);
    destination = std::move(candidateProjection);
    return {true, "accepted"};
}

} // namespace palace
