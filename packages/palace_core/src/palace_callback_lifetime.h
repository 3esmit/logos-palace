#pragma once

#include <atomic>
#include <mutex>
#include <utility>

namespace palace {

// Keeps callback owners alive for the duration of an invocation. invalidate()
// drains an in-flight callback and makes every later callback a no-op.
template <typename Owner>
class CallbackLifetime {
public:
    explicit CallbackLifetime(Owner* owner)
        : m_owner(owner)
    {
    }

    CallbackLifetime(const CallbackLifetime&) = delete;
    CallbackLifetime& operator=(const CallbackLifetime&) = delete;

    template <typename Callback>
    bool invoke(Callback&& callback)
    {
        if (!m_acceptingCallbacks.load(
                std::memory_order_acquire)) {
            return false;
        }
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        if (!m_acceptingCallbacks.load(
                std::memory_order_relaxed)
            || m_owner == nullptr) {
            return false;
        }
        std::forward<Callback>(callback)(*m_owner);
        return true;
    }

    void invalidate()
    {
        m_acceptingCallbacks.store(
            false, std::memory_order_release);
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        m_owner = nullptr;
    }

    bool acceptingCallbacks() const
    {
        return m_acceptingCallbacks.load(
            std::memory_order_acquire);
    }

private:
    std::atomic<bool> m_acceptingCallbacks{true};
    std::recursive_mutex m_mutex;
    Owner* m_owner = nullptr;
};

} // namespace palace
