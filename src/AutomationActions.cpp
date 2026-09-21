#include <PianoRollCore/AutomationActions.h>

#include <algorithm>
#include <cmath>
#include <map>
#include <tuple>
#include <utility>

namespace pianoroll {
namespace {
bool finite(double value) noexcept { return std::isfinite(value); }
bool valueController(int cc) noexcept { return cc >= 0 && cc < 120 && cc != 96 && cc != 97; }
double clamp(double value, double low, double high) noexcept {
    return std::max(low, std::min(high, value));
}
double fraction(double time, double start, double end) noexcept {
    return end > start ? clamp((time - start) / (end - start), 0, 1) : 0;
}
double interpolate(double a, double b, double amount) noexcept {
    return a + (b - a) * amount;
}
bool validScope(const AutomationScope& scope) noexcept {
    if (scope.source < 0 || !finite(scope.start) || !finite(scope.end) ||
        scope.start < 0 || scope.end <= scope.start) return false;
    for (int cc : scope.controllers) if (cc < 0 || cc > 127) return false;
    return true;
}
bool validEvent(const AutomationEvent& event) noexcept {
    return finite(event.time) && event.time >= 0 && finite(event.value) &&
           event.value >= 0 && event.value <= 127;
}
struct KnownValue { bool known = false; double value = 0; };
KnownValue valueAt(const std::vector<AutomationEvent>& events, double time) {
    KnownValue result;
    // The lane is sorted; at equal times the final source event wins.
    for (const auto& event : events) {
        if (event.time > time) break;
        result = {true, event.value};
    }
    return result;
}
void sortLane(std::vector<AutomationEvent>& lane) {
    std::stable_sort(lane.begin(), lane.end(), [](const auto& a, const auto& b) {
        if (!finite(a.time)) return false;
        if (!finite(b.time)) return true;
        return a.time < b.time;
    });
}
void eraseRange(std::vector<AutomationEvent>& lane, double start, double end) {
    lane.erase(std::remove_if(lane.begin(), lane.end(), [=](const auto& event) {
        return event.time >= start && event.time < end;
    }), lane.end());
}
void ensureState(std::vector<AutomationEvent>& lane, double time, KnownValue state,
                 int source, int controller) {
    if (!state.known) return;
    sortLane(lane);
    const auto current = valueAt(lane, time);
    if (!current.known || current.value != state.value)
        lane.push_back({time, state.value, source, controller, 0});
}

template<class MapTime, class MapValue>
AutomationEditResult editAutomation(const std::vector<AutomationEvent>& sourceSnapshot,
                                    const std::vector<AutomationEvent>& snapshot,
                                    const AutomationScope& scope, bool copy,
                                    MapTime mapTime, MapValue mapValue) {
    AutomationEditResult result;
    result.events = snapshot;
    if (!scope.enabled) return result;
    auto fail = [&](const char* reason) {
        result.ok = false;
        result.changed = false;
        result.reason = reason;
        result.events = snapshot;
        result.changedControllers.clear();
        return result;
    };
    if (!validScope(scope)) return fail("Choose a source, controllers, and a valid time range.");
    if (scope.controllers.empty()) return result;
    for (int cc : scope.controllers)
        if (!valueController(cc))
            return fail("Relative and channel-mode controllers cannot follow or transform as value curves.");
    const double destinationStart = mapTime(scope.start);
    const double destinationEnd = mapTime(scope.end);
    if (!finite(destinationStart) || !finite(destinationEnd) ||
        destinationStart < 0 || destinationEnd <= destinationStart)
        return fail("Automation must keep a positive time range at or after the start.");

    std::map<int, std::vector<AutomationEvent>> lanes;
    for (const auto& event : sourceSnapshot) {
        if (event.source != scope.source || !scope.controllers.count(event.controller)) continue;
        if (!validEvent(event)) return fail("The selected controller contains an invalid event.");
        lanes[event.controller].push_back(event);
    }
    std::map<int, std::vector<AutomationEvent>> replacements;
    for (auto& item : lanes) {
        const int controller = item.first;
        auto& source = item.second;
        sortLane(source);
        std::vector<AutomationEvent> original;
        for (const auto& event : snapshot)
            if (event.source == scope.source && event.controller == controller) {
                if (!validEvent(event)) return fail("The destination controller contains an invalid event.");
                original.push_back(event);
            }
        sortLane(original);
        std::vector<AutomationEvent> payload;
        const auto sourceState = valueAt(source, scope.start);
        bool realEventAtStart = false;
        for (const auto& event : source) {
            if (event.time == scope.start) realEventAtStart = true;
            if (event.time >= scope.start && event.time < scope.end) payload.push_back(event);
        }
        if (sourceState.known && !realEventAtStart)
            payload.insert(payload.begin(), {scope.start, sourceState.value,
                                            scope.source, controller, 0});
        if (payload.empty()) continue; // No existing controller value to carry.
        bool changed = destinationStart != scope.start || destinationEnd != scope.end ||
                       (copy && &sourceSnapshot != &snapshot);
        for (auto& event : payload) {
            const double mappedTime = mapTime(event.time);
            const double mappedValue = mapValue(event.time, event.value);
            if (!finite(mappedTime) || mappedTime < destinationStart ||
                mappedTime > destinationEnd || !finite(mappedValue) ||
                mappedValue < 0 || mappedValue > 127)
                return fail("The automation transform produced an invalid event.");
            changed = changed || mappedTime != event.time || mappedValue != event.value;
            event.time = mappedTime;
            event.value = mappedValue;
            if (copy) event.id = 0;
        }
        if (!changed) continue;
        const auto destinationState = valueAt(original, destinationEnd);
        if (!destinationState.known)
            return fail("This destination has no earlier controller value to restore.");

        auto lane = original;
        if (!copy) {
            eraseRange(lane, scope.start, scope.end);
            ensureState(lane, scope.end, valueAt(original, scope.end), scope.source, controller);
        }
        eraseRange(lane, destinationStart, destinationEnd);
        // A flat quantize segment can land interior points on the right edge.
        // The original destination edge wins there, protecting outside state.
        payload.erase(std::remove_if(payload.begin(), payload.end(), [&](const auto& event) {
            return event.time >= destinationEnd;
        }), payload.end());
        lane.insert(lane.end(), payload.begin(), payload.end());
        ensureState(lane, destinationEnd, destinationState, scope.source, controller);
        replacements.emplace(controller, coalesceAutomationEvents(std::move(lane)));
        result.changedControllers.insert(controller);
    }
    if (replacements.empty()) return result;
    result.events.erase(std::remove_if(result.events.begin(), result.events.end(), [&](const auto& event) {
        return event.source == scope.source && replacements.count(event.controller) != 0;
    }), result.events.end());
    for (const auto& replacement : replacements)
        result.events.insert(result.events.end(), replacement.second.begin(), replacement.second.end());
    // Do not coalesce unrelated lanes: their original event sequence is owned
    // by the caller, even if it has duplicate controller events at one tick.
    sortLane(result.events);
    result.changed = true;
    return result;
}
} // namespace

ValueTransform valueTransformFromBox(double start, double end, double low, double high,
                                    const ValueTransformBox& box,
                                    double minimum, double maximum) {
    ValueTransform transform;
    transform.timeStart = start;
    transform.timeEnd = end;
    transform.valuePivot = low;
    const double span = std::max(1.0, high - low);
    transform.valueScaleStart = box.highLeft - box.lowLeft;
    transform.valueScaleEnd = box.highRight - box.lowRight;
    transform.valueOffsetStart = span * box.lowLeft;
    transform.valueOffsetEnd = span * box.lowRight;
    transform.timeOrigin = start;
    transform.timeScale = box.timeScale;
    transform.timeOffset = box.timeOffset * (end - start);
    transform.minimumValue = minimum;
    transform.maximumValue = maximum;
    return transform;
}
bool validValueTransform(const ValueTransform& transform) noexcept {
    const double values[] = {transform.timeStart, transform.timeEnd, transform.valuePivot,
        transform.valueScaleStart, transform.valueScaleEnd, transform.valueOffsetStart,
        transform.valueOffsetEnd, transform.timeOrigin, transform.timeScale,
        transform.timeOffset, transform.minimumValue, transform.maximumValue};
    for (double value : values) if (!finite(value)) return false;
    return transform.timeEnd >= transform.timeStart && transform.timeScale > 0 &&
           transform.minimumValue <= transform.maximumValue;
}
ValuePoint transformValuePoint(ValuePoint point, const ValueTransform& transform) noexcept {
    if (!validValueTransform(transform) || !finite(point.time) || !finite(point.value)) return point;
    const double u = fraction(point.time, transform.timeStart, transform.timeEnd);
    const double scale = interpolate(transform.valueScaleStart, transform.valueScaleEnd, u);
    const double offset = interpolate(transform.valueOffsetStart, transform.valueOffsetEnd, u);
    point.value = clamp(transform.valuePivot + (point.value - transform.valuePivot) * scale + offset,
                        transform.minimumValue, transform.maximumValue);
    point.time = transform.timeOrigin + (point.time - transform.timeOrigin) * transform.timeScale +
                 transform.timeOffset;
    return point;
}
std::vector<ValuePoint> transformValues(const std::vector<ValuePoint>& points,
                                      const ValueTransform& transform) {
    auto result = points;
    if (validValueTransform(transform))
        for (auto& point : result) point = transformValuePoint(point, transform);
    return result;
}

bool validAutomationTimeMap(const AutomationTimeMap& map) noexcept {
    if (!finite(map.origin) || !finite(map.scale) || !finite(map.offset) || map.scale <= 0) return false;
    // Duplicates are allowed, but the final anchor for each input position is
    // the one used below; validate monotonicity against those final anchors.
    bool havePrevious = false;
    double previousBefore = 0, previousAfter = 0;
    for (std::size_t i = 0; i < map.anchors.size(); ++i) {
        const auto& anchor = map.anchors[i];
        if (!finite(anchor.before) || !finite(anchor.after)) return false;
        if (i > 0 && anchor.before < map.anchors[i - 1].before) return false;
        if (i + 1 < map.anchors.size() && anchor.before == map.anchors[i + 1].before) continue;
        if (havePrevious && (anchor.before <= previousBefore || anchor.after < previousAfter)) return false;
        havePrevious = true;
        previousBefore = anchor.before;
        previousAfter = anchor.after;
    }
    return true;
}
double mapAutomationTime(double time, const AutomationTimeMap& map) {
    if (map.anchors.empty()) return map.origin + (time - map.origin) * map.scale + map.offset;
    // upper_bound chooses the LAST equal-before anchor, matching the collision
    // policy of MIDI event timing and avoiding an unstable quantize pivot.
    const auto upper = std::upper_bound(map.anchors.begin(), map.anchors.end(), time,
        [](double value, const auto& anchor) { return value < anchor.before; });
    if (upper == map.anchors.begin()) {
        auto first = upper;
        while (first + 1 != map.anchors.end() && (first + 1)->before == first->before) ++first;
        return time + first->after - first->before;
    }
    const auto lower = upper - 1;
    if (upper == map.anchors.end()) return time + lower->after - lower->before;
    auto next = upper;
    while (next + 1 != map.anchors.end() && (next + 1)->before == next->before) ++next;
    return interpolate(lower->after, next->after,
                       (time - lower->before) / (next->before - lower->before));
}
AutomationEditResult followAutomation(const std::vector<AutomationEvent>& snapshot,
                                     const AutomationScope& scope,
                                     const AutomationTimeMap& map, bool copy) {
    if (scope.enabled && !validAutomationTimeMap(map))
        return {false, false, "Controller-follow requires a forward time mapping.", snapshot, {}};
    return editAutomation(snapshot, snapshot, scope, copy,
        [&](double time) { return mapAutomationTime(time, map); },
        [](double, double value) { return value; });
}
AutomationEditResult copyAutomation(const std::vector<AutomationEvent>& sourceSnapshot,
                                   const std::vector<AutomationEvent>& destinationSnapshot,
                                   const AutomationScope& scope, const AutomationTimeMap& map) {
    if (scope.enabled && !validAutomationTimeMap(map))
        return {false, false, "Controller-follow requires a forward time mapping.", destinationSnapshot, {}};
    return editAutomation(sourceSnapshot, destinationSnapshot, scope, true,
        [&](double time) { return mapAutomationTime(time, map); },
        [](double, double value) { return value; });
}
AutomationEditResult eraseAutomationRange(const std::vector<AutomationEvent>& snapshot,
                                         const AutomationScope& scope) {
    AutomationEditResult result;
    result.events = snapshot;
    if (!scope.enabled) return result;
    if (!validScope(scope))
        return {false, false, "Choose a source, controllers, and a valid time range.", snapshot, {}};
    for (int cc : scope.controllers)
        if (!valueController(cc))
            return {false, false, "Relative and channel-mode controllers cannot follow as value curves.", snapshot, {}};
    for (int cc : scope.controllers) {
        std::vector<AutomationEvent> lane;
        bool changes = false;
        for (const auto& event : snapshot) {
            if (event.source != scope.source || event.controller != cc) continue;
            if (!validEvent(event))
                return {false, false, "The selected controller contains an invalid event.", snapshot, {}};
            lane.push_back(event);
            changes = changes || (event.time >= scope.start && event.time < scope.end);
        }
        if (!changes) continue;
        sortLane(lane);
        const auto endState = valueAt(lane, scope.end);
        eraseRange(lane, scope.start, scope.end);
        ensureState(lane, scope.end, endState, scope.source, cc);
        result.events.erase(std::remove_if(result.events.begin(), result.events.end(), [&](const auto& event) {
            return event.source == scope.source && event.controller == cc;
        }), result.events.end());
        result.events.insert(result.events.end(), lane.begin(), lane.end());
        result.changedControllers.insert(cc);
    }
    result.changed = !result.changedControllers.empty();
    if (result.changed) sortLane(result.events);
    return result;
}
AutomationEditResult transformAutomation(const std::vector<AutomationEvent>& snapshot,
                                        const AutomationScope& scope,
                                        const ValueTransform& transform) {
    if (scope.enabled && (!validValueTransform(transform) || transform.minimumValue < 0 ||
                          transform.maximumValue > 127))
        return {false, false, "Controller values must stay between 0 and 127.", snapshot, {}};
    return editAutomation(snapshot, snapshot, scope, false,
        [&](double time) { return transformValuePoint({time, 0, 0}, transform).time; },
        [&](double time, double value) { return transformValuePoint({time, value, 0}, transform).value; });
}
std::vector<AutomationEvent> coalesceAutomationEvents(std::vector<AutomationEvent> events) {
    using Key = std::tuple<double, int, int>;
    std::map<Key, std::size_t> last;
    for (std::size_t i = 0; i < events.size(); ++i)
        if (finite(events[i].time) && valueController(events[i].controller))
            last[{events[i].time, events[i].source, events[i].controller}] = i;
    std::vector<AutomationEvent> result;
    result.reserve(events.size());
    for (std::size_t i = 0; i < events.size(); ++i) {
        const auto& event = events[i];
        if (!finite(event.time) || !valueController(event.controller) ||
            last[{event.time, event.source, event.controller}] == i)
            result.push_back(event);
    }
    sortLane(result);
    return result;
}

} // namespace pianoroll
