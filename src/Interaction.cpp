#include <PianoRollCore/Interaction.h>
#include <cctype>

namespace pianoroll {
EditorCommand editorCommand(const ShortcutInput& input) noexcept {
    if (input.textInputFocused) return EditorCommand::none;
    const auto mods = input.modifiers;
    if (input.key == ShortcutKey::escape) return EditorCommand::escape;
    if (mods.command && !mods.alt && input.key == ShortcutKey::character) {
        switch (std::toupper(static_cast<unsigned char>(input.character))) {
            case 'Z': return mods.shift ? EditorCommand::redo : EditorCommand::undo;
            case 'Y': return EditorCommand::redo;
            case 'D': return EditorCommand::deselect;
            case 'A': return EditorCommand::selectAll;
            case 'C': return EditorCommand::copy;
            case 'X': return EditorCommand::cut;
            case 'V': return EditorCommand::paste;
        }
    }
    switch (input.key) {
        case ShortcutKey::deleteKey: case ShortcutKey::backspace: return EditorCommand::erase;
        case ShortcutKey::up: return EditorCommand::up;
        case ShortcutKey::down: return EditorCommand::down;
        case ShortcutKey::left: return EditorCommand::left;
        case ShortcutKey::right: return EditorCommand::right;
        default: break;
    }
    if (!mods.command && !mods.alt && !mods.shift) {
        if (input.key == ShortcutKey::space || input.key == ShortcutKey::enter) return EditorCommand::playPause;
        if (input.key == ShortcutKey::home) return EditorCommand::home;
        if (input.key == ShortcutKey::character && input.character == '1') return EditorCommand::pencil;
        if (input.key == ShortcutKey::character && input.character == '2') return EditorCommand::pointer;
    }
    return EditorCommand::none;
}
void RulerGesture::begin(Point point, ViewState view, Rect area) noexcept {
    down_ = point; view_ = view; area_ = area;
    active_ = std::isfinite(point.x) && std::isfinite(point.y) && std::isfinite(area.x) &&
              std::isfinite(area.width) && area.width > 0 && std::isfinite(view.timeStart) &&
              std::isfinite(view.timeSpan) && view.timeSpan > 0;
}
double RulerGesture::previewTime(Point point) const noexcept {
    if (!active_ || !std::isfinite(point.x)) return 0;
    const double ratio = std::max(0.0, std::min(1.0, (point.x - area_.x) / area_.width));
    return std::max(0.0, view_.timeStart + ratio * view_.timeSpan);
}
RulerResult RulerGesture::finish(Point release, const SnapPolicy& snap, double threshold) noexcept {
    if (!active_ || !std::isfinite(release.x) || !std::isfinite(release.y)) { cancel(); return {}; }
    const double first = previewTime(down_), last = previewTime(release);
    const bool click = isPointerClick(down_, release, threshold);
    cancel();
    if (click) return {RulerAction::seek, last, last};
    const double a = snapTime(first, snap), b = snapTime(last, snap);
    if (a == b) return {RulerAction::seek, last, last};
    return {RulerAction::setLoop, std::min(a, b), std::max(a, b)};
}
bool isPointerClick(Point down, Point current, double threshold) noexcept {
    return std::isfinite(down.x) && std::isfinite(down.y) && std::isfinite(current.x) &&
           std::isfinite(current.y) && std::hypot(current.x - down.x, current.y - down.y) <=
           std::max(0.0, threshold);
}
void GroupScope::choose(int group) {
    if (group < 0) return;
    all = false; groups = {group}; writeGroup = group;
}
void GroupScope::chooseAll() noexcept { all = true; groups.clear(); }
void GroupScope::toggle(int group) {
    if (group < 0) return;
    if (all) { choose(group); return; }
    if (groups.erase(group)) {
        if (writeGroup == group) writeGroup = groups.empty() ? -1 : *groups.rbegin();
    } else { groups.insert(group); writeGroup = group; }
}
void GroupScope::activate(int group) {
    if (group < 0) return;
    if (all || !contains(group)) choose(group);
    else writeGroup = group;
}
MoveDelta constrainNoteDrag(MoveDelta delta, bool shiftAtMouseDown, InputModifiers current) noexcept {
    if (current.command) delta.time = 0;
    if (current.shift && !shiftAtMouseDown) delta.pitch = 0;
    return delta;
}
int laneValueAt(double y, Rect area, int minimum, int maximum) noexcept {
    if (maximum < minimum) std::swap(minimum, maximum);
    if (!std::isfinite(y) || !std::isfinite(area.y) || !std::isfinite(area.height) || area.height <= 0) return minimum;
    const double ratio = std::max(0.0, std::min(1.0, (area.bottom() - y) / area.height));
    // MIDI lanes map zero to their bottom; velocity subsequently clamps to one.
    return std::max(minimum, std::min(maximum, static_cast<int>(std::round(ratio * maximum))));
}
int relativeLaneValue(int baseline, int anchorBaseline, int requestedAnchor, int minimum, int maximum) noexcept {
    if (maximum < minimum) std::swap(minimum, maximum);
    const auto result = static_cast<long long>(baseline) + requestedAnchor - anchorBaseline;
    return static_cast<int>(std::max(static_cast<long long>(minimum), std::min(static_cast<long long>(maximum), result)));
}
int velocityWheelDelta(double deltaY) noexcept {
    return !std::isfinite(deltaY) || deltaY == 0 ? 0 : (deltaY > 0 ? 1 : -1);
}
LaneStrokeSample sampleLaneStroke(double noteX, Rect area, Point previous, Point current,
                                 bool first, double tolerance, int minimum, int maximum) noexcept {
    if (!std::isfinite(noteX) || !std::isfinite(current.x) || !std::isfinite(current.y) ||
        !std::isfinite(previous.x) || !std::isfinite(previous.y) ||
        area.width <= 0 || area.height <= 0 || noteX < area.x || noteX > area.right()) return {};
    const double dx = current.x - previous.x;
    if (first || std::abs(dx) < .01) {
        if (std::abs(noteX - current.x) > std::max(0.0, tolerance)) return {};
        return {true, laneValueAt(current.y, area, minimum, maximum)};
    }
    if (noteX < std::min(previous.x, current.x) || noteX > std::max(previous.x, current.x)) return {};
    const double y = previous.y + (current.y - previous.y) * (noteX - previous.x) / dx;
    return {true, laneValueAt(y, area, minimum, maximum)};
}
} // namespace pianoroll
