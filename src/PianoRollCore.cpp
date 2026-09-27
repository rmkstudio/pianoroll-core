// Shared piano-roll operations extracted from MIDI Lines and MIDI Recover.
// Distributed under GNU GPL version 3; see LICENSE and NOTICE.md for source notices.
#include <PianoRollCore/PianoRollCore.h>

namespace pianoroll {
bool Rect::contains(Point p) const noexcept {
    return width > 0 && height > 0 && p.x >= x && p.x < right() && p.y >= y && p.y < bottom();
}
bool Rect::intersects(Rect r) const noexcept {
    return width > 0 && height > 0 && r.width > 0 && r.height > 0 &&
           x < r.right() && right() > r.x && y < r.bottom() && bottom() > r.y;
}
Rect Rect::expanded(double h, double v) const noexcept {
    return {x - h, y - v, width + 2 * h, height + 2 * v};
}
Rect Rect::fromPoints(Point a, Point b) noexcept {
    return {std::min(a.x, b.x), std::min(a.y, b.y), std::abs(a.x - b.x), std::abs(a.y - b.y)};
}
double Viewport::timeToX(double time) const noexcept {
    return timeSpan > 0 ? area.x + (time - timeStart) * area.width / timeSpan : area.x;
}
double Viewport::xToTime(double x) const noexcept {
    return area.width > 0 ? timeStart + (x - area.x) * timeSpan / area.width : timeStart;
}
double Viewport::pitchToY(double pitch, double offset) const noexcept {
    return area.y + (topPitch - pitch + offset) * rowHeight;
}
int Viewport::yToPitch(double y, PitchHit policy) const noexcept {
    double p = rowHeight > 0 ? topPitch - (y - area.y) / rowHeight : topPitch;
    if (!std::isfinite(p)) return 0;
    p = policy == PitchHit::ceilFromTop ? std::ceil(p) : std::floor(p + 1);
    return static_cast<int>(std::clamp(p, 0.0, 127.0));
}
Rect Viewport::noteRect(double start, double end, int pitch, double height,
                        double minWidth, double rowOffset) const noexcept {
    return {timeToX(start), pitchToY(pitch, rowOffset),
            std::max(minWidth, timeToX(end) - timeToX(start)), std::max(0.0, height)};
}
double snapTime(double time, const SnapPolicy& p) noexcept {
    if (!std::isfinite(time)) return time;
    double result = time;
    if (p.step > 0 && std::isfinite(p.step)) {
        const double position = time / p.step;
        result = (p.rounding == Rounding::nearest ? std::round(position) : std::floor(position)) * p.step;
    }
    if (p.outputQuantum > 0 && std::isfinite(p.outputQuantum))
        result = std::round(result / p.outputQuantum) * p.outputQuantum;
    return std::max(p.minimum, result);
}
double snappedDelta(double anchor, double requested, const SnapPolicy& policy) noexcept {
    return snapTime(anchor + requested, policy) - anchor;
}
namespace {
const FlGridSegment& flSegmentAt(int tick, const FlTickGrid& grid,
                                  FlGridSegment& fallback) noexcept {
    const auto defaultUnit = std::max(1, grid.ticksPerQuarter /
                                           std::max(1, grid.division));
    fallback = {0, std::numeric_limits<int>::max(), defaultUnit, defaultUnit};
    for (const auto& segment : grid.segments) {
        if (segment.startTick <= tick && tick <= segment.endTick) return segment;
    }
    if (tick <= 0 && !grid.segments.empty()) return grid.segments.front();
    return fallback;
}
}
int flGridQuantizeTick(int tick, const FlTickGrid& grid) noexcept {
    if (!grid.enabled) return tick;
    FlGridSegment fallback;
    const auto& segment = flSegmentAt(tick, grid, fallback);
    const int defaultUnit = std::max(1, grid.ticksPerQuarter /
                                          std::max(1, grid.division));
    const int unit = std::max(1, segment.unitTicks > 0 ? segment.unitTicks : defaultUnit);
    const int divisor = std::max(1, segment.divisorTicks > 0 ? segment.divisorTicks : unit);
    double ratio = static_cast<double>(tick - segment.startTick) /
                   static_cast<double>(divisor);
    // CF2C70 adds the exact -0.5 constant for low mode 1 or bit 0x100 before
    // its integer conversion. nearbyint matches the default MXCSR nearest mode.
    if ((grid.mode & 0xff) == 1 || (grid.mode & 0x100) != 0) ratio -= 0.5;
    const auto steps = static_cast<long long>(std::nearbyint(ratio));
    const auto result = static_cast<long long>(segment.startTick) + steps * unit;
    return static_cast<int>(std::clamp(result,
                                       static_cast<long long>(std::numeric_limits<int>::min()),
                                       static_cast<long long>(std::numeric_limits<int>::max())));
}
double flGridQuantizeTime(double time, const FlTickGrid& grid) noexcept {
    if (!grid.enabled || !std::isfinite(time)) return time;
    const int ppq = std::max(1, grid.ticksPerQuarter);
    const auto ticks = static_cast<long long>(std::llround(time * ppq));
    const auto clamped = std::clamp(ticks,
                                    static_cast<long long>(std::numeric_limits<int>::min()),
                                    static_cast<long long>(std::numeric_limits<int>::max()));
    return static_cast<double>(flGridQuantizeTick(static_cast<int>(clamped), grid)) /
           static_cast<double>(ppq);
}
double flGridSnappedDelta(double anchorTime, double requestedDelta,
                          const FlTickGrid& grid) noexcept {
    return flGridQuantizeTime(anchorTime + requestedDelta, grid) - anchorTime;
}
void GroupBounds::include(double start, double end, int pitch, bool editable) noexcept {
    if (!editable || !std::isfinite(start) || !std::isfinite(end) || end <= start ||
        pitch < 0 || pitch > 127) return;
    ++count;
    earliest = std::min(earliest, start);
    latest = std::max(latest, end);
    shortest = std::min(shortest, end - start);
    lowestPitch = std::min(lowestPitch, pitch);
    highestPitch = std::max(highestPitch, pitch);
}
MoveDelta clampMove(const GroupBounds& b, MoveDelta requested, double minTime,
                    int minPitch, int maxPitch, double maxTime) noexcept {
    if (!b.count || !std::isfinite(requested.time) || minPitch > maxPitch) return {};
    const int lo = minPitch - b.lowestPitch, hi = maxPitch - b.highestPitch;
    if (lo > hi) return {};
    double time = std::max(minTime - b.earliest, requested.time);
    if (std::isfinite(maxTime) && std::isfinite(b.latest))
        time = std::min(maxTime - b.latest, time);
    return {time, std::clamp(requested.pitch, lo, hi)};
}
bool allowsMove(const GroupBounds& b, MoveDelta requested, double minTime,
                int minPitch, int maxPitch, double maxTime) noexcept {
    if (!b.count) return false;
    const auto result = clampMove(b, requested, minTime, minPitch, maxPitch, maxTime);
    return result.time == requested.time && result.pitch == requested.pitch;
}
double clampResize(const GroupBounds& b, double requested, double minimumLength) noexcept {
    if (!b.count || !std::isfinite(requested) || !std::isfinite(minimumLength)) return 0;
    return std::max(std::max(0.0, minimumLength) - b.shortest, requested);
}
} // namespace pianoroll
