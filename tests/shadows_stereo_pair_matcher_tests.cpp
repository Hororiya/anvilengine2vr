#include <anvilvr/ShadowsStereoPairMatcher.hpp>

#include <cassert>
#include <vector>

using namespace anvilvr::shadows_stereo_pair;

int main() {
    const auto oldLeft = makeStamp(1, 101, 1001, 1, 0);
    const auto oldRight = makeStamp(2, 102, 1002, 2, 0);
    assert(oldLeft && oldRight && oldLeft->pairId == 0 && oldRight->pairId == 0);

    std::vector<Observation> pending{{1, 9, *oldRight}, {0, 7, *oldLeft}};
    auto match = newestMatchedPair(pending);
    assert(match && match->tokens[0] == 7 && match->tokens[1] == 9);

    pending = {{0, 11, *makeStamp(3, 201, 2001, 3, 0)}};
    assert(!newestMatchedPair(pending));

    pending = {{0, 20, *makeStamp(100, 300, 3000, 10, 55)},
               {1, 21, *makeStamp(101, 301, 3000, 11, 55)}};
    assert(!newestMatchedPair(pending));
    pending.push_back({1, 22, *makeStamp(102, 300, 3001, 12, 55)});
    assert(!newestMatchedPair(pending));
    pending.push_back({1, 23, *makeStamp(103, 300, 3000, 13, 55)});
    match = newestMatchedPair(pending);
    assert(match && match->tokens[0] == 20 && match->tokens[1] == 23 &&
           match->stamp.explicitPairId && match->stamp.pairId == 55);

    pending = {{0, 30, *makeStamp(0, 400, 4000, 20, 70)},
               {1, 31, *makeStamp(0, 400, 4000, 21, 70)},
               {0, 32, *makeStamp(0, 401, 4001, 22, 71)},
               {1, 33, *makeStamp(0, 401, 4001, 23, 71)}};
    match = newestMatchedPair(pending);
    assert(match && match->stamp.pairId == 71 && match->tokens[0] == 32 &&
           match->tokens[1] == 33);
    assert(isNewerThanCommitted(*match, true, true, 70));
    assert(!isNewerThanCommitted(*match, true, true, 71));
    assert(!isNewerThanCommitted(*match, true, true, 72));
    return 0;
}
