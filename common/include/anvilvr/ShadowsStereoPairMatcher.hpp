#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace anvilvr::shadows_stereo_pair {

struct Stamp final {
    std::uint64_t pairId = 0;
    std::uint64_t poseSequence = 0;
    std::int64_t predictedDisplayTime = 0;
    std::uint64_t publicationSequence = 0;
    bool explicitPairId = false;
};

struct Observation final {
    std::uint32_t eye = 0;
    std::size_t token = 0;
    Stamp stamp{};
};

struct Match final {
    std::array<std::size_t, 2> tokens{};
    Stamp stamp{};
};

inline std::optional<Stamp> makeStamp(std::uint64_t sourceFrame,
                                      std::uint64_t poseSequence,
                                      std::int64_t predictedDisplayTime,
                                      std::uint64_t publicationSequence,
                                      std::uint32_t reservedPairId) {
    if (publicationSequence == 0) return std::nullopt;
    if (reservedPairId != 0)
        return Stamp{reservedPairId, poseSequence, predictedDisplayTime, publicationSequence, true};
    if (sourceFrame == 0) return std::nullopt;
    return Stamp{(sourceFrame - 1u) / 2u, poseSequence, predictedDisplayTime,
                 publicationSequence, false};
}

inline std::optional<Match> newestMatchedPair(std::span<const Observation> observations) {
    std::optional<Match> best;
    for (const auto& left : observations) {
        if (left.eye != 0) continue;
        for (const auto& right : observations) {
            if (right.eye != 1 || left.stamp.explicitPairId != right.stamp.explicitPairId ||
                left.stamp.pairId != right.stamp.pairId)
                continue;
            if (left.stamp.explicitPairId &&
                (left.stamp.poseSequence != right.stamp.poseSequence ||
                 left.stamp.predictedDisplayTime != right.stamp.predictedDisplayTime))
                continue;
            const auto newestPublication = std::max(left.stamp.publicationSequence,
                                                    right.stamp.publicationSequence);
            const auto bestPublication = best ? best->stamp.publicationSequence : 0;
            if (!best || left.stamp.pairId > best->stamp.pairId ||
                (left.stamp.pairId == best->stamp.pairId && newestPublication > bestPublication)) {
                best = Match{{left.token, right.token},
                             Stamp{left.stamp.pairId, left.stamp.poseSequence,
                                   left.stamp.predictedDisplayTime,
                                   newestPublication, left.stamp.explicitPairId}};
            }
        }
    }
    return best;
}

inline bool isNewerThanCommitted(const Match& candidate, bool hasCommitted,
                                 bool committedExplicit, std::uint64_t committedPairId) {
    return !hasCommitted || candidate.stamp.explicitPairId != committedExplicit ||
           candidate.stamp.pairId > committedPairId;
}

} // namespace anvilvr::shadows_stereo_pair
