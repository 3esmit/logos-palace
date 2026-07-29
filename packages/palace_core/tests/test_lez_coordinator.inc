LOGOS_TEST(palace_lez_coordinator_requires_exact_network_fingerprint) {
    using namespace palace;
    using namespace palace_lez_test;
    PalaceLezTransactionCoordinator coordinator;
    LOGOS_ASSERT_FALSE(coordinator.activate());
    LOGOS_ASSERT_EQ(
        static_cast<std::uint8_t>(coordinator.compatibilityState()),
        static_cast<std::uint8_t>(PalaceLezCompatibilityState::Unknown));

    PalaceLezNetworkFingerprint required = fingerprint();
    PalaceLezNetworkFingerprint observed = required;
    observed.runtimeRevision =
        "787a15a000000000000000000000000000000000";
    const PalaceLezCompatibilityResult mismatch =
        coordinator.configureNetworkFingerprint(required, observed);
    LOGOS_ASSERT_FALSE(mismatch.accepted);
    LOGOS_ASSERT_EQ(mismatch.reason, std::string("network-fingerprint-mismatch"));
    LOGOS_ASSERT_FALSE(coordinator.activate());

    const PalaceLezCompatibilityResult accepted =
        coordinator.configureNetworkFingerprint(required, required);
    LOGOS_ASSERT_TRUE(accepted.accepted);
    LOGOS_ASSERT_TRUE(coordinator.activate());
}

LOGOS_TEST(palace_lez_coordinator_never_observes_across_height_race_or_state_mismatch) {
    using namespace palace;
    using namespace palace_lez_test;
    PalaceLezTransactionCoordinator coordinator;
    makeCompatible(coordinator);
    const PalaceLezInstructionV3 instruction{
        PalaceLezRevokeCapabilityV3{7U, bytes(0x12U)}};
    const PalaceLezTransactionPlanV3 plan =
        PalaceLezCodec::buildTransaction(kProgramId, kSigner, instruction);
    const std::string transactionHash = hash(40U);
    LOGOS_ASSERT_TRUE(coordinator.registerSubmission(
        plan,
        submissionJson(transactionHash),
        kRootBorshSha256).accepted);

    const PalaceLezCoordinatorUpdate raced = coordinator.observeStableRoot(
        transactionHash,
        10,
        rootAccountJson(),
        11);
    LOGOS_ASSERT_FALSE(raced.accepted);
    LOGOS_ASSERT_EQ(raced.reason, std::string("unstable-height"));
    LOGOS_ASSERT_EQ(
        static_cast<std::uint8_t>(coordinator.transactions().front().stage),
        static_cast<std::uint8_t>(PalaceLezTransactionStage::Submitted));

    std::string altered = rootAccountJson();
    const std::size_t data = altered.find(kRootBorsh);
    LOGOS_ASSERT_TRUE(data != std::string::npos);
    altered.replace(data + 10U, 2U, "11");
    const PalaceLezCoordinatorUpdate mismatched = coordinator.observeStableRoot(
        transactionHash,
        11,
        altered,
        11);
    LOGOS_ASSERT_FALSE(mismatched.accepted);
    LOGOS_ASSERT_EQ(mismatched.reason, std::string("observation-mismatch"));

    const PalaceLezCoordinatorUpdate observed = coordinator.observeStableRoot(
        transactionHash,
        12,
        rootAccountJson(),
        12);
    LOGOS_ASSERT_TRUE(observed.accepted);
    LOGOS_ASSERT_TRUE(observed.changed);
    LOGOS_ASSERT_EQ(
        static_cast<std::uint8_t>(observed.stage),
        static_cast<std::uint8_t>(PalaceLezTransactionStage::Observed));
}

LOGOS_TEST(palace_lez_coordinator_recovered_submission_is_exact_and_idempotent) {
    using namespace palace;
    using namespace palace_lez_test;
    PalaceLezTransactionCoordinator coordinator;
    makeCompatible(coordinator);
    const PalaceLezInstructionV3 instruction{
        PalaceLezRevokeCapabilityV3{7U, bytes(0x12U)}};
    const PalaceLezTransactionPlanV3 plan =
        PalaceLezCodec::buildTransaction(
            kProgramId, kSigner, instruction);
    const std::string transactionHash = hash(45U);

    const PalaceLezCoordinatorUpdate recovered =
        coordinator.registerRecoveredSubmission(
            plan, transactionHash, kRootBorshSha256);
    LOGOS_ASSERT_TRUE(recovered.accepted);
    LOGOS_ASSERT_TRUE(recovered.changed);
    const PalaceLezCoordinatorUpdate repeated =
        coordinator.registerRecoveredSubmission(
            plan, transactionHash, kRootBorshSha256);
    LOGOS_ASSERT_TRUE(repeated.accepted);
    LOGOS_ASSERT_FALSE(repeated.changed);

    PalaceLezTransactionPlanV3 drifted = plan;
    drifted.instructionWords.back() ^= 1U;
    LOGOS_ASSERT_FALSE(
        coordinator.registerRecoveredSubmission(
            drifted, transactionHash, kRootBorshSha256).accepted);
}

LOGOS_TEST(palace_lez_coordinator_requires_observation_then_exact_indexed_hash) {
    using namespace palace;
    using namespace palace_lez_test;
    PalaceLezTransactionCoordinator coordinator;
    makeCompatible(coordinator);
    const PalaceLezInstructionV3 instruction{
        PalaceLezRevokeCapabilityV3{7U, bytes(0x12U)}};
    const PalaceLezTransactionPlanV3 plan =
        PalaceLezCodec::buildTransaction(kProgramId, kSigner, instruction);
    const std::string transactionHash = hash(50U);
    LOGOS_ASSERT_TRUE(coordinator.registerSubmission(
        plan,
        submissionJson(transactionHash),
        kRootBorshSha256).accepted);

    const PalaceLezCoordinatorUpdate premature = coordinator.reconcileFinality(
        transactionHash,
        indexerJson({{plan, transactionHash}}));
    LOGOS_ASSERT_FALSE(premature.accepted);
    LOGOS_ASSERT_EQ(premature.reason, std::string("observation-required"));

    LOGOS_ASSERT_TRUE(coordinator.observeStableRoot(
        transactionHash,
        20,
        rootAccountJson(),
        20).accepted);
    const PalaceLezCoordinatorUpdate lag =
        coordinator.reconcileFinality(transactionHash, "[]");
    LOGOS_ASSERT_TRUE(lag.accepted);
    LOGOS_ASSERT_FALSE(lag.changed);
    LOGOS_ASSERT_EQ(lag.reason, std::string("indexer-lag"));

    const PalaceLezCoordinatorUpdate wrongHash = coordinator.reconcileFinality(
        transactionHash,
        indexerJson({{plan, hash(51U)}}));
    LOGOS_ASSERT_FALSE(wrongHash.accepted);
    LOGOS_ASSERT_EQ(wrongHash.reason, std::string("transaction-hash-mismatch"));

    const PalaceLezCoordinatorUpdate finalized = coordinator.reconcileFinality(
        transactionHash,
        indexerJson({{plan, transactionHash}}));
    LOGOS_ASSERT_TRUE(finalized.accepted);
    LOGOS_ASSERT_TRUE(finalized.changed);
    LOGOS_ASSERT_EQ(
        static_cast<std::uint8_t>(finalized.stage),
        static_cast<std::uint8_t>(PalaceLezTransactionStage::Finalized));
}

LOGOS_TEST(palace_lez_coordinator_snapshot_restores_every_phase_without_promotion) {
    using namespace palace;
    using namespace palace_lez_test;
    const PalaceLezInstructionV3 instruction{
        PalaceLezRevokeCapabilityV3{7U, bytes(0x12U)}};
    const PalaceLezTransactionPlanV3 plan =
        PalaceLezCodec::buildTransaction(kProgramId, kSigner, instruction);
    const std::string transactionHash = hash(60U);

    PalaceLezTransactionCoordinator submitted;
    makeCompatible(submitted);
    LOGOS_ASSERT_TRUE(submitted.registerSubmission(
        plan,
        submissionJson(transactionHash),
        kRootBorshSha256).accepted);
    const PalaceLezCoordinatorSnapshot submittedSnapshot = submitted.snapshot();
    LOGOS_ASSERT_TRUE(submittedSnapshot.accepted);

    PalaceLezTransactionCoordinator restoredSubmitted;
    LOGOS_ASSERT_TRUE(restoredSubmitted.restore(submittedSnapshot.bytes).accepted);
    LOGOS_ASSERT_EQ(
        static_cast<std::uint8_t>(restoredSubmitted.transactions().front().stage),
        static_cast<std::uint8_t>(PalaceLezTransactionStage::Submitted));
    LOGOS_ASSERT_FALSE(restoredSubmitted.running());
    makeCompatible(restoredSubmitted);
    LOGOS_ASSERT_TRUE(restoredSubmitted.observeStableRoot(
        transactionHash,
        30,
        rootAccountJson(),
        30).accepted);
    restoredSubmitted.interrupt();
    const PalaceLezCoordinatorSnapshot observedSnapshot = restoredSubmitted.snapshot();

    PalaceLezTransactionCoordinator restoredObserved;
    LOGOS_ASSERT_TRUE(restoredObserved.restore(observedSnapshot.bytes).accepted);
    LOGOS_ASSERT_EQ(
        static_cast<std::uint8_t>(restoredObserved.transactions().front().stage),
        static_cast<std::uint8_t>(PalaceLezTransactionStage::Observed));
    makeCompatible(restoredObserved);
    LOGOS_ASSERT_TRUE(restoredObserved.reconcileFinality(
        transactionHash,
        indexerJson({{plan, transactionHash}})).accepted);
    restoredObserved.interrupt();
    const PalaceLezCoordinatorSnapshot finalizedSnapshot = restoredObserved.snapshot();

    PalaceLezTransactionCoordinator restoredFinalized;
    LOGOS_ASSERT_TRUE(restoredFinalized.restore(finalizedSnapshot.bytes).accepted);
    LOGOS_ASSERT_EQ(
        static_cast<std::uint8_t>(restoredFinalized.transactions().front().stage),
        static_cast<std::uint8_t>(PalaceLezTransactionStage::Finalized));

    std::vector<std::uint8_t> corrupted = finalizedSnapshot.bytes;
    corrupted.back() ^= 0xffU;
    PalaceLezTransactionCoordinator rejected;
    LOGOS_ASSERT_TRUE(rejected.restore(submittedSnapshot.bytes).accepted);
    LOGOS_ASSERT_FALSE(rejected.restore(corrupted).accepted);
    LOGOS_ASSERT_EQ(rejected.transactions().size(), 1U);
    LOGOS_ASSERT_EQ(
        static_cast<std::uint8_t>(rejected.transactions().front().stage),
        static_cast<std::uint8_t>(PalaceLezTransactionStage::Submitted));

    std::vector<std::uint8_t> trailing = submittedSnapshot.bytes;
    trailing.push_back(0U);
    LOGOS_ASSERT_FALSE(PalaceLezTransactionCoordinator{}.restore(trailing).accepted);
    std::vector<std::uint8_t> oversized(600000U, 0U);
    LOGOS_ASSERT_FALSE(PalaceLezTransactionCoordinator{}.restore(oversized).accepted);
}

LOGOS_TEST(palace_lez_coordinator_interrupt_retains_state_and_blocks_progress) {
    using namespace palace;
    using namespace palace_lez_test;
    PalaceLezTransactionCoordinator coordinator;
    makeCompatible(coordinator);
    const PalaceLezTransactionPlanV3 plan = PalaceLezCodec::buildTransaction(
        kProgramId,
        kSigner,
        PalaceLezInstructionV3{PalaceLezRevokeCapabilityV3{7U, bytes(0x12U)}});
    const std::string transactionHash = hash(70U);
    LOGOS_ASSERT_TRUE(coordinator.registerSubmission(
        plan,
        submissionJson(transactionHash),
        kRootBorshSha256).accepted);
    coordinator.interrupt();
    const PalaceLezCoordinatorUpdate blocked = coordinator.observeStableRoot(
        transactionHash,
        40,
        rootAccountJson(),
        40);
    LOGOS_ASSERT_FALSE(blocked.accepted);
    LOGOS_ASSERT_EQ(blocked.reason, std::string("coordinator-interrupted"));
    LOGOS_ASSERT_EQ(coordinator.transactions().size(), 1U);
    LOGOS_ASSERT_TRUE(coordinator.reset());
    LOGOS_ASSERT_TRUE(coordinator.transactions().empty());
}
