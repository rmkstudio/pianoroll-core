#include <PianoRollCore/AutomationActions.h>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>

using namespace pianoroll;
namespace {
int passed = 0;
void check(bool value, const char* name) {
    if (!value) { std::cerr << "FAIL: " << name << '\n'; std::exit(1); }
    ++passed;
}
bool near(double a, double b) { return std::abs(a - b) < 1e-9; }
double state(const std::vector<AutomationEvent>& events, int source, int cc, double time) {
    double value = -1, lastTime = -1;
    for (const auto& event : events) {
        if (event.source == source && event.controller == cc && event.time <= time && event.time >= lastTime) {
            value = event.value;
            lastTime = event.time;
        }
    }
    return value;
}
std::size_t count(const std::vector<AutomationEvent>& events, int source, int cc) {
    return static_cast<std::size_t>(std::count_if(events.begin(), events.end(), [&](const auto& event) {
        return event.source == source && event.controller == cc;
    }));
}
}
int main() {
    const std::vector<ValuePoint> velocities{{0, 40, 1}, {1, 80, 2}, {2, 60, 3}};
    ValueTransform transform;
    transform.timeStart = 0; transform.timeEnd = 2;
    transform.minimumValue = 1;
    transform.valueScaleStart = transform.valueScaleEnd = .5;
    auto values = transformValues(velocities, transform);
    check(values[0].value == 20 && values[1].value == 40 && values[2].value == 30,
          "value scaling preserves internal dynamics");
    transform.valueOffsetStart = 0; transform.valueOffsetEnd = 20;
    values = transformValues(velocities, transform);
    check(values[0].value == 20 && values[1].value == 50 && values[2].value == 50,
          "left and right ramp interpolate from original times");
    check(velocities[1].value == 80 && transformValues(velocities, transform)[1].value == 50,
          "preview leaves baseline untouched and never compounds");
    transform.valueOffsetStart = -1000; transform.valueOffsetEnd = 1000;
    values = transformValues(velocities, transform);
    check(values.front().value == 1 && values.back().value == 127, "velocity clamps to note-on range");
    transform.minimumValue = 0;
    check(transformValues(velocities, transform).front().value == 0, "CC permits zero");

    ValueTransformBox box;
    box.highLeft = .5;
    transform = valueTransformFromBox(0, 2, 40, 80, box, 1, 127);
    check(transformValuePoint({0, 80, 1}, transform).value == 60 &&
          transformValuePoint({0, 40, 2}, transform).value == 40 &&
          transformValuePoint({2, 80, 3}, transform).value == 80,
          "top-left corner tilts upper edge and preserves lower and right edges");
    box = {}; box.lowLeft = box.lowRight = .25; box.highLeft = box.highRight = 1.25;
    box.timeScale = .5; box.timeOffset = 1;
    transform = valueTransformFromBox(2, 6, 20, 100, box);
    auto point = transformValuePoint({4, 60, 19}, transform);
    check(point.value == 80 && point.time == 7 && point.id == 19,
          "frame translation and horizontal shrink retain point identity");
    box = {}; box.lowLeft = box.lowRight = 2; box.highLeft = box.highRight = 3;
    transform = valueTransformFromBox(0, 2, 80, 80, box);
    check(transformValuePoint({1, 80, 3}, transform).value == 82,
          "constant-value selection still has a movable frame");
    transform.timeScale = 0;
    check(!validValueTransform(transform) && transformValues(velocities, transform)[0].value == 40,
          "invalid transform cannot collapse time or mutate values");

    const std::vector<AutomationEvent> events{
        {0, 10, 17, 1, 1}, {2, 40, 17, 1, 2}, {3, 60, 17, 1, 3},
        {5, 90, 17, 1, 4}, {10, 20, 17, 1, 5},
        {2, 120, 17, 11, 6}, {2, 77, 18, 1, 7}, {2, 35, 1, 1, 8}};
    AutomationScope scope{false, 17, {1}, 1, 4};
    AutomationTimeMap mapping; mapping.offset = 5;
    auto result = followAutomation(events, scope, mapping, true);
    check(result.ok && !result.changed && result.events.size() == events.size(), "CC follow defaults off");
    scope.enabled = true;
    result = followAutomation(events, scope, mapping, true);
    check(result.ok && result.changed && result.changedControllers == std::set<int>{1},
          "copy reports only explicitly selected controller");
    check(state(result.events, 17, 1, 6) == 10 && state(result.events, 17, 1, 7) == 40 &&
          state(result.events, 17, 1, 8) == 60 && state(result.events, 17, 1, 9) == 90,
          "copy carries true boundary state and restores destination end");
    check(state(result.events, 17, 1, 2) == 40 && state(result.events, 17, 11, 7) == 120 &&
          count(result.events, 18, 1) == 1 && count(result.events, 1, 1) == 1,
          "copy retains source and never broadcasts to other channels or tracks");
    check(std::all_of(result.events.begin(), result.events.end(), [](const auto& event) {
        return event.source != 17 || event.controller != 1 || event.time < 6 || event.time >= 9 || event.id == 0;
    }), "copy assigns new identities only to copied points and guards");
    result = followAutomation(events, scope, mapping, false);
    check(result.ok && state(result.events, 17, 1, 2) == 10 && state(result.events, 17, 1, 4) == 60 &&
          state(result.events, 17, 1, 9) == 90, "move clears source while preserving both exterior edge states");
    check(std::any_of(result.events.begin(), result.events.end(), [](const auto& event) {
        return event.id == 2 && event.time == 7;
    }), "move preserves real event identity");
    auto clipboardDestination = events;
    clipboardDestination[1].value = 99;
    result = copyAutomation(events, clipboardDestination, scope, mapping);
    check(result.ok && state(result.events, 17, 1, 2) == 99 && state(result.events, 17, 1, 7) == 40,
          "clipboard copies captured CC even after the source is edited");
    result = eraseAutomationRange(events, scope);
    check(result.ok && result.changed && state(result.events, 17, 1, 2) == 10 &&
          state(result.events, 17, 1, 4) == 60 && state(result.events, 18, 1, 2) == 77,
          "linked cut clears its source phrase and restores the original right-edge state");
    scope.controllers = {64};
    result = followAutomation(events, scope, mapping, true);
    check(result.ok && !result.changed && count(result.events, 17, 64) == 0,
          "selecting nonexistent controller never creates default CC");
    scope.controllers = {96};
    result = followAutomation(events, scope, mapping, true);
    check(!result.ok && !result.changed, "relative controller actions cannot be treated as latched values");
    scope.controllers = {1};
    mapping.offset = 1;
    result = followAutomation(events, scope, mapping, false);
    check(result.ok && state(result.events, 17, 1, .5) == 10 &&
          state(result.events, 17, 1, 3) == 40 && state(result.events, 17, 1, 4) == 60 &&
          state(result.events, 17, 1, 5) == 90,
          "overlapping move uses source snapshot and keeps union exterior states");
    scope.start = 2; scope.end = 3; mapping.offset = 4;
    result = followAutomation(events, scope, mapping, true);
    check(result.ok && state(result.events, 17, 1, 6.9) == 40 && state(result.events, 17, 1, 7) == 90,
          "right endpoint belongs to exterior and is not copied");
    scope = {true, 17, {1}, 1, 4};
    mapping.offset = 0;
    result = followAutomation(events, scope, mapping, false);
    check(result.ok && !result.changed && result.events.size() == events.size(),
          "identity follow leaves snapshot byte-level event sequence alone");

    const std::vector<AutomationEvent> startsLate{{10, 64, 17, 1, 1}, {12, 127, 17, 1, 2}};
    scope = {true, 17, {1}, 10, 13}; mapping.offset = -10;
    result = eraseAutomationRange(startsLate, scope);
    check(result.ok && state(result.events, 17, 1, 12) == -1 && state(result.events, 17, 1, 13) == 127,
          "cut with unknown prior state never inserts a default controller value");
    result = followAutomation(startsLate, scope, mapping, true);
    check(!result.ok && !result.changed && result.events.size() == startsLate.size() &&
          result.events.front().time == 10, "unknown destination restore rejects atomically without inferred zero");
    auto mixed = startsLate; mixed.push_back({0, 20, 17, 11, 3});
    scope.controllers = {1, 11};
    result = followAutomation(mixed, scope, mapping, true);
    check(!result.ok && result.changedControllers.empty() && result.events.size() == mixed.size(),
          "one unsafe lane cancels the complete multi-controller operation");

    mapping = {}; mapping.anchors = {{1, 1}, {2, 2.2}, {3, 2.8}, {4, 4}};
    check(validAutomationTimeMap(mapping) && near(mapAutomationTime(2.5, mapping), 2.5),
          "CC follows quantized note anchors by piecewise-linear warp");
    scope = {true, 17, {1}, 1, 4};
    result = followAutomation(events, scope, mapping, false);
    check(result.ok && std::any_of(result.events.begin(), result.events.end(), [](const auto& event) {
        return event.id == 2 && near(event.time, 2.2);
    }), "warp changes real CC timestamps without tick rounding");
    mapping.anchors = {{1, 1}, {2, 2.2}, {2, 2.3}, {4, 4}};
    check(validAutomationTimeMap(mapping) && mapAutomationTime(2, mapping) == 2.3 &&
          near(mapAutomationTime(3, mapping), 3.15), "equal input note anchors choose final anchor consistently");
    mapping.anchors = {{1, 1.2}, {2, 2.3}};
    check(near(mapAutomationTime(0, mapping), .2) && near(mapAutomationTime(3, mapping), 3.3),
          "warp preserves outer note-anchor displacements");
    mapping.anchors = {{1, 2}, {2, 1}};
    result = followAutomation(events, scope, mapping, false);
    check(!validAutomationTimeMap(mapping) && !result.ok, "reverse warp is rejected rather than reversing step semantics");
    mapping.anchors = {{1, 1}, {2, 2}, {3, 2}, {4, 4}};
    result = followAutomation(events, scope, mapping, false);
    check(result.ok && state(result.events, 17, 1, 2) == 60,
          "quantize collisions choose stable last original event");

    transform = {}; transform.timeStart = 1; transform.timeEnd = 4;
    transform.valueOffsetStart = transform.valueOffsetEnd = 20;
    result = transformAutomation(events, scope, transform);
    check(result.ok && state(result.events, 17, 1, .5) == 10 &&
          state(result.events, 17, 1, 1) == 30 && state(result.events, 17, 1, 2) == 60 &&
          state(result.events, 17, 1, 4) == 60 && state(result.events, 17, 1, 5) == 90,
          "CC value transform is local and restores untouched curve at right edge");
    check(result.events.size() == transformAutomation(events, scope, transform).events.size() &&
          state(transformAutomation(events, scope, transform).events, 17, 1, 2) == 60,
          "CC preview derives once from immutable baseline");
    transform = {}; transform.timeOrigin = 1; transform.timeScale = 2;
    result = transformAutomation(events, scope, transform);
    check(result.ok && state(result.events, 17, 1, 3) == 40 && state(result.events, 17, 1, 5) == 60 &&
          state(result.events, 17, 1, 7) == 90, "CC frame time scale preserves step values and exterior state");
    transform.timeScale = -1;
    check(!transformAutomation(events, scope, transform).ok, "CC time reverse cannot silently reorder curves");
    transform = {}; transform.valueOffsetStart = transform.valueOffsetEnd = -1000;
    result = transformAutomation(events, scope, transform);
    check(result.ok && state(result.events, 17, 1, 2) == 0, "CC transformation clamps to 0 through 127");
    transform.valueScaleEnd = std::numeric_limits<double>::quiet_NaN();
    check(!transformAutomation(events, scope, transform).ok, "nonfinite transform rejected before any write");

    auto rounded = std::vector<AutomationEvent>{{1.1, 20, 17, 1, 1}, {1.2, 30, 18, 1, 2},
        {1.3, 70, 17, 1, 3}, {1.4, 90, 17, 11, 4}};
    for (auto& event : rounded) event.time = std::round(event.time);
    rounded = coalesceAutomationEvents(std::move(rounded));
    check(rounded.size() == 3 && state(rounded, 17, 1, 1) == 70 && state(rounded, 18, 1, 1) == 30 &&
          state(rounded, 17, 11, 1) == 90, "post-rounding coalesce is stable and source/controller-specific");
    auto actions = coalesceAutomationEvents({{1, 0, 17, 96, 1}, {1, 0, 17, 96, 2},
                                             {1, 0, 17, 123, 3}, {1, 0, 17, 123, 4}});
    check(actions.size() == 4 && actions[1].id == 2 && actions[3].id == 4,
          "unrelated relative and channel-mode actions are never collapsed");
    std::cout << passed << " automation action checks passed\n";
}
