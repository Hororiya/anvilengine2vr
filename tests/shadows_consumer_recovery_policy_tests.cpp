#include <anvilvr/ShadowsConsumerRecoveryPolicy.hpp>

#include <cassert>

using anvilvr::shadows_consumer_recovery::Backoff;

int main() {
    Backoff policy;
    assert(!policy.active());
    policy.begin(1000);
    assert(policy.canAttempt(1000));

    policy.failed(1000);
    assert(policy.attempts() == 1);
    assert(!policy.canAttempt(1049));
    assert(policy.canAttempt(1050));

    policy.failed(1050);
    assert(!policy.canAttempt(1149));
    assert(policy.canAttempt(1150));

    std::uint64_t now = 1150;
    for (int i = 0; i < 8; ++i) {
        policy.failed(now);
        const auto next = policy.nextAttemptMilliseconds();
        assert(next >= now + 100);
        assert(next <= now + 1000);
        now = next;
    }

    policy.succeeded();
    assert(!policy.active());
    assert(!policy.canAttempt(now + 10000));
    return 0;
}
