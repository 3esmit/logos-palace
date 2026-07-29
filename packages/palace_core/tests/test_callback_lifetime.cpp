#include <logos_test.h>

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>

#include "palace_callback_lifetime.h"

namespace {

struct CallbackOwner {
    std::atomic<int> calls{0};
};

} // namespace

LOGOS_TEST(callback_lifetime_drains_inflight_work_and_rejects_after_teardown) {
    CallbackOwner owner;
    auto lifetime =
        std::make_shared<palace::CallbackLifetime<CallbackOwner>>(
            &owner);
    std::mutex mutex;
    std::condition_variable condition;
    bool entered = false;
    bool release = false;
    std::atomic<bool> callbackAccepted{false};

    std::thread callback([&]() {
        callbackAccepted = lifetime->invoke(
            [&](CallbackOwner& guardedOwner) {
                {
                    std::lock_guard<std::mutex> lock(mutex);
                    entered = true;
                }
                condition.notify_all();
                std::unique_lock<std::mutex> lock(mutex);
                condition.wait(lock, [&]() { return release; });
                ++guardedOwner.calls;
            });
    });

    {
        std::unique_lock<std::mutex> lock(mutex);
        condition.wait(lock, [&]() { return entered; });
    }

    std::atomic<bool> invalidated{false};
    std::thread teardown([&]() {
        lifetime->invalidate();
        invalidated = true;
    });
    while (lifetime->acceptingCallbacks())
        std::this_thread::yield();
    const bool invalidatedWhileCallbackHeld =
        invalidated.load();

    {
        std::lock_guard<std::mutex> lock(mutex);
        release = true;
    }
    condition.notify_all();
    callback.join();
    teardown.join();

    LOGOS_ASSERT_FALSE(invalidatedWhileCallbackHeld);
    LOGOS_ASSERT_TRUE(callbackAccepted.load());
    LOGOS_ASSERT_TRUE(invalidated.load());
    LOGOS_ASSERT_EQ(owner.calls.load(), 1);
    LOGOS_ASSERT_FALSE(lifetime->invoke(
        [](CallbackOwner& guardedOwner) {
            ++guardedOwner.calls;
        }));
    LOGOS_ASSERT_EQ(owner.calls.load(), 1);
}

LOGOS_TEST(callback_lifetime_allows_guarded_reentrant_callbacks) {
    CallbackOwner owner;
    auto lifetime =
        std::make_shared<palace::CallbackLifetime<CallbackOwner>>(
            &owner);

    LOGOS_ASSERT_TRUE(lifetime->invoke(
        [&](CallbackOwner& guardedOwner) {
            ++guardedOwner.calls;
            LOGOS_ASSERT_TRUE(lifetime->invoke(
                [](CallbackOwner& nestedOwner) {
                    ++nestedOwner.calls;
                }));
        }));
    LOGOS_ASSERT_EQ(owner.calls.load(), 2);
}
