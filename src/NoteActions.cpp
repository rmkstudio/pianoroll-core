#include "PianoRollCore/NoteActions.h"
#include <map>
#include <numeric>
#include <set>

namespace pianoroll {
namespace {
bool finite(double v) { return std::isfinite(v); }
bool eligible(const ActionNote& n, const ActionRules& r) {
    return n.editable && (!n.keySwitch || r.includeKeySwitches);
}
bool target(const ActionNote& n, const ActionRules& r) { return n.selected && eligible(n, r); }
bool inRange(double t, const ActionRange& r) { return !r.enabled || (t >= r.start && t < r.end); }
bool validRange(const ActionRange& r) {
    return !r.enabled || (finite(r.start) && finite(r.end) && r.end > r.start);
}
std::string inputError(const std::vector<ActionNote>& notes, const ActionRules& r) {
    if (!finite(r.minimumTime) || !finite(r.maximumTime) || r.maximumTime <= r.minimumTime ||
        !finite(r.minimumLength) || r.minimumLength <= 0 || r.minimumPitch < 0 ||
        r.maximumPitch > 127 || r.minimumPitch > r.maximumPitch || r.maximumNotes == 0)
        return "Invalid note editing limits.";
    if (notes.size() > r.maximumNotes) return "Note limit exceeded.";
    for (const auto& n : notes)
        if (!finite(n.start) || !finite(n.end) || n.end <= n.start || n.pitch < 0 || n.pitch > 127 ||
            n.velocity < 1 || n.velocity > 127)
            return "The note data contains an invalid time, pitch, or velocity.";
    return {};
}
NoteActionResult fail(const std::vector<ActionNote>& notes, const std::string& error) {
    return {notes, error, false};
}
bool same(const ActionNote& a, const ActionNote& b) {
    return a.start == b.start && a.end == b.end && a.pitch == b.pitch && a.velocity == b.velocity &&
        a.source == b.source && a.originalIndex == b.originalIndex && a.selected == b.selected &&
        a.editable == b.editable && a.keySwitch == b.keySwitch && a.generated == b.generated;
}
bool overlaps(const ActionNote& a, const ActionNote& b) {
    return a.source == b.source && a.pitch == b.pitch && a.start < b.end && b.start < a.end;
}
NoteActionResult finish(const std::vector<ActionNote>& before, std::vector<ActionNote> after,
                        const ActionRules& r) {
    if (after.size() > r.maximumNotes) return fail(before, "The operation would exceed the note limit.");
    bool changed = before.size() != after.size();
    for (std::size_t i = 0; i < after.size(); ++i) {
        const auto& n = after[i];
        const bool touched = i >= before.size() || !same(n, before[i]);
        changed = changed || touched;
        if (touched && (!finite(n.start) || !finite(n.end) || n.start < r.minimumTime ||
            n.end > r.maximumTime || n.end - n.start < r.minimumLength ||
            n.pitch < r.minimumPitch || n.pitch > r.maximumPitch || n.velocity < 1 || n.velocity > 127))
            return fail(before, "The operation would create an out-of-range or zero-length note.");
    }
    if (changed && r.rejectNewOverlaps) {
        std::map<std::size_t, const ActionNote*> originals;
        for (std::size_t i = 0; i < before.size(); ++i)
            originals.emplace(before[i].originalIndex == noIndex ? i : before[i].originalIndex, &before[i]);
        std::vector<std::size_t> order(after.size());
        std::iota(order.begin(), order.end(), 0);
        std::stable_sort(order.begin(), order.end(), [&](auto a, auto b) {
            const auto& x = after[a]; const auto& y = after[b];
            if (x.source != y.source) return x.source < y.source;
            if (x.pitch != y.pitch) return x.pitch < y.pitch;
            return x.start < y.start;
        });
        for (std::size_t i = 0; i < order.size(); ++i) {
            const auto& a = after[order[i]];
            if (!eligible(a, r)) continue;
            for (std::size_t j = i + 1; j < order.size(); ++j) {
                const auto& b = after[order[j]];
                if (a.source != b.source || a.pitch != b.pitch || b.start >= a.end) break;
                if (!eligible(b, r)) continue;
                auto ai = originals.find(a.originalIndex == noIndex ? order[i] : a.originalIndex);
                auto bi = originals.find(b.originalIndex == noIndex ? order[j] : b.originalIndex);
                if (a.generated || b.generated || ai == originals.end() || bi == originals.end() ||
                    ai == bi || !overlaps(*ai->second, *bi->second))
                    return fail(before, "The operation would introduce overlapping notes at the same pitch.");
            }
        }
    }
    return {std::move(after), {}, changed};
}
std::vector<std::size_t> selected(const std::vector<ActionNote>& notes, const ActionRules& r) {
    std::vector<std::size_t> out;
    for (std::size_t i = 0; i < notes.size(); ++i) if (target(notes[i], r)) out.push_back(i);
    return out;
}
bool validGrid(const ActionGrid& g) {
    return finite(g.step) && g.step > 0 && finite(g.origin) && finite(g.swing) && g.swing >= 0 &&
        g.swing < 1 && (g.division == GridDivision::straight || g.division == GridDivision::triplet ||
                       g.division == GridDivision::quintuplet);
}
double gridPoint(double index, const ActionGrid& grid) {
    const bool odd = std::fmod(std::abs(index), 2.0) == 1;
    return grid.origin + (index + (odd ? grid.swing : 0)) * actionGridStep(grid);
}
bool validPivot(MirrorPivot p) {
    return p == MirrorPivot::first || p == MirrorPivot::middle ||
        p == MirrorPivot::last || p == MirrorPivot::custom;
}
} // namespace

double actionGridStep(const ActionGrid& g) noexcept {
    if (!validGrid(g)) return std::numeric_limits<double>::quiet_NaN();
    return g.step * (g.division == GridDivision::triplet ? 2.0 / 3 :
                     g.division == GridDivision::quintuplet ? 4.0 / 5 : 1.0);
}
double nearestActionGrid(double time, const ActionGrid& g) noexcept {
    const double step = actionGridStep(g), ratio = (time - g.origin) / step;
    if (!finite(time) || !finite(ratio) || std::abs(ratio) > 0x1p50)
        return std::numeric_limits<double>::quiet_NaN();
    const double base = std::floor(ratio);
    double best = gridPoint(base - 2, g), distance = std::abs(time - best);
    for (int i = -1; i <= 2; ++i) {
        const double point = gridPoint(base + i, g), d = std::abs(time - point);
        if (d < distance) { best = point; distance = d; }
    }
    return best;
}
NoteActionResult quantizeNotes(const std::vector<ActionNote>& notes, const QuantizeOptions& o,
                               const ActionRules& r) {
    if (const auto error = inputError(notes, r); !error.empty()) return fail(notes, error);
    if (!validGrid(o.grid) || !finite(o.startStrength) || !finite(o.endStrength) ||
        o.startStrength < 0 || o.startStrength > 1 || o.endStrength < 0 || o.endStrength > 1 ||
        std::isnan(o.captureRange) || o.captureRange < 0 || !validRange(o.range))
        return fail(notes, "Invalid quantize settings.");
    auto out = notes;
    auto quantize = [&](double time, double strength) {
        if (!inRange(time, o.range) || strength == 0) return time;
        const double nearest = nearestActionGrid(time, o.grid);
        if (!finite(nearest)) return nearest;
        return std::abs(nearest - time) <= o.captureRange ? time + strength * (nearest - time) : time;
    };
    for (auto& n : out) if (target(n, r)) {
        const auto start = n.start, end = n.end;
        if (o.starts) n.start = quantize(start, o.startStrength);
        if (o.ends) n.end = quantize(end, o.endStrength);
        else n.end += n.start - start;
    }
    return finish(notes, std::move(out), r);
}
NoteActionResult restoreNoteTiming(const std::vector<ActionNote>& notes,
                                 const std::vector<OriginalTiming>& original, const ActionRules& r) {
    if (const auto error = inputError(notes, r); !error.empty()) return fail(notes, error);
    std::map<std::size_t, OriginalTiming> lookup;
    for (const auto& timing : original) {
        if (timing.originalIndex == noIndex || !finite(timing.start) || !finite(timing.end) ||
            timing.end <= timing.start || !lookup.emplace(timing.originalIndex, timing).second)
            return fail(notes, "Invalid original note timing.");
    }
    auto out = notes;
    for (std::size_t i = 0; i < out.size(); ++i) if (target(out[i], r)) {
        auto found = lookup.find(out[i].originalIndex == noIndex ? i : out[i].originalIndex);
        if (found == lookup.end()) return fail(notes, "Original timing is unavailable for a selected note.");
        out[i].start = found->second.start; out[i].end = found->second.end;
    }
    return finish(notes, std::move(out), r);
}
NoteActionResult mirrorNotes(const std::vector<ActionNote>& notes, const MirrorOptions& o,
                            const ActionRules& r) {
    if (const auto error = inputError(notes, r); !error.empty()) return fail(notes, error);
    if (!validPivot(o.pitchPivot) || !validPivot(o.timePivot) || !finite(o.customPitch) ||
        !finite(o.customTime) || (o.direction != MirrorDirection::pitch &&
        o.direction != MirrorDirection::time && o.direction != MirrorDirection::both))
        return fail(notes, "Invalid mirror settings.");
    auto ids = selected(notes, r), order = ids;
    if (ids.empty()) return {notes, {}, false};
    std::stable_sort(order.begin(), order.end(), [&](auto a, auto b) { return notes[a].start < notes[b].start; });
    double first = notes[ids[0]].start, last = notes[ids[0]].end;
    int low = 127, high = 0;
    for (auto i : ids) { first = std::min(first, notes[i].start); last = std::max(last, notes[i].end);
        low = std::min(low, notes[i].pitch); high = std::max(high, notes[i].pitch); }
    const double pitchAxis = o.pitchPivot == MirrorPivot::custom ? o.customPitch :
        o.pitchPivot == MirrorPivot::middle ? (low + high) * 0.5 :
        notes[o.pitchPivot == MirrorPivot::first ? order.front() : order.back()].pitch;
    const double timeAxis = o.timePivot == MirrorPivot::custom ? o.customTime :
        o.timePivot == MirrorPivot::first ? first : o.timePivot == MirrorPivot::last ? last : (first + last) * 0.5;
    auto out = notes;
    for (auto i : ids) {
        auto& n = out[i];
        if (o.direction != MirrorDirection::time) {
            const double pitch = pitchAxis * 2 - n.pitch;
            if (!finite(pitch) || pitch < r.minimumPitch || pitch > r.maximumPitch || std::round(pitch) != pitch)
                return fail(notes, "The mirror axis must keep every note at a valid MIDI pitch.");
            n.pitch = static_cast<int>(pitch);
        }
        if (o.direction != MirrorDirection::pitch) {
            n.start = timeAxis * 2 - notes[i].end; n.end = timeAxis * 2 - notes[i].start;
        }
    }
    return finish(notes, std::move(out), r);
}
NoteActionResult setNoteLength(const std::vector<ActionNote>& notes, double length, const ActionRules& r) {
    if (const auto error = inputError(notes, r); !error.empty()) return fail(notes, error);
    if (!finite(length) || length < r.minimumLength) return fail(notes, "Note length must be positive.");
    auto out = notes;
    for (auto& n : out) if (target(n, r)) n.end = n.start + length;
    return finish(notes, std::move(out), r);
}
NoteActionResult alignNoteEnds(const std::vector<ActionNote>& notes, double end, bool latest,
                              const ActionRules& r) {
    if (const auto error = inputError(notes, r); !error.empty()) return fail(notes, error);
    if (!finite(end)) return fail(notes, "Invalid end position.");
    auto ids = selected(notes, r);
    if (ids.empty()) return {notes, {}, false};
    if (latest) { end = notes[ids[0]].end; for (auto i : ids) end = std::max(end, notes[i].end); }
    auto out = notes;
    for (auto i : ids) out[i].end = end;
    return finish(notes, std::move(out), r);
}
NoteActionResult moveLinkedBoundary(const std::vector<ActionNote>& notes, std::size_t index,
                                   bool endBoundary, double time, double tolerance, const ActionRules& r) {
    if (const auto error = inputError(notes, r); !error.empty()) return fail(notes, error);
    if (index >= notes.size() || !target(notes[index], r) || !finite(time) ||
        !finite(tolerance) || tolerance < 0) return fail(notes, "Invalid linked note boundary.");
    const auto& anchor = notes[index];
    const double previous = endBoundary ? anchor.end : anchor.start;
    auto out = notes;
    if (endBoundary) out[index].end = time; else out[index].start = time;
    for (std::size_t i = 0; i < out.size(); ++i) if (i != index && target(out[i], r) &&
        out[i].source == anchor.source) {
        if (endBoundary && std::abs(out[i].start - previous) <= tolerance) out[i].start = time;
        if (!endBoundary && std::abs(out[i].end - previous) <= tolerance) out[i].end = time;
    }
    return finish(notes, std::move(out), r);
}
NoteActionResult scaleNoteTime(const std::vector<ActionNote>& notes, double factor,
                              double anchor, const ActionRules& r) {
    if (const auto error = inputError(notes, r); !error.empty()) return fail(notes, error);
    if (!finite(factor) || factor <= 0 || !finite(anchor)) return fail(notes, "Invalid phrase stretch settings.");
    auto out = notes;
    for (auto& n : out) if (target(n, r)) {
        n.start = anchor + (n.start - anchor) * factor;
        n.end = anchor + (n.end - anchor) * factor;
    }
    return finish(notes, std::move(out), r);
}
NoteActionResult transposeNotes(const std::vector<ActionNote>& notes, int semitones, const ActionRules& r) {
    if (const auto error = inputError(notes, r); !error.empty()) return fail(notes, error);
    auto out = notes;
    for (auto& n : out) if (target(n, r)) {
        const auto pitch = static_cast<long long>(n.pitch) + semitones;
        if (pitch < r.minimumPitch || pitch > r.maximumPitch) return fail(notes, "Transposition exceeds the MIDI pitch range.");
        n.pitch = static_cast<int>(pitch);
    }
    return finish(notes, std::move(out), r);
}
NoteActionResult changeNoteVelocity(const std::vector<ActionNote>& notes, int delta, const ActionRules& r) {
    if (const auto error = inputError(notes, r); !error.empty()) return fail(notes, error);
    auto out = notes;
    for (auto& n : out) if (target(n, r))
        n.velocity = static_cast<int>(std::clamp(static_cast<long long>(n.velocity) + delta, 1LL, 127LL));
    return finish(notes, std::move(out), r);
}
NoteSelectionResult selectNotes(const std::vector<ActionNote>& notes, const NoteSelectionOptions& o,
                                const ActionRules& r) {
    if (const auto error = inputError(notes, r); !error.empty()) return {{}, error};
    if (!validRange(o.range) || o.minimum > o.maximum || !finite(o.minimumLength) ||
        !finite(o.maximumLength) || o.minimumLength < 0 || o.minimumLength > o.maximumLength ||
        o.every == 0 || o.offset >= o.every || !finite(o.period) || o.period <= 0 ||
        !finite(o.position) || !finite(o.origin) || !finite(o.tolerance) || o.tolerance < 0)
        return {{}, "Invalid note selection settings."};
    std::vector<std::size_t> ids;
    for (std::size_t i = 0; i < notes.size(); ++i)
        if (eligible(notes[i], r) && (!o.withinSelection || notes[i].selected) && inRange(notes[i].start, o.range))
            ids.push_back(i);
    if (o.kind == NoteSelectionKind::highest || o.kind == NoteSelectionKind::lowest) {
        struct Edge { double time; std::size_t index; bool start; };
        std::vector<Edge> edges;
        for (auto i : ids) { edges.push_back({notes[i].start, i, true}); edges.push_back({notes[i].end, i, false}); }
        std::sort(edges.begin(), edges.end(), [](const auto& a, const auto& b) { return a.time < b.time; });
        std::map<int, std::set<std::size_t>> active;
        std::set<std::size_t> found;
        for (std::size_t cursor = 0; cursor < edges.size();) {
            const double time = edges[cursor].time;
            do {
                auto e = edges[cursor++];
                if (e.start) active[notes[e.index].pitch].insert(e.index);
                else { auto it = active.find(notes[e.index].pitch); if (it != active.end()) {
                    it->second.erase(e.index); if (it->second.empty()) active.erase(it); } }
            } while (cursor < edges.size() && edges[cursor].time == time);
            if (!active.empty() && cursor < edges.size() && edges[cursor].time > time) {
                const auto& group = o.kind == NoteSelectionKind::highest ? active.rbegin()->second : active.begin()->second;
                found.insert(group.begin(), group.end());
            }
        }
        return {{found.begin(), found.end()}, {}};
    }
    std::stable_sort(ids.begin(), ids.end(), [&](auto a, auto b) {
        if (notes[a].start != notes[b].start) return notes[a].start < notes[b].start;
        return notes[a].pitch < notes[b].pitch;
    });
    NoteSelectionResult result;
    for (std::size_t ordinal = 0; ordinal < ids.size(); ++ordinal) {
        const auto id = ids[ordinal]; const auto& n = notes[id]; bool match = false;
        switch (o.kind) {
            case NoteSelectionKind::pitchRange: match = n.pitch >= o.minimum && n.pitch <= o.maximum; break;
            case NoteSelectionKind::velocityRange: match = n.velocity >= o.minimum && n.velocity <= o.maximum; break;
            case NoteSelectionKind::lengthRange: match = n.end - n.start >= o.minimumLength && n.end - n.start <= o.maximumLength; break;
            case NoteSelectionKind::everyNth: match = ordinal % o.every == o.offset; break;
            case NoteSelectionKind::beatPosition: {
                double distance = std::fmod(n.start - o.origin - o.position, o.period);
                if (distance < 0) distance += o.period;
                match = std::min(distance, o.period - distance) <= o.tolerance; break;
            }
            default: return {{}, "Unknown note selection condition."};
        }
        if (match) result.indices.push_back(id);
    }
    std::sort(result.indices.begin(), result.indices.end());
    return result;
}
NoteActionResult splitNotes(const std::vector<ActionNote>& notes, const ActionGrid& grid, const ActionRules& r) {
    if (const auto error = inputError(notes, r); !error.empty()) return fail(notes, error);
    if (!validGrid(grid) || actionGridStep(grid) * (1 - grid.swing) < r.minimumLength)
        return fail(notes, "The split grid is finer than the minimum note length.");
    auto out = notes;
    const double step = actionGridStep(grid);
    for (std::size_t i = 0; i < notes.size(); ++i) if (target(notes[i], r)) {
        const auto& original = notes[i];
        const double ratio = (original.start - grid.origin) / step;
        if (!finite(ratio) || std::abs(ratio) > 0x1p50)
            return fail(notes, "The split grid is too fine for this time range.");
        double index = std::floor(ratio) - 1, start = original.start;
        bool first = true;
        for (;;) {
            const double point = gridPoint(++index, grid);
            if (!finite(point)) return fail(notes, "Invalid split position.");
            if (point >= original.end) break;
            if (point <= start || point - start < r.minimumLength || original.end - point < r.minimumLength) continue;
            if (first) { out[i].end = point; first = false; }
            else {
                auto part = original; part.start = start; part.end = point; part.generated = true;
                if (part.originalIndex == noIndex) part.originalIndex = i;
                out.push_back(part);
            }
            start = point;
            if (out.size() >= r.maximumNotes) return fail(notes, "The split would exceed the note limit.");
        }
        if (!first) {
            auto part = original; part.start = start; part.generated = true;
            if (part.originalIndex == noIndex) part.originalIndex = i;
            out.push_back(part);
        }
    }
    return finish(notes, std::move(out), r);
}
NoteActionResult splitNoteAt(const std::vector<ActionNote>& notes, std::size_t noteIndex,
                             double time, const ActionRules& r) {
    if (const auto error = inputError(notes, r); !error.empty()) return fail(notes, error);
    if (noteIndex >= notes.size() || !target(notes[noteIndex], r) || !finite(time))
        return fail(notes, "The split target must be a selected editable note.");
    const auto& original = notes[noteIndex];
    if (time - original.start < r.minimumLength || original.end - time < r.minimumLength)
        return fail(notes, "The split would create a note shorter than the minimum length.");
    auto out = notes;
    out[noteIndex].end = time;
    auto part = original;
    part.start = time;
    part.generated = true;
    if (part.originalIndex == noIndex) part.originalIndex = noteIndex;
    out.push_back(part);
    return finish(notes, std::move(out), r);
}
NoteActionResult glueNotes(const std::vector<ActionNote>& notes, const GlueOptions& options,
                           const ActionRules& r) {
    if (const auto error = inputError(notes, r); !error.empty()) return fail(notes, error);
    if (!finite(options.tolerance) || options.tolerance < 0)
        return fail(notes, "Invalid glue tolerance.");
    auto out = notes;
    for (std::size_t i = 0; i < out.size(); ++i) {
        if (!eligible(out[i], r) || (options.selectedOnly && !out[i].selected)) continue;
        for (std::size_t j = i + 1; j < out.size();) {
            auto& left = out[i];
            const auto& right = out[j];
            if (!eligible(right, r) || (options.selectedOnly && !right.selected) ||
                left.source != right.source || left.pitch != right.pitch ||
                left.keySwitch != right.keySwitch ||
                right.start > left.end + options.tolerance ||
                left.start > right.end + options.tolerance) {
                ++j;
                continue;
            }
            const bool keepLeft = left.start <= right.start;
            const auto metadata = keepLeft ? left : right;
            left.start = std::min(left.start, right.start);
            left.end = std::max(left.end, right.end);
            left.velocity = metadata.velocity;
            left.source = metadata.source;
            left.originalIndex = metadata.originalIndex;
            left.selected = left.selected || right.selected;
            left.editable = left.editable && right.editable;
            left.generated = left.generated || right.generated;
            out.erase(out.begin() + static_cast<std::ptrdiff_t>(j));
        }
    }
    return finish(notes, std::move(out), r);
}
NoteActionResult repeatNotes(const std::vector<ActionNote>& notes, const RepeatOptions& o, const ActionRules& r) {
    if (const auto error = inputError(notes, r); !error.empty()) return fail(notes, error);
    if (!finite(o.start) || !finite(o.end) || !finite(o.period) || o.start < r.minimumTime ||
        o.end <= o.start || o.end > r.maximumTime || o.period < 0)
        return fail(notes, "Invalid repeat range.");
    auto ids = selected(notes, r);
    if (ids.empty()) return {notes, {}, false};
    double first = notes[ids[0]].start, end = notes[ids[0]].end;
    for (auto i : ids) { first = std::min(first, notes[i].start); end = std::max(end, notes[i].end); }
    const double period = o.period > 0 ? o.period : end - first;
    if (period < r.minimumLength) return fail(notes, "Repeat period is too short.");
    auto out = notes;
    for (auto i : ids) {
        const auto& n = notes[i];
        if (n.end - n.start < r.minimumLength)
            return fail(notes, "A selected note is shorter than the minimum repeat length.");
        const double initial = std::max(1.0, std::ceil((o.start - n.start) / period));
        if (!finite(initial) || initial > 0x1p50) return fail(notes, "Repeat range is too large.");
        for (double count = initial;; count += 1) {
            auto copy = n; copy.start = n.start + period * count;
            if (!finite(copy.start)) return fail(notes, "Invalid repeat position.");
            if (copy.start >= o.end) break;
            copy.end = n.end + period * count;
            if (o.clipAtEnd) copy.end = std::min(copy.end, o.end);
            else if (copy.end > o.end) break;
            if (copy.end - copy.start < r.minimumLength) continue;
            copy.generated = true; if (copy.originalIndex == noIndex) copy.originalIndex = i;
            out.push_back(copy);
            if (out.size() > r.maximumNotes) return fail(notes, "The repeat would exceed the note limit.");
        }
    }
    return finish(notes, std::move(out), r);
}
NoteActionResult strumNotes(const std::vector<ActionNote>& notes, const StrumOptions& o, const ActionRules& r) {
    if (const auto error = inputError(notes, r); !error.empty()) return fail(notes, error);
    if (!finite(o.spacing) || o.spacing < 0 || !finite(o.onsetTolerance) || o.onsetTolerance < 0)
        return fail(notes, "Invalid strum settings.");
    auto ids = selected(notes, r);
    std::stable_sort(ids.begin(), ids.end(), [&](auto a, auto b) { return notes[a].start < notes[b].start; });
    auto out = notes;
    for (std::size_t first = 0; first < ids.size();) {
        std::size_t end = first + 1;
        while (end < ids.size() && notes[ids[end]].start - notes[ids[first]].start <= o.onsetTolerance) ++end;
        std::stable_sort(ids.begin() + static_cast<std::ptrdiff_t>(first), ids.begin() + static_cast<std::ptrdiff_t>(end),
            [&](auto a, auto b) { return o.descending ? notes[a].pitch > notes[b].pitch : notes[a].pitch < notes[b].pitch; });
        for (auto j = first; j < end; ++j) {
            const double delta = (j - first) * o.spacing;
            out[ids[j]].start += delta;
            if (!o.preserveEnds) out[ids[j]].end += delta;
        }
        first = end;
    }
    return finish(notes, std::move(out), r);
}
} // namespace pianoroll
