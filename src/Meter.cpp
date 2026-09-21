#include <PianoRollCore/Meter.h>
#include <algorithm>
#include <cmath>
#include <limits>

namespace pianoroll {
namespace {
std::int64_t safeBar(double value) noexcept {
    constexpr auto maximum = std::numeric_limits<std::int64_t>::max();
    return !std::isfinite(value) || value >= static_cast<double>(maximum) ? maximum :
           static_cast<std::int64_t>(std::max(1.0, value));
}
bool valid(MeterChange change) noexcept {
    return std::isfinite(change.time) && change.time >= 0 && change.numerator > 0 &&
           change.numerator <= 255 && change.denominator > 0 &&
           (change.denominator & (change.denominator - 1)) == 0;
}
}
void MeterMap::assign(std::vector<MeterChange> changes, double unitsPerQuarter) {
    if (!std::isfinite(unitsPerQuarter) || unitsPerQuarter <= 0) unitsPerQuarter = 1;
    changes.erase(std::remove_if(changes.begin(), changes.end(), [](auto c) { return !valid(c); }), changes.end());
    std::stable_sort(changes.begin(), changes.end(), [](auto a, auto b) { return a.time < b.time; });
    std::vector<MeterChange> unique;
    for (auto change : changes) {
        if (!unique.empty() && unique.back().time == change.time) unique.back() = change;
        else unique.push_back(change);
    }
    segments_.clear();
    segments_.push_back({0, unitsPerQuarter, unitsPerQuarter * 4, 1, 4, 4, true});
    for (auto change : unique) {
        const auto previous = segments_.back();
        const double beatLength = unitsPerQuarter * (4.0 / change.denominator);
        const double barLength = beatLength * change.numerator;
        if (!std::isfinite(beatLength) || !std::isfinite(barLength) || beatLength <= 0 || barLength <= 0) continue;
        // Repeated identical signatures do not invent a bar boundary.
        if (!previous.implicit && previous.numerator == change.numerator && previous.denominator == change.denominator) continue;
        const double elapsedBars = (change.time - previous.time) / previous.barLength;
        const auto bar = safeBar(static_cast<double>(previous.bar) + std::ceil(std::max(0.0, elapsedBars - 1e-9)));
        Segment segment{change.time, beatLength, barLength, bar, change.numerator, change.denominator, false};
        if (change.time == previous.time) segments_.back() = segment;
        else segments_.push_back(segment);
    }
}
MusicalPosition MeterMap::positionAt(double time) const noexcept {
    if (!std::isfinite(time) || time < 0) time = 0;
    Segment segment;
    if (!segments_.empty()) {
        auto next = std::upper_bound(segments_.begin(), segments_.end(), time,
                                    [](double t, const auto& s) { return t < s.time; });
        segment = next == segments_.begin() ? *next : *std::prev(next);
    }
    const double bars = std::max(0.0, (time - segment.time) / segment.barLength);
    const double whole = std::floor(bars + 1e-10);
    const double phase = std::max(0.0, time - segment.time - whole * segment.barLength);
    return {safeBar(static_cast<double>(segment.bar) + whole),
            1 + std::min(static_cast<double>(segment.numerator), phase / segment.beatLength),
            segment.numerator, segment.denominator, segment.implicit};
}
std::vector<MeterGridMark> MeterMap::grid(double from, double to, double minimumSpacing,
                                        std::size_t maximumMarks, double subdivisionStep) const {
    std::vector<MeterGridMark> result;
    if (!std::isfinite(from) || !std::isfinite(to) || to < from || maximumMarks == 0) return result;
    from = std::max(0.0, from);
    if (to < from) return result;
    minimumSpacing = std::isfinite(minimumSpacing) && minimumSpacing > 0 ? minimumSpacing : 1e-9;
    const double budgetSpacing = (to - from) / static_cast<double>(maximumMarks);
    minimumSpacing = std::max(minimumSpacing, budgetSpacing);
    const std::vector<Segment> defaults{Segment{}};
    const auto& segments = segments_.empty() ? defaults : segments_;
    result.reserve(std::min<std::size_t>(maximumMarks, 512));
    for (std::size_t index = 0; index < segments.size() && result.size() < maximumMarks; ++index) {
        const auto& segment = segments[index];
        const double end = index + 1 < segments.size() ? std::min(to, segments[index + 1].time) : to;
        const double begin = std::max(from, segment.time);
        if (begin > end || (index + 1 < segments.size() && begin == end)) continue;
        const bool perBeat = segment.beatLength >= minimumSpacing;
        const double stride = perBeat ? segment.beatLength : segment.barLength * std::max(1.0, std::ceil(minimumSpacing / segment.barLength));
        if (!std::isfinite(stride) || stride <= 0) continue;
        double step = std::max(0.0, std::ceil((begin - segment.time) / stride - 1e-10));
        for (std::size_t count = 0; count < maximumMarks && result.size() < maximumMarks; ++count, step += 1) {
            const double time = segment.time + step * stride;
            if (!std::isfinite(time) || time > end + 1e-10 ||
                (index + 1 < segments.size() && time >= segments[index + 1].time - 1e-10)) break;
            if (!result.empty() && time <= result.back().time) break; // huge-coordinate rounding cannot spin
            const auto position = positionAt(time);
            const bool barStart = std::abs(position.beat - 1) < 1e-7;
            result.push_back({time, position.bar, static_cast<int>(std::floor(position.beat + 1e-7)),
                              barStart, !segment.implicit && time == segment.time});
        }
    }
    // Snap subdivisions are layered over real meter lines: triplets and unusual
    // PPQ grids may not divide the denominator beat, but must never erase bars.
    if (std::isfinite(subdivisionStep) && subdivisionStep >= minimumSpacing && subdivisionStep > 0) {
        for (std::size_t index = 0; index < segments.size() && result.size() < maximumMarks; ++index) {
            const auto& segment = segments[index];
            const double end = index + 1 < segments.size() ? std::min(to, segments[index + 1].time) : to;
            const double begin = std::max(from, segment.time);
            if (begin > end || (index + 1 < segments.size() && begin == end)) continue;
            double step = std::max(0.0, std::ceil((begin - segment.time) / subdivisionStep - 1e-10));
            double previous = -1;
            for (std::size_t count = 0; count < maximumMarks && result.size() < maximumMarks; ++count, step += 1) {
                const double time = segment.time + step * subdivisionStep;
                if (!std::isfinite(time) || time > end + 1e-10 || time <= previous ||
                    (index + 1 < segments.size() && time >= segments[index + 1].time - 1e-10)) break;
                previous = time;
                const auto position = positionAt(time);
                const bool beatStart = std::abs(position.beat - std::round(position.beat)) < 1e-7;
                // Beat/bar lines were already emitted when their spacing permits.
                if (beatStart && segment.beatLength >= minimumSpacing) continue;
                result.push_back({time, position.bar, static_cast<int>(std::floor(position.beat + 1e-7)),
                                  std::abs(position.beat - 1) < 1e-7,
                                  !segment.implicit && time == segment.time, beatStart});
            }
        }
        std::stable_sort(result.begin(), result.end(), [](auto a, auto b) { return a.time < b.time; });
        result.erase(std::unique(result.begin(), result.end(), [](auto a, auto b) {
            return std::abs(a.time - b.time) < 1e-9;
        }), result.end());
    }
    return result;
}
} // namespace pianoroll
