#pragma once
#include "Core.h"
#include "Navigation.h"

namespace pianoroll {
enum class ShortcutKey { other, space, enter, home, escape, backspace, deleteKey,
                         up, down, left, right, character };
enum class EditorCommand { none, escape, undo, redo, deselect, selectAll, copy, cut,
                           paste, erase, up, down, left, right, playPause, home,
                           pencil, pointer };
struct ShortcutInput {
    ShortcutKey key = ShortcutKey::other;
    char character = 0;
    InputModifiers modifiers;
    bool textInputFocused = false;
};
// Returns no editor command when a text editor owns focus. Recognised unavailable
// commands must still be consumed by the adapter so they do not leak to the host.
EditorCommand editorCommand(const ShortcutInput&) noexcept;

enum class RulerAction { none, seek, setLoop, clearLoop };
struct RulerResult { RulerAction action = RulerAction::none; double start = 0, end = 0; };
class RulerGesture {
public:
    void begin(Point, ViewState, Rect rulerArea) noexcept;
    void cancel() noexcept { active_ = false; }
    bool active() const noexcept { return active_; }
    double previewTime(Point) const noexcept;
    RulerResult finish(Point release, const SnapPolicy&, double dragThreshold = 4) noexcept;
    static RulerResult clearLoop() noexcept { return {RulerAction::clearLoop, 0, 0}; }
private:
    ViewState view_;
    Rect area_;
    Point down_;
    bool active_ = false;
};
bool isPointerClick(Point down, Point current, double dragThreshold = 4) noexcept;

// Group means a Voice in Lines or Track in Recover. Scope and solo are separate:
// choosing a group never silently mutes another group or selects every note.
struct GroupScope {
    bool all = true;
    std::set<int> groups;
    int writeGroup = 0;
    bool contains(int group) const noexcept { return all || groups.count(group) != 0; }
    bool empty() const noexcept { return !all && groups.empty(); }
    void choose(int group);
    void chooseAll() noexcept;
    void toggle(int group);
    void activate(int group);
};
// Shared edit gesture constraints and controller-lane drawing.
MoveDelta constrainNoteDrag(MoveDelta, bool shiftAtMouseDown, InputModifiers current) noexcept;
int laneValueAt(double y, Rect laneArea, int minimum = 1, int maximum = 127) noexcept;
int relativeLaneValue(int baseline, int anchorBaseline, int requestedAnchor,
                      int minimum = 1, int maximum = 127) noexcept;
int velocityWheelDelta(double deltaY) noexcept;
struct LaneStrokeSample { bool hit = false; int value = 0; };
LaneStrokeSample sampleLaneStroke(double noteX, Rect laneArea, Point previous, Point current,
                                 bool first, double tolerance = 5,
                                 int minimum = 1, int maximum = 127) noexcept;

struct SoloState {
    int group = -1;
    void toggle(int target) noexcept { group = target < 0 || group == target ? -1 : target; }
    void clear() noexcept { group = -1; }
    bool audible(int target) const noexcept { return group < 0 || group == target; }
};
} // namespace pianoroll
