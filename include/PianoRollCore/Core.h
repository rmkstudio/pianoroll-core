// Shared piano-roll operations extracted from MIDI Lines and MIDI Recover.
// Distributed under GNU GPL version 3; see LICENSE and NOTICE.md for source notices.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <set>
#include <utility>
#include <vector>

namespace pianoroll {
// Release version source; CMake and tools/snapshot.py read this declaration.
inline constexpr const char* version = "0.5.0";
inline constexpr std::size_t noIndex = std::numeric_limits<std::size_t>::max();

struct Point { double x = 0, y = 0; };
struct Rect {
    double x = 0, y = 0, width = 0, height = 0;
    double right() const noexcept { return x + width; }
    double bottom() const noexcept { return y + height; }
    bool contains(Point) const noexcept;
    bool intersects(Rect) const noexcept;
    Rect expanded(double horizontal, double vertical) const noexcept;
    static Rect fromPoints(Point, Point) noexcept;
};

// Units are supplied by the adapter: quarter-note beats or original integer ticks.
// No tempo, PPQ conversion, or saved document representation lives in this core.
struct Viewport {
    Rect area;
    double timeStart = 0, timeSpan = 1;
    double topPitch = 127, rowHeight = 12;
    double timeToX(double time) const noexcept;
    double xToTime(double x) const noexcept;
    double pitchToY(double pitch, double rowOffset = 0) const noexcept;
    enum class PitchHit { ceilFromTop, floorFromUpperEdge };
    int yToPitch(double y, PitchHit = PitchHit::ceilFromTop) const noexcept;
    // rowOffset places note top relative to the pitch row: 0 = row top;
    // 0.5 - noteHeight/(2*rowHeight) = vertically centred.
    Rect noteRect(double start, double end, int pitch, double noteHeight,
                  double minWidth = 2, double rowOffset = 0) const noexcept;
};

enum class Rounding { nearest, down };
struct SnapPolicy {
    double step = 0;                 // <= 0 disables grid snapping
    Rounding rounding = Rounding::nearest;
    double minimum = -std::numeric_limits<double>::infinity();
    double outputQuantum = 0;        // e.g. 1 for Recover's integer ticks; 0 for beats
};
double snapTime(double time, const SnapPolicy&) noexcept;
double snappedDelta(double anchorTime, double requestedDelta, const SnapPolicy&) noexcept;

// Only actually editable/participating notes contribute to group boundaries.
struct GroupBounds {
    std::size_t count = 0;
    double earliest = std::numeric_limits<double>::infinity();
    double shortest = std::numeric_limits<double>::infinity();
    int lowestPitch = 127, highestPitch = 0;
    void include(double start, double end, int pitch, bool editable = true) noexcept;
};
struct MoveDelta { double time = 0; int pitch = 0; };
MoveDelta clampMove(const GroupBounds&, MoveDelta requested, double minimumTime = 0,
                    int minimumPitch = 0, int maximumPitch = 127) noexcept;
bool allowsMove(const GroupBounds&, MoveDelta requested, double minimumTime = 0,
                int minimumPitch = 0, int maximumPitch = 127) noexcept;
double clampResize(const GroupBounds&, double requestedLengthDelta,
                   double minimumLength) noexcept;

struct CopyEntry { std::size_t source = noIndex, target = noIndex; };
struct CopyPlan {
    std::vector<CopyEntry> entries;
    std::size_t anchor = noIndex;
};
// Input order is retained. Missing, duplicated, invalid and read-only indices are skipped.
// `anchor` always denotes the actual grabbed note's copy, never the first selected note.
template<class Indices, class IsEditable>
CopyPlan makeCopyPlan(const Indices& indices, std::size_t originalCount,
                      std::size_t grabbedIndex, IsEditable isEditable) {
    CopyPlan result;
    std::set<std::size_t> seen;
    for (auto raw : indices) {
        auto index = static_cast<std::size_t>(raw);
        if (index >= originalCount || !seen.insert(index).second || !isEditable(index)) continue;
        const auto target = originalCount + result.entries.size();
        result.entries.push_back({index, target});
        if (index == grabbedIndex) result.anchor = target;
    }
    return result;
}

enum class HitPolicy { lastPainted, closest };
struct HitCandidate { Rect rect; double distance = 0; bool eligible = true; };
// Caller supplies note geometry and optional distance; first wins an equal-distance tie.
template<class CandidateAt>
int hitTest(std::size_t count, Point point, CandidateAt candidateAt,
            HitPolicy policy = HitPolicy::lastPainted) {
    int result = -1;
    double closest = std::numeric_limits<double>::infinity();
    for (std::size_t i = 0; i < count; ++i) {
        auto c = candidateAt(i);
        if (!c.eligible || !c.rect.contains(point)) continue;
        if (policy == HitPolicy::lastPainted || c.distance < closest) {
            result = static_cast<int>(i);
            closest = c.distance;
        }
    }
    return result;
}
template<class RectAt, class IsEligible, class OnHit>
void forEachIntersecting(std::size_t count, Rect box, RectAt rectAt,
                        IsEligible isEligible, OnHit onHit) {
    for (std::size_t i = 0; i < count; ++i)
        if (isEligible(i) && rectAt(i).intersects(box)) onHit(i);
}

enum class CommitResult { inactive, unchanged, stale, rejected, committed };
// Editor keeps its own baseline/draft and undo storage. This gate owns the shared
// gesture lifetime: a changed current gesture invokes one commit callback at most.
class EditSession {
public:
    void begin(std::uint64_t revision) noexcept { revision_ = revision; active_ = true; changed_ = false; }
    bool active() const noexcept { return active_; }
    bool changed() const noexcept { return active_ && changed_; }
    bool isCurrent(std::uint64_t revision) const noexcept { return active_ && revision_ == revision; }
    std::uint64_t revision() const noexcept { return revision_; }
    void markChanged(bool value = true) noexcept { if (active_) changed_ = value; }
    void cancel() noexcept { active_ = false; changed_ = false; }
    template<class Commit>
    CommitResult commit(std::uint64_t currentRevision, Commit applyAndRemember) {
        if (!active_) return CommitResult::inactive;
        const bool same = revision_ == currentRevision, changed = changed_;
        cancel(); // consume before invoking client code, also safe for reentrant mouse-up
        if (!same) return CommitResult::stale;
        if (!changed) return CommitResult::unchanged;
        return applyAndRemember() ? CommitResult::committed : CommitResult::rejected;
    }
private:
    std::uint64_t revision_ = 0;
    bool active_ = false, changed_ = false;
};
} // namespace pianoroll
