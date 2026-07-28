#include <logos_test.h>

#include "palace_availability.h"

LOGOS_TEST(active_objects_require_two_independent_retainers_before_activation) {
    palace::ObjectRetentionIndex retention;
    LOGOS_ASSERT_TRUE(retention.declareActive("cid-room-background"));
    LOGOS_ASSERT_FALSE(retention.meetsActivationPolicy("cid-room-background"));
    LOGOS_ASSERT_FALSE(retention.canResolve("cid-room-background"));

    LOGOS_ASSERT_TRUE(retention.retain("cid-room-background", "alice"));
    LOGOS_ASSERT_FALSE(retention.retain("cid-room-background", "alice"));
    LOGOS_ASSERT_EQ(retention.availability("cid-room-background"), palace::ObjectAvailability::Degraded);
    LOGOS_ASSERT_TRUE(retention.canResolve("cid-room-background"));
    LOGOS_ASSERT_FALSE(retention.meetsActivationPolicy("cid-room-background"));

    LOGOS_ASSERT_TRUE(retention.retain("cid-room-background", "bob"));
    LOGOS_ASSERT_EQ(retention.availability("cid-room-background"),
                    palace::ObjectAvailability::RedundantlyRetained);
    LOGOS_ASSERT_TRUE(retention.meetsActivationPolicy("cid-room-background"));
}

LOGOS_TEST(creator_removal_preserves_recovery_from_another_retainer) {
    palace::ObjectRetentionIndex retention;
    LOGOS_ASSERT_TRUE(retention.declareActive("cid-script"));
    LOGOS_ASSERT_TRUE(retention.retain("cid-script", "alice"));
    LOGOS_ASSERT_TRUE(retention.retain("cid-script", "bob"));

    LOGOS_ASSERT_TRUE(retention.removeRetainer("cid-script", "alice"));
    LOGOS_ASSERT_EQ(retention.availability("cid-script"), palace::ObjectAvailability::Degraded);
    LOGOS_ASSERT_TRUE(retention.canResolve("cid-script"));
    LOGOS_ASSERT_FALSE(retention.meetsActivationPolicy("cid-script"));
}
