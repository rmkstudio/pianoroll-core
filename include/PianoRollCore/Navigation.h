// Shared navigation behavior for JUCE and other piano-roll adapters.
#pragma once
#include "Core.h"
#include <string>

namespace pianoroll {
struct ViewState {
    double timeStart = 0, timeSpan = 16;
    double pitchLow = 36, pitchHigh = 84;
    double pitchSpan() const noexcept { return pitchHigh - pitchLow + 1; }
};
struct NavigationLimits {
    double minimumTimeSpan = .25, maximumTimeSpan = 128, timeEnd = 128;
    double minimumPitchSpan = 12;
};
struct InputModifiers { bool command = false, shift = false, alt = false; };
enum class PointerRegion { outside, keys, notes, controllerLane, ruler };
enum class NavigationKind { none, pan, pitchZoom, timeZoom };
struct NavigationResult { ViewState view; NavigationKind kind = NavigationKind::none; };
struct NavigationDrag {
    ViewState initial;
    Rect area;
    Point anchor;
    NavigationKind kind = NavigationKind::none;
};
ViewState clampNavigation(ViewState, const NavigationLimits&) noexcept;
ViewState zoomPitch(ViewState, double requestedSpan, double anchorPitch,
                    double anchorRatio, const NavigationLimits&) noexcept;
ViewState zoomTime(ViewState, double requestedSpan, double anchorTime,
                   double anchorRatio, const NavigationLimits&) noexcept;
NavigationDrag beginMiddleDrag(PointerRegion, Point, ViewState, Rect noteArea) noexcept;
NavigationResult dragNavigation(const NavigationDrag&, Point,
                                const NavigationLimits&) noexcept;
NavigationResult wheelNavigation(ViewState, Rect noteArea, Point pointer,
    double deltaX, double deltaY, InputModifiers, const NavigationLimits&) noexcept;
// Follow resumes after an explicit seek/play; manual navigation suspends it.
class PlayheadFollow {
public:
    void setEnabled(bool value) noexcept { enabled_ = value; suspended_ = false; }
    bool enabled() const noexcept { return enabled_; }
    bool suspended() const noexcept { return suspended_; }
    void userNavigated() noexcept { suspended_ = true; }
    void transportLocated() noexcept { suspended_ = false; }
    ViewState update(ViewState, double playTime, bool playing, bool gestureActive,
                     const NavigationLimits&) const noexcept;
private:
    bool enabled_ = true, suspended_ = false;
};
// MIDI 60 is C4 by default; octave offset may preserve the application's naming convention.
std::string pitchName(int midiPitch, int middleCOctave = 4, bool flats = false);
bool showPitchLabel(double rowHeight, double availableWidth, bool isC = false) noexcept;
} // namespace pianoroll
