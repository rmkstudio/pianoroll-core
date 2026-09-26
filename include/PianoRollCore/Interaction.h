#pragma once
#include "Core.h"
#include "Navigation.h"
#include "NoteActions.h"
#include <cstdint>
#include <vector>

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

// Shared note-canvas gesture state. The adapter owns the document and undo
// storage; this class owns only the mouse-down snapshot and the current draft.
enum class NoteGestureMode { none, move, resizeLeft, resizeRight, lasso };
enum class NoteGesturePhase { idle, armed, dragging, lassoing, committed, canceled };
struct NoteGestureOptions {
    double deadZone = 4.0;          // pixels before a click becomes an edit
    double edgeZone = 5.0;           // pixels at either note edge
    double noteHeight = 8.0;
    double minimumWidth = 2.0;
    double minimumLength = 1.0e-9;
    double minimumTime = 0;
    double maximumTime = 1.0e12;
    int minimumPitch = 0, maximumPitch = 127;
    SnapPolicy snap{};
    FlTickGrid flGrid{};            // optional FL-style tick-domain snapping
    HitPolicy hitPolicy = HitPolicy::lastPainted;
    bool copyOnShift = true;
    bool selectHit = true;
    bool blankClickDeselect = true;
    bool lassoAdds = false;
    bool includeKeySwitches = false;
};

class NoteGesture {
public:
    void mouseDown(Point, Viewport, const std::vector<ActionNote>&,
                   std::uint64_t revision, InputModifiers = {},
                   NoteGestureOptions = {});
    void mouseMove(Point, InputModifiers = {});
    template<class Commit>
    CommitResult mouseUp(Point release, std::uint64_t currentRevision,
                         InputModifiers current, Commit apply) {
        mouseMove(release, current);
        return commit(currentRevision, apply);
    }
    template<class Commit>
    CommitResult commit(std::uint64_t currentRevision, Commit apply) {
        if (!edit_.active()) return CommitResult::inactive;
        const auto result = edit_.commit(currentRevision, [&] { return apply(draft_); });
        phase_ = result == CommitResult::committed ? NoteGesturePhase::committed :
                 (result == CommitResult::unchanged ? NoteGesturePhase::idle :
                  NoteGesturePhase::canceled);
        mode_ = NoteGestureMode::none;
        return result;
    }
    void cancel() noexcept;
    bool active() const noexcept { return edit_.active(); }
    bool changed() const noexcept { return edit_.changed(); }
    NoteGestureMode mode() const noexcept { return mode_; }
    NoteGesturePhase phase() const noexcept { return phase_; }
    NoteHit hit() const noexcept { return hit_; }
    std::size_t grabbedIndex() const noexcept { return hit_.index; }
    Rect lasso() const noexcept { return lasso_; }
    const std::vector<ActionNote>& baseline() const noexcept { return baseline_; }
    const std::vector<ActionNote>& draft() const noexcept { return draft_; }

private:
    bool eligible(const ActionNote&) const noexcept;
    bool crossedDeadZone(Point) const noexcept;
    void beginCopyIfNeeded();
    void rebuildDraftWithCopies();
    void updateMove(Point, InputModifiers);
    void updateResize(Point, InputModifiers);
    void updateLasso(Point);
    void markIfChanged(const std::vector<ActionNote>& before) noexcept;

    Viewport view_;
    NoteGestureOptions options_;
    Point down_, last_;
    Rect lasso_;
    NoteHit hit_;
    NoteGestureMode mode_ = NoteGestureMode::none;
    NoteGesturePhase phase_ = NoteGesturePhase::idle;
    InputModifiers downModifiers_;
    bool copyPending_ = false;
    bool hasCopy_ = false;
    CopyPlan copyPlan_;
    std::vector<std::size_t> gestureIndices_;
    std::vector<ActionNote> baseline_, draft_;
    EditSession edit_;
};
} // namespace pianoroll
