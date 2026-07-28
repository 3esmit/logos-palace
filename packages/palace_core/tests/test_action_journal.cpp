#include <logos_test.h>

#include "palace_action_journal.h"

LOGOS_TEST(delivery_publication_never_marks_a_durable_action_final) {
    palace::ActionJournal journal;
    LOGOS_ASSERT_TRUE(journal.createDraft("lock-room-1"));
    LOGOS_ASSERT_TRUE(journal.queue("lock-room-1"));
    LOGOS_ASSERT_TRUE(journal.markSubmittedToLez("lock-room-1"));
    LOGOS_ASSERT_TRUE(journal.markDeliveryPublished("lock-room-1"));

    const palace::ActionStatus afterDelivery = journal.status("lock-room-1");
    LOGOS_ASSERT_EQ(afterDelivery.durableStage, palace::DurableActionStage::SubmittedToLez);
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

    LOGOS_ASSERT_EQ(journal.actionStatus("ban-user-2"),
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
