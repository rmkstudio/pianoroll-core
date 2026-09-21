#pragma once

#include <cstddef>
#include <cstdint>
#include <set>
#include <string>
#include <vector>

namespace pianoroll {

// All functions operate on UI-thread snapshots. Times use the adapter's own
// units (beats or original ticks); round once when publishing to its document.
struct ValuePoint {
    double time = 0, value = 0;
    std::uint64_t id = 0;
};
struct ValueTransform {
    double timeStart = 0, timeEnd = 1;
    double valuePivot = 0;
    double valueScaleStart = 1, valueScaleEnd = 1;
    double valueOffsetStart = 0, valueOffsetEnd = 0;
    double timeOrigin = 0, timeScale = 1, timeOffset = 0;
    double minimumValue = 0, maximumValue = 127;
};
// Normalised corners of the original selection rectangle. A top-left drag
// changes highLeft only. A whole-frame drag adds equally to all four corners.
struct ValueTransformBox {
    double lowLeft = 0, lowRight = 0, highLeft = 1, highRight = 1;
    double timeScale = 1, timeOffset = 0; // offset is a FRACTION of selection duration
};
ValueTransform valueTransformFromBox(double start, double end, double low,
                                    double high, const ValueTransformBox&,
                                    double minimum = 0, double maximum = 127);
bool validValueTransform(const ValueTransform&) noexcept;
ValuePoint transformValuePoint(ValuePoint, const ValueTransform&) noexcept;
// Always pass the mouse-down snapshot, not the previous preview. Values clamp
// after transformation; use minimumValue=1 for note-on velocity, 0 for CC.
std::vector<ValuePoint> transformValues(const std::vector<ValuePoint>&,
                                      const ValueTransform&);

struct AutomationEvent {
    double time = 0, value = 0;
    int source = -1, controller = -1; // source identifies BOTH track and channel
    std::uint64_t id = 0;             // 0 marks a new boundary/copy event
};
struct AutomationScope {
    bool enabled = false;
    int source = -1;
    std::set<int> controllers;
    double start = 0, end = 0; // half-open [start, end)
};
struct AutomationTimeAnchor { double before = 0, after = 0; };
struct AutomationTimeMap {
    double origin = 0, scale = 1, offset = 0;
    // Empty: affine mapping. Otherwise sorted piecewise-linear anchors;
    // equal input positions take the last anchor. Outside their range the
    // first/last anchor's displacement is retained. Maps must be monotone.
    std::vector<AutomationTimeAnchor> anchors;
};
bool validAutomationTimeMap(const AutomationTimeMap&) noexcept;
double mapAutomationTime(double, const AutomationTimeMap&);
struct AutomationEditResult {
    bool ok = true, changed = false;
    std::string reason;
    std::vector<AutomationEvent> events;
    std::set<int> changedControllers;
};

// Controller-follow is explicitly scoped and OFF by default. This replaces
// destination events only for actual source lanes, preserving all other lanes.
// It carries the real source value at the phrase start (if known), includes
// real events in [start,end), and restores the original value at the destination
// end. Moving also clears [start,end) and restores its original end value.
// Thus states outside the union of both ranges are preserved, including when
// ranges overlap. Copying stays on the explicitly chosen source; adapters must
// never broadcast shared source automation to every note group/voice.
// No default CC value is inferred. If a changed destination has no known value
// at its end, the whole edit is rejected atomically, rather than changing the
// subsequent unknown state. Empty/nonexistent selected CC lanes stay empty.
AutomationEditResult followAutomation(const std::vector<AutomationEvent>&,
                                     const AutomationScope&,
                                     const AutomationTimeMap&, bool copy);
// Clipboard/repeat variant: sourceSnapshot remains the original captured data
// while destinationSnapshot may already contain subsequent edits or copies.
// Scope.source identifies the same explicit track/channel in both snapshots.
AutomationEditResult copyAutomation(const std::vector<AutomationEvent>& sourceSnapshot,
                                   const std::vector<AutomationEvent>& destinationSnapshot,
                                   const AutomationScope&, const AutomationTimeMap&);
// The source-removal half of Cut: clear the phrase while keeping its original
// state at the right edge. Manual event erasing is intentionally different and
// may remove a whole lane; callers use their normal eraser for that operation.
AutomationEditResult eraseAutomationRange(const std::vector<AutomationEvent>&,
                                         const AutomationScope&);

// Value/box transform of scoped CC. The original snapshot supplies every
// preview. Source and destination edge states follow the same rules as above.
// This function supports timeScale > 0; CC reverse requires a separate step-
// envelope policy and is deliberately rejected rather than guessed.
AutomationEditResult transformAutomation(const std::vector<AutomationEvent>&,
                                        const AutomationScope&,
                                        const ValueTransform&);

// Stable last-event-wins for exact same (time,source,controller). Call again
// AFTER final tick rounding, so collapsed samples follow the same rule in both
// adapters. This does not infer defaults or drop equal values at different times.
// Relative controllers 96/97 and channel-mode 120..127 remain in full original
// order: repeated messages can be meaningful actions rather than state values.
std::vector<AutomationEvent> coalesceAutomationEvents(std::vector<AutomationEvent>);

} // namespace pianoroll
