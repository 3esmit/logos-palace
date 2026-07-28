#include <logos_test.h>

#include <filesystem>
#include <fstream>

#include "palace_action_journal.h"
#include "palace_sha256.h"

LOGOS_TEST(delivery_publication_never_marks_a_durable_action_final) {
    palace::ActionJournal journal;
    LOGOS_ASSERT_TRUE(journal.createDraft("lock-room-1"));
    LOGOS_ASSERT_TRUE(journal.queue("lock-room-1"));
    LOGOS_ASSERT_TRUE(journal.markSubmittedToLez("lock-room-1"));
    LOGOS_ASSERT_TRUE(journal.markDeliveryPublished("lock-room-1"));

    const palace::ActionStatus afterDelivery = journal.status("lock-room-1");
    LOGOS_ASSERT_EQ(palace::actionStatusName(afterDelivery.durableStage),
                    std::string("submitted_to_lez"));
    LOGOS_ASSERT_TRUE(afterDelivery.deliveryPublished);
    LOGOS_ASSERT_FALSE(journal.markFinalized("lock-room-1"));
}

LOGOS_TEST(finality_requires_observation_and_preserves_delivery_status) {
    palace::ActionJournal journal;
    LOGOS_ASSERT_TRUE(journal.createDraft("ban-user-2"));
    LOGOS_ASSERT_TRUE(journal.queue("ban-user-2"));
    LOGOS_ASSERT_TRUE(journal.markSubmittedToLez("ban-user-2"));
    LOGOS_ASSERT_TRUE(journal.markDeliveryPublished("ban-user-2"));
    LOGOS_ASSERT_TRUE(journal.markObserved("ban-user-2"));
    LOGOS_ASSERT_TRUE(journal.markFinalized("ban-user-2"));

    LOGOS_ASSERT_EQ(palace::canonicalActionStatus(journal.status("ban-user-2")),
                    std::string("durable=finalized;delivery_published=1"));
}

LOGOS_TEST(duplicate_and_unknown_actions_fail_closed) {
    palace::ActionJournal journal;
    LOGOS_ASSERT_FALSE(journal.queue("door-state-3"));
    LOGOS_ASSERT_TRUE(journal.createDraft("door-state-3"));
    LOGOS_ASSERT_TRUE(journal.queue("door-state-3"));
    LOGOS_ASSERT_FALSE(journal.queue("door-state-3"));
    LOGOS_ASSERT_FALSE(journal.markObserved("missing"));
    LOGOS_ASSERT_FALSE(journal.markDeliveryPublished("missing"));
}

LOGOS_TEST(terminal_recovery_states_require_a_nonfinal_durable_action) {
    palace::ActionJournal journal;
    LOGOS_ASSERT_TRUE(journal.createDraft("recover-1"));
    LOGOS_ASSERT_TRUE(journal.queue("recover-1"));
    LOGOS_ASSERT_TRUE(journal.markSubmittedToLez("recover-1"));
    LOGOS_ASSERT_TRUE(journal.markObserved("recover-1"));
    LOGOS_ASSERT_TRUE(journal.markOrphaned("recover-1"));
    LOGOS_ASSERT_FALSE(journal.markFinalized("recover-1"));

    LOGOS_ASSERT_TRUE(journal.createDraft("reject-1"));
    LOGOS_ASSERT_TRUE(journal.queue("reject-1"));
    LOGOS_ASSERT_TRUE(journal.markRejected("reject-1"));
    LOGOS_ASSERT_FALSE(journal.markFinalized("reject-1"));
}

LOGOS_TEST(action_journal_store_restores_submitted_and_delivery_state) {
    const auto directory = std::filesystem::temp_directory_path()
        / "logos-palace-action-journal-contract";
    std::filesystem::remove_all(directory);

    palace::ActionJournal original;
    LOGOS_ASSERT_TRUE(original.createDraft("wire-action-1"));
    LOGOS_ASSERT_TRUE(original.queue("wire-action-1"));
    LOGOS_ASSERT_TRUE(original.markSubmittedToLez("wire-action-1"));
    LOGOS_ASSERT_TRUE(original.markDeliveryPublished("wire-action-1"));
    palace::ActionJournalStore store(directory.string());
    LOGOS_ASSERT_TRUE(store.save(original));

    palace::ActionJournal restored;
    LOGOS_ASSERT_TRUE(store.load(restored));
    LOGOS_ASSERT_EQ(palace::canonicalActionStatus(restored.status("wire-action-1")),
                    std::string("durable=submitted_to_lez;delivery_published=1"));
    std::filesystem::remove_all(directory);
}

LOGOS_TEST(action_journal_store_rejects_tampering_without_mutating_live_state) {
    const auto directory = std::filesystem::temp_directory_path()
        / "logos-palace-action-journal-invalid-contract";
    std::filesystem::remove_all(directory);
    std::filesystem::create_directories(directory);
    {
        std::ofstream output(directory / "action-journal-v1", std::ios::binary);
        const std::string invalidState = "version=1\n776972652d616374696f6e;9;0\n";
        output << palace::crypto::sha256Hex(invalidState) << '\n' << invalidState;
    }

    palace::ActionJournal journal;
    LOGOS_ASSERT_TRUE(journal.createDraft("safe-action"));
    palace::ActionJournalStore store(directory.string());
    LOGOS_ASSERT_FALSE(store.load(journal));
    LOGOS_ASSERT_EQ(palace::canonicalActionStatus(journal.status("safe-action")),
                    std::string("durable=local_draft;delivery_published=0"));
    std::filesystem::remove_all(directory);
}
