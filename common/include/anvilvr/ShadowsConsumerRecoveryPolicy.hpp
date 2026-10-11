#pragma once

#include <algorithm>
#include <cstdint>

namespace anvilvr::shadows_consumer_recovery {

class Backoff final {
public:
    void begin(std::uint64_t nowMilliseconds) {
        active_ = true;
        attempts_ = 0;
        delayMilliseconds_ = kInitialDelayMilliseconds;
        nextAttemptMilliseconds_ = nowMilliseconds;
    }

    bool active() const { return active_; }
    bool canAttempt(std::uint64_t nowMilliseconds) const {
        return active_ && nowMilliseconds >= nextAttemptMilliseconds_;
    }

    void failed(std::uint64_t nowMilliseconds) {
        ++attempts_;
        nextAttemptMilliseconds_ = nowMilliseconds + delayMilliseconds_;
        delayMilliseconds_ = std::min(delayMilliseconds_ * 2u, kMaximumDelayMilliseconds);
    }

    void succeeded() { active_ = false; }

    std::uint64_t attempts() const { return attempts_; }
    std::uint64_t nextAttemptMilliseconds() const { return nextAttemptMilliseconds_; }

private:
    static constexpr std::uint64_t kInitialDelayMilliseconds = 50;
    static constexpr std::uint64_t kMaximumDelayMilliseconds = 1000;
    bool active_ = false;
    std::uint64_t attempts_ = 0;
    std::uint64_t delayMilliseconds_ = kInitialDelayMilliseconds;
    std::uint64_t nextAttemptMilliseconds_ = 0;
};

} // namespace anvilvr::shadows_consumer_recovery
