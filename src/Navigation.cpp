#include <PianoRollCore/Navigation.h>

namespace pianoroll {
namespace {
double finiteOr(double value, double fallback) noexcept {
    return std::isfinite(value) ? value : fallback;
}
double clamp(double value, double lo, double hi) noexcept {
    return std::max(lo, std::min(hi, value));
}
double ratioAt(double value, double origin, double size) noexcept {
    return size > 0 && std::isfinite(size) ? clamp((value - origin) / size, 0, 1) : .5;
}
bool validArea(Rect area) noexcept {
    return std::isfinite(area.x) && std::isfinite(area.y) &&
           std::isfinite(area.width) && std::isfinite(area.height) && area.width > 0 && area.height > 0;
}
bool validPoint(Point point) noexcept { return std::isfinite(point.x) && std::isfinite(point.y); }
}
ViewState clampNavigation(ViewState view, const NavigationLimits& limits) noexcept {
    const double minTime = std::max(1e-9, finiteOr(limits.minimumTimeSpan, .25));
    const double maxTime = std::max(minTime, finiteOr(limits.maximumTimeSpan, 128));
    view.timeSpan = clamp(finiteOr(view.timeSpan, minTime), minTime, maxTime);
    const double timeEnd = std::max(0.0, finiteOr(limits.timeEnd, maxTime));
    view.timeStart = clamp(finiteOr(view.timeStart, 0), 0, std::max(0.0, timeEnd - view.timeSpan));
    const double minPitch = clamp(finiteOr(limits.minimumPitchSpan, 12), 1, 128);
    const double count = clamp(finiteOr(view.pitchSpan(), minPitch), minPitch, 128);
    view.pitchHigh = clamp(finiteOr(view.pitchHigh, 84), count - 1, 127);
    view.pitchLow = view.pitchHigh - count + 1;
    return view;
}
ViewState zoomPitch(ViewState view, double requestedSpan, double anchorPitch,
                    double anchorRatio, const NavigationLimits& limits) noexcept {
    view = clampNavigation(view, limits);
    const double count = clamp(finiteOr(requestedSpan, view.pitchSpan()),
                               clamp(finiteOr(limits.minimumPitchSpan, 12), 1, 128), 128);
    anchorRatio = clamp(finiteOr(anchorRatio, .5), 0, 1);
    anchorPitch = finiteOr(anchorPitch, view.pitchHigh + .5 - anchorRatio * view.pitchSpan());
    view.pitchHigh = clamp(anchorPitch + anchorRatio * count - .5, count - 1, 127);
    view.pitchLow = view.pitchHigh - count + 1;
    return view;
}
ViewState zoomTime(ViewState view, double requestedSpan, double anchorTime,
                   double anchorRatio, const NavigationLimits& limits) noexcept {
    view = clampNavigation(view, limits);
    view.timeSpan = finiteOr(requestedSpan, view.timeSpan);
    view = clampNavigation(view, limits); // clamp the size before placing its anchor
    view.timeStart = finiteOr(anchorTime, view.timeStart) - clamp(finiteOr(anchorRatio, .5), 0, 1) * view.timeSpan;
    return clampNavigation(view, limits);
}
NavigationDrag beginMiddleDrag(PointerRegion region, Point point, ViewState view, Rect area) noexcept {
    NavigationDrag drag{view, area, point, NavigationKind::none};
    if (!validArea(area) || !validPoint(point)) return drag;
    if (region == PointerRegion::keys) drag.kind = NavigationKind::pitchZoom;
    else if (region == PointerRegion::notes || region == PointerRegion::controllerLane)
        drag.kind = NavigationKind::pan;
    return drag;
}
NavigationResult dragNavigation(const NavigationDrag& drag, Point current,
                                const NavigationLimits& limits) noexcept {
    auto view = clampNavigation(drag.initial, limits);
    if (!validArea(drag.area) || !validPoint(current) || !validPoint(drag.anchor)) return {view};
    if (drag.kind == NavigationKind::pitchZoom) {
        const double ratio = ratioAt(drag.anchor.y, drag.area.y, drag.area.height);
        const double anchor = view.pitchHigh + .5 - ratio * view.pitchSpan();
        const double factor = std::exp(clamp((current.y - drag.anchor.y) * .005, -50, 50));
        return {zoomPitch(view, view.pitchSpan() * factor, anchor, ratio, limits), drag.kind};
    }
    if (drag.kind == NavigationKind::pan) {
        view.timeStart -= (current.x - drag.anchor.x) / drag.area.width * view.timeSpan;
        const double shift = clamp((current.y - drag.anchor.y) / drag.area.height * view.pitchSpan(),
                                   -view.pitchLow, 127 - view.pitchHigh);
        view.pitchLow += shift;
        view.pitchHigh += shift;
        return {clampNavigation(view, limits), drag.kind};
    }
    return {view};
}
NavigationResult wheelNavigation(ViewState view, Rect area, Point point,
    double deltaX, double deltaY, InputModifiers mods, const NavigationLimits& limits) noexcept {
    view = clampNavigation(view, limits);
    if (!validArea(area) || !validPoint(point) || !std::isfinite(deltaX) || !std::isfinite(deltaY) ||
        (deltaX == 0 && deltaY == 0)) return {view};
    if (mods.command && mods.shift) {
        if (deltaY == 0) return {view};
        const double ratio = ratioAt(point.y, area.y, area.height);
        const double anchor = view.pitchHigh + .5 - ratio * view.pitchSpan();
        const double factor = std::pow(2.0, clamp(-deltaY * .8, -50, 50));
        return {zoomPitch(view, view.pitchSpan() * factor, anchor, ratio, limits), NavigationKind::pitchZoom};
    }
    if (mods.command) {
        if (deltaY == 0) return {view};
        const double ratio = ratioAt(point.x, area.x, area.width);
        const double anchor = view.timeStart + ratio * view.timeSpan;
        const double factor = std::pow(2.0, clamp(-deltaY * 2.5, -50, 50));
        return {zoomTime(view, view.timeSpan * factor, anchor, ratio, limits), NavigationKind::timeZoom};
    }
    if (mods.shift || std::abs(deltaX) > .01) {
        view.timeStart -= (mods.shift ? deltaY + deltaX : deltaX) * view.timeSpan * .7;
        return {clampNavigation(view, limits), NavigationKind::pan};
    }
    if (deltaY == 0) return {view};
    const double shift = clamp(deltaY > 0 ? 2.0 : -2.0, -view.pitchLow, 127 - view.pitchHigh);
    view.pitchLow += shift;
    view.pitchHigh += shift;
    return {view, NavigationKind::pan};
}
ViewState PlayheadFollow::update(ViewState view, double playTime, bool playing, bool gestureActive,
                                const NavigationLimits& limits) const noexcept {
    if (!enabled_ || suspended_ || !playing || gestureActive || !std::isfinite(playTime)) return view;
    if (playTime < view.timeStart || playTime >= view.timeStart + view.timeSpan) {
        view.timeStart = std::max(0.0, playTime - view.timeSpan * .1);
        return clampNavigation(view, limits);
    }
    return view;
}
std::string pitchName(int midiPitch, int middleCOctave, bool flats) {
    if (midiPitch < 0 || midiPitch > 127) return {};
    static constexpr const char* sharps[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    static constexpr const char* flatNames[] = {"C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B"};
    return std::string((flats ? flatNames : sharps)[midiPitch % 12]) + std::to_string(midiPitch / 12 + middleCOctave - 5);
}
bool showPitchLabel(double rowHeight, double availableWidth, bool isC) noexcept {
    return std::isfinite(rowHeight) && std::isfinite(availableWidth) && availableWidth >= 25 &&
           rowHeight >= (isC ? 5 : 11);
}
} // namespace pianoroll
