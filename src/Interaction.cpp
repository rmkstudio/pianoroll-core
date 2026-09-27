#include <PianoRollCore/Interaction.h>
#include <cctype>
#include <limits>

namespace pianoroll {
namespace {
bool sameEditableState(const ActionNote& a, const ActionNote& b) noexcept {
    return a.start == b.start && a.end == b.end && a.pitch == b.pitch &&
           a.velocity == b.velocity && a.source == b.source &&
           a.originalIndex == b.originalIndex && a.editable == b.editable &&
           a.keySwitch == b.keySwitch && a.generated == b.generated;
}
double safeDeadZone(double value) noexcept {
    return std::isfinite(value) ? std::max(0.0, value) : 4.0;
}
SnapPolicy effectiveSnap(const SnapPolicy& input, InputModifiers modifiers) noexcept {
    auto snap = input;
    if (std::isnan(snap.minimum)) snap.minimum = 0;
    // FL exposes Alt as the transient snap/unsnap modifier.
    if (modifiers.alt) snap.step = 0;
    return snap;
}
double snappedGestureDelta(double anchor, double requested,
                           const NoteGestureOptions& options,
                           InputModifiers modifiers) noexcept {
    if (options.flGrid.enabled && !modifiers.alt)
        return flGridSnappedDelta(anchor, requested, options.flGrid);
    return snappedDelta(anchor, requested, effectiveSnap(options.snap, modifiers));
}
double quantizedGestureTime(double time, const NoteGestureOptions& options,
                            InputModifiers modifiers) noexcept {
    if (options.flGrid.enabled && !modifiers.alt)
        return flGridQuantizeTime(time, options.flGrid);
    return snapTime(time, effectiveSnap(options.snap, modifiers));
}
}

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
        if (input.key == ShortcutKey::character && input.character == '3') return EditorCommand::eraser;
        if (input.key == ShortcutKey::character && input.character == '4') return EditorCommand::slice;
        if (input.key == ShortcutKey::character && input.character == '5') return EditorCommand::glue;
        if (input.key == ShortcutKey::character && input.character == '6') return EditorCommand::paint;
        if (input.key == ShortcutKey::character && input.character == '7') return EditorCommand::mute;
    }
    return EditorCommand::none;
}
ToolMode toolModeForCommand(EditorCommand command) noexcept {
    switch (command) {
        case EditorCommand::pencil: return ToolMode::pencil;
        case EditorCommand::pointer: return ToolMode::pointer;
        case EditorCommand::eraser: return ToolMode::eraser;
        case EditorCommand::slice: return ToolMode::slice;
        case EditorCommand::glue: return ToolMode::glue;
        case EditorCommand::paint: return ToolMode::paint;
        case EditorCommand::mute: return ToolMode::mute;
        default: return ToolMode::pointer;
    }
}
const char* toolModeName(ToolMode mode) noexcept {
    switch (mode) {
        case ToolMode::pointer: return "pointer";
        case ToolMode::pencil: return "pencil";
        case ToolMode::eraser: return "eraser";
        case ToolMode::slice: return "slice";
        case ToolMode::glue: return "glue";
        case ToolMode::paint: return "paint";
        case ToolMode::mute: return "mute";
        default: return "pointer";
    }
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
double smoothLaneValue(double previous, double target, double response) noexcept {
    if (!std::isfinite(target)) return std::isfinite(previous) ? previous : 0;
    if (!std::isfinite(previous)) previous = target;
    if (!std::isfinite(response)) response = .35;
    response = std::clamp(response, 0.0, 1.0);
    return previous + (target - previous) * response;
}
int smoothedLaneValue(double previous, double target, double response,
                      int minimum, int maximum) noexcept {
    if (maximum < minimum) std::swap(minimum, maximum);
    const auto value = static_cast<int>(std::lround(smoothLaneValue(previous, target, response)));
    return std::clamp(value, minimum, maximum);
}
double velocityLightness(int velocity) noexcept {
    velocity = std::clamp(velocity, 1, 127);
    const double t = static_cast<double>(velocity - 1) / 126.0;
    // Keep the difference subtle: weak notes are washed toward the lane
    // background, while strong notes retain the saturated note colour.
    return .92 - .24 * t;
}

Rect lassoResizeHandle(Rect lasso, double size) noexcept {
    if (!std::isfinite(size) || size <= 0) size = 12;
    const double half = size * .5;
    return {lasso.right() - half, lasso.y + lasso.height * .5 - half, size, size};
}
bool hitLassoResizeHandle(Point point, Rect lasso, double size) noexcept {
    return lasso.width > 0 && lasso.height > 0 &&
           lassoResizeHandle(lasso, size).contains(point);
}
PointerFeedback pointerFeedback(Point point, const Viewport& view,
                                const std::vector<ActionNote>& notes,
                                InputModifiers modifiers,
                                const NoteGestureOptions& options,
                                NoteGesturePhase phase, Rect lasso,
                                NoteHit pressed) noexcept {
    PointerFeedback result;
    result.ctrlSelect = modifiers.command;
    result.showLassoHint = (phase == NoteGesturePhase::lassoing ||
                            phase == NoteGesturePhase::idle ||
                            phase == NoteGesturePhase::dragging ||
                            phase == NoteGesturePhase::committed) &&
                           lasso.width > 0 && lasso.height > 0;
    if (result.showLassoHint) {
        result.showResizeHandle = true;
        result.resizeHandle = lassoResizeHandle(lasso);
        result.cursor = hitLassoResizeHandle(point, lasso) ?
            CursorHint::resizeHorizontal :
            (phase == NoteGesturePhase::lassoing ? CursorHint::lasso : CursorHint::pointer);
    }
    auto candidateAt = [&](std::size_t i) {
        const auto& note = notes[i];
        const auto rect = view.noteRect(note.start, note.end, note.pitch,
                                        options.noteHeight, options.minimumWidth,
                                        .5 - options.noteHeight / (2 * view.rowHeight));
        const double cx = rect.x + rect.width * .5;
        const double cy = rect.y + rect.height * .5;
        return HitCandidate{rect, std::hypot(point.x - cx, point.y - cy),
                            note.editable && (options.includeKeySwitches || !note.keySwitch)};
    };
    result.hit = noteHitTest(notes.size(), point, candidateAt, options.edgeZone,
                             options.hitPolicy);
    if (result.hit.valid()) {
        result.cursor = result.hit.kind == NoteHitKind::body ? CursorHint::move :
                         CursorHint::resizeHorizontal;
        const auto& note = notes[result.hit.index];
        if (pressed.valid() && pressed.index == result.hit.index)
            result.note = NoteFeedbackState::pressed;
        else if (note.selected)
            result.note = NoteFeedbackState::selected;
        else
            result.note = NoteFeedbackState::hover;
        if (modifiers.command) result.cursor = CursorHint::select;
    } else if (!result.showLassoHint) {
        result.cursor = modifiers.command ? CursorHint::select : CursorHint::pencil;
    }
    return result;
}

bool NoteGesture::eligible(const ActionNote& note) const noexcept {
    return note.editable && (options_.includeKeySwitches || !note.keySwitch);
}

bool NoteGesture::crossedDeadZone(Point point) const noexcept {
    return isPointerClick(down_, point, safeDeadZone(options_.deadZone)) == false;
}

void NoteGesture::mouseDown(Point point, Viewport view, const std::vector<ActionNote>& notes,
                            std::uint64_t revision, InputModifiers modifiers,
                            NoteGestureOptions options, std::size_t preferredHit) {
    cancel();
    options_ = options;
    if (!std::isfinite(options_.minimumTime)) options_.minimumTime = 0;
    if (!std::isfinite(options_.maximumTime) || options_.maximumTime <= options_.minimumTime)
        options_.maximumTime = 1.0e12;
    if (!std::isfinite(options_.minimumLength) || options_.minimumLength <= 0)
        options_.minimumLength = 1.0e-9;
    if (!std::isfinite(options_.noteHeight) || options_.noteHeight <= 0)
        options_.noteHeight = 8.0;
    if (!std::isfinite(options_.minimumWidth) || options_.minimumWidth < 0)
        options_.minimumWidth = 2.0;
    if (!std::isfinite(options_.edgeZone) || options_.edgeZone < 0)
        options_.edgeZone = 5.0;
    if (options_.maximumPitch < options_.minimumPitch) std::swap(options_.maximumPitch, options_.minimumPitch);
    options_.minimumPitch = std::max(0, options_.minimumPitch);
    options_.maximumPitch = std::min(127, options_.maximumPitch);
    view_ = view;
    down_ = last_ = point;
    lasso_ = {};
    hit_ = {};
    mode_ = NoteGestureMode::none;
    phase_ = NoteGesturePhase::idle;
    downModifiers_ = modifiers;
    copyPending_ = false;
    hasCopy_ = false;
    copyPlan_ = {};
    gestureIndices_.clear();
    baseline_ = notes;
    draft_ = notes;

    if (!std::isfinite(point.x) || !std::isfinite(point.y) ||
        !std::isfinite(view_.area.width) || !std::isfinite(view_.area.height) ||
        view_.area.width <= 0 || view_.area.height <= 0 ||
        !std::isfinite(view_.timeSpan) || view_.timeSpan <= 0 ||
        !std::isfinite(view_.rowHeight) || view_.rowHeight <= 0) return;

    auto candidateAt = [&](std::size_t i) {
        const auto& note = draft_[i];
        const auto rect = view_.noteRect(note.start, note.end, note.pitch,
                                         options_.noteHeight, options_.minimumWidth,
                                         .5 - options_.noteHeight / (2 * view_.rowHeight));
        const double cx = rect.x + rect.width * .5;
        const double cy = rect.y + rect.height * .5;
        return HitCandidate{rect, std::hypot(point.x - cx, point.y - cy), eligible(note)};
    };
    hit_ = noteHitTest(draft_.size(), point, candidateAt, options_.edgeZone, options_.hitPolicy);
    // An adapter may supply the next member of an overlap stack.  Validate it
    // against the same geometry before accepting it so stale cycle state can
    // never redirect a click outside the pointer's actual hit area.
    if (preferredHit < draft_.size()) {
        const auto preferred = candidateAt(preferredHit);
        if (preferred.eligible) {
            const auto one = noteHitTest(1, point,
                [&](std::size_t) { return preferred; }, options_.edgeZone,
                HitPolicy::lastPainted);
            if (one.valid()) hit_ = {preferredHit, one.kind, one.distance};
        }
    }
    if (hit_.valid()) {
        mode_ = hit_.kind == NoteHitKind::leftEdge ? NoteGestureMode::resizeLeft :
                 hit_.kind == NoteHitKind::rightEdge ? NoteGestureMode::resizeRight :
                 NoteGestureMode::move;
        if (options_.selectHit && !draft_[hit_.index].selected) {
            for (auto& note : draft_) note.selected = false;
            draft_[hit_.index].selected = true;
            baseline_ = draft_; // selection is a gesture baseline, not an undo edit.
        }
        for (std::size_t i = 0; i < baseline_.size(); ++i)
            if (baseline_[i].selected && eligible(baseline_[i])) gestureIndices_.push_back(i);
        if (gestureIndices_.empty()) gestureIndices_.push_back(hit_.index);
        copyPending_ = options_.copyOnShift && modifiers.shift && mode_ == NoteGestureMode::move;
    } else {
        mode_ = NoteGestureMode::lasso;
        if (options_.blankClickDeselect) {
            for (auto& note : draft_) note.selected = false;
            baseline_ = draft_;
        }
    }
    edit_.begin(revision);
    phase_ = NoteGesturePhase::armed;
}

void NoteGesture::beginSelectionScale(Point point, Viewport view,
                                      const std::vector<ActionNote>& notes,
                                      std::uint64_t revision, Rect selectionBounds,
                                      InputModifiers modifiers,
                                      NoteGestureOptions options) {
    cancel();
    options_ = options;
    if (!std::isfinite(options_.minimumTime)) options_.minimumTime = 0;
    if (!std::isfinite(options_.maximumTime) || options_.maximumTime <= options_.minimumTime)
        options_.maximumTime = 1.0e12;
    if (!std::isfinite(options_.minimumLength) || options_.minimumLength <= 0)
        options_.minimumLength = 1.0e-9;
    if (!std::isfinite(options_.noteHeight) || options_.noteHeight <= 0)
        options_.noteHeight = 8.0;
    if (!std::isfinite(options_.minimumWidth) || options_.minimumWidth < 0)
        options_.minimumWidth = 2.0;
    view_ = view;
    down_ = last_ = point;
    lasso_ = selectionBounds;
    hit_ = {};
    mode_ = NoteGestureMode::scaleSelection;
    phase_ = NoteGesturePhase::idle;
    downModifiers_ = modifiers;
    copyPending_ = hasCopy_ = false;
    copyPlan_ = {};
    gestureIndices_.clear();
    baseline_ = notes;
    draft_ = notes;
    for (std::size_t i = 0; i < baseline_.size(); ++i)
        if (baseline_[i].selected && eligible(baseline_[i])) gestureIndices_.push_back(i);
    if (gestureIndices_.empty() || !std::isfinite(selectionBounds.x) ||
        !std::isfinite(selectionBounds.width) || selectionBounds.width <= 0 ||
        !std::isfinite(view_.timeSpan) || view_.timeSpan <= 0) {
        mode_ = NoteGestureMode::none;
        return;
    }
    edit_.begin(revision);
    phase_ = NoteGesturePhase::armed;
}

void NoteGesture::beginCopyIfNeeded() {
    if (!copyPending_ || hasCopy_ || mode_ != NoteGestureMode::move) return;
    copyPlan_ = makeCopyPlan(gestureIndices_, baseline_.size(), hit_.index,
                             [&](std::size_t i) { return eligible(baseline_[i]); });
    if (copyPlan_.entries.empty() || copyPlan_.anchor == noIndex) {
        copyPending_ = false;
        return;
    }
    hasCopy_ = true;
    copyPending_ = false;
    gestureIndices_.clear();
    for (const auto& entry : copyPlan_.entries) gestureIndices_.push_back(entry.target);
    hit_.index = copyPlan_.anchor;
}

void NoteGesture::rebuildDraftWithCopies() {
    draft_ = baseline_;
    if (!hasCopy_) return;
    for (const auto& entry : copyPlan_.entries) {
        if (entry.source >= baseline_.size()) continue;
        auto copy = baseline_[entry.source];
        copy.generated = true;
        if (copy.originalIndex == noIndex) copy.originalIndex = entry.source;
        copy.selected = true;
        draft_[entry.source].selected = false;
        draft_.push_back(copy);
    }
}

void NoteGesture::markIfChanged(const std::vector<ActionNote>& before) noexcept {
    (void)before;
    bool changed = baseline_.size() != draft_.size();
    if (!changed) {
        for (std::size_t i = 0; i < baseline_.size(); ++i)
            if (!sameEditableState(baseline_[i], draft_[i])) { changed = true; break; }
    }
    edit_.markChanged(changed);
}

void NoteGesture::updateMove(Point point, InputModifiers modifiers) {
    beginCopyIfNeeded();
    rebuildDraftWithCopies();
    const auto before = draft_;
    const auto& anchor = hasCopy_ ? draft_[hit_.index] : baseline_[hit_.index];
    const double requestedTime = view_.xToTime(point.x) - view_.xToTime(down_.x);
    const int requestedPitch = view_.yToPitch(point.y, Viewport::PitchHit::ceilFromTop) -
                               view_.yToPitch(down_.y, Viewport::PitchHit::ceilFromTop);
    auto delta = constrainNoteDrag({requestedTime, requestedPitch}, downModifiers_.shift, modifiers);
    delta.time = snappedGestureDelta(anchor.start, delta.time, options_, modifiers);

    GroupBounds bounds;
    for (const auto& note : draft_)
        if (note.selected) bounds.include(note.start, note.end, note.pitch, eligible(note));
    delta = clampMove(bounds, delta, options_.minimumTime,
                       options_.minimumPitch, options_.maximumPitch, options_.maximumTime);
    for (auto index : gestureIndices_) {
        if (index >= draft_.size() || !eligible(draft_[index])) continue;
        draft_[index].start += delta.time;
        draft_[index].end += delta.time;
        draft_[index].pitch = std::clamp(draft_[index].pitch + delta.pitch,
                                         options_.minimumPitch, options_.maximumPitch);
    }
    markIfChanged(before);
}

void NoteGesture::updateResize(Point point, InputModifiers modifiers) {
    rebuildDraftWithCopies();
    const auto before = draft_;
    if (hit_.index >= draft_.size()) return;
    auto& note = draft_[hit_.index];
    const auto& original = hasCopy_ ? baseline_[copyPlan_.entries.front().source] : baseline_[hit_.index];
    const double pointerDelta = view_.xToTime(point.x) - view_.xToTime(down_.x);
    if (mode_ == NoteGestureMode::resizeLeft) {
        const double proposed = quantizedGestureTime(original.start + pointerDelta,
                                                     options_, modifiers);
        const double maximum = original.end - options_.minimumLength;
        note.start = std::max(options_.minimumTime, std::min(proposed, maximum));
    } else {
        const double proposed = quantizedGestureTime(original.end + pointerDelta,
                                                     options_, modifiers);
        const double minimum = original.start + options_.minimumLength;
        note.end = std::max(minimum, std::min(proposed, options_.maximumTime));
    }
    markIfChanged(before);
}

void NoteGesture::updateScale(Point point, InputModifiers modifiers) {
    (void)modifiers;
    const double left = view_.xToTime(lasso_.x);
    const double right = view_.xToTime(lasso_.right());
    const double span = right - left;
    if (!std::isfinite(span) || span <= options_.minimumLength) return;
    double targetRight = view_.xToTime(point.x);
    if (options_.flGrid.enabled && !modifiers.alt)
        targetRight = flGridQuantizeTime(targetRight, options_.flGrid);
    else
        targetRight = snapTime(targetRight, effectiveSnap(options_.snap, modifiers));
    const double factor = std::clamp((targetRight - left) / span, .05, 100.0);
    const auto before = draft_;
    draft_ = baseline_;
    for (auto index : gestureIndices_) {
        if (index >= draft_.size() || !eligible(draft_[index])) continue;
        auto& note = draft_[index];
        const auto& original = baseline_[index];
        note.start = std::clamp(left + (original.start - left) * factor,
                                options_.minimumTime, options_.maximumTime);
        note.end = std::clamp(left + (original.end - left) * factor,
                              note.start + options_.minimumLength, options_.maximumTime);
    }
    markIfChanged(before);
}

void NoteGesture::updateLasso(Point point) {
    const auto before = draft_;
    lasso_ = Rect::fromPoints(down_, point);
    draft_ = baseline_;
    if (options_.lassoAdds) {
        for (std::size_t i = 0; i < draft_.size(); ++i)
            if (eligible(draft_[i]) && view_.noteRect(draft_[i].start, draft_[i].end, draft_[i].pitch,
                                                       options_.noteHeight, options_.minimumWidth,
                                                       .5 - options_.noteHeight / (2 * view_.rowHeight)).intersects(lasso_))
                draft_[i].selected = true;
    } else {
        for (auto& note : draft_) note.selected = false;
        for (std::size_t i = 0; i < draft_.size(); ++i)
            if (eligible(draft_[i]) && view_.noteRect(draft_[i].start, draft_[i].end, draft_[i].pitch,
                                                       options_.noteHeight, options_.minimumWidth,
                                                       .5 - options_.noteHeight / (2 * view_.rowHeight)).intersects(lasso_))
                draft_[i].selected = true;
    }
    // Selection is kept outside the musical undo transaction.
    if (before.size() == draft_.size()) edit_.markChanged(false);
}

void NoteGesture::mouseMove(Point point, InputModifiers modifiers) {
    if (!edit_.active() || !std::isfinite(point.x) || !std::isfinite(point.y)) return;
    last_ = point;
    if (phase_ == NoteGesturePhase::armed) {
        if (!crossedDeadZone(point)) return;
        phase_ = mode_ == NoteGestureMode::lasso ? NoteGesturePhase::lassoing : NoteGesturePhase::dragging;
    }
    if (phase_ == NoteGesturePhase::dragging) {
        if (mode_ == NoteGestureMode::move) updateMove(point, modifiers);
        else if (mode_ == NoteGestureMode::resizeLeft || mode_ == NoteGestureMode::resizeRight)
            updateResize(point, modifiers);
        else if (mode_ == NoteGestureMode::scaleSelection) updateScale(point, modifiers);
    } else if (phase_ == NoteGesturePhase::lassoing) updateLasso(point);
}

void NoteGesture::cancel() noexcept {
    if (edit_.active()) edit_.cancel();
    draft_ = baseline_;
    mode_ = NoteGestureMode::none;
    phase_ = NoteGesturePhase::canceled;
    copyPending_ = hasCopy_ = false;
    copyPlan_ = {};
    gestureIndices_.clear();
}
} // namespace pianoroll
