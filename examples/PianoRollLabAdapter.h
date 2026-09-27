#pragma once

#include <PianoRollCore/PianoRollCore.h>
#include <algorithm>
#include <cstdint>
#include <sstream>
#include <string>
#include <vector>

namespace pianoroll_lab {

using namespace pianoroll;

class Adapter {
public:
    Adapter() { resetDemo(); }

    void resetDemo() {
        gesture_.cancel();
        document_.notes = {
            {1.00, 2.00, 60, 100, 0, 0, true,  true,  false, false},
            {1.00, 2.00, 64,  92, 0, 1, true,  true,  false, false},
            {1.50, 2.50, 67,  84, 0, 2, false, true,  false, false},
            {4.00, 5.00, 60,  76, 0, 3, false, true,  false, false},
            {4.00, 4.50, 60,  62, 0, 4, false, true,  false, false},
            {6.00, 7.00, 72, 110, 0, 5, false, true,  true,  false},
        };
        document_.revision = 1;
        document_.undo.clear();
        viewport_ = {{72, 48, 960, 480}, 0, 16, 84, 12};
        options_ = {};
        options_.snap = {.25, Rounding::nearest, 0, 0};
        options_.flGrid.enabled = true;
        options_.flGrid.ticksPerQuarter = 96;
        options_.flGrid.division = 4;
        options_.noteHeight = 10;
        options_.edgeZone = 6;
        options_.minimumLength = .125;
        middleActive_ = false;
        feedback_ = {};
        overlapStack_.clear();
        overlapCursor_ = noIndex;
        overlapPoint_ = {};
        overlapActive_ = false;
        hasHover_ = false;
        effects_.clear();
        presentationNow_ = 0;
        tool_ = ToolMode::pointer;
    }

    const std::vector<ActionNote>& notes() const noexcept { return document_.notes; }
    std::uint64_t revision() const noexcept { return document_.revision; }
    std::size_t undoCount() const noexcept { return document_.undo.size(); }
    const Viewport& viewport() const noexcept { return viewport_; }
    const NoteGesture& gesture() const noexcept { return gesture_; }
    const PointerFeedback& feedback() const noexcept { return feedback_; }
    const TransientEffects& effects() const noexcept { return effects_; }
    double presentationNow() const noexcept { return presentationNow_; }
    ToolMode tool() const noexcept { return tool_; }
    void setTool(ToolMode mode) noexcept { tool_ = mode; }
    Rect selectionBounds() const noexcept {
        bool have = false;
        Rect result{};
        for (const auto& note : document_.notes) {
            if (!note.selected || !note.editable) continue;
            const auto r = viewport_.noteRect(note.start, note.end, note.pitch,
                                              options_.noteHeight, options_.minimumWidth,
                                              .5 - options_.noteHeight / (2 * viewport_.rowHeight));
            if (!have) { result = r; have = true; continue; }
            const double right = std::max(result.right(), r.right());
            const double bottom = std::max(result.bottom(), r.bottom());
            result.x = std::min(result.x, r.x);
            result.y = std::min(result.y, r.y);
            result.width = right - result.x;
            result.height = bottom - result.y;
        }
        return have ? result : Rect{};
    }

    void resize(int width, int height) noexcept {
        viewport_.area = {72, 48, std::max(200.0, static_cast<double>(width) - 88.0),
                          std::max(160.0, static_cast<double>(height) - 112.0)};
    }

    void hover(Point point, InputModifiers modifiers = {}) noexcept {
        hoverPoint_ = point;
        hoverModifiers_ = modifiers;
        hasHover_ = true;
        const auto hint = gesture_.lasso().width > 0 ? gesture_.lasso() : selectionBounds();
        feedback_ = pointerFeedback(point, viewport_, document_.notes, modifiers,
                                    options_, gesture_.phase(), hint,
                                    gesture_.hit());
    }

    void mouseDown(Point point, InputModifiers modifiers = {}) {
        auto candidateAt = [&](std::size_t i) {
            const auto& note = document_.notes[i];
            const auto rect = viewport_.noteRect(note.start, note.end, note.pitch,
                                                  options_.noteHeight, options_.minimumWidth,
                                                  .5 - options_.noteHeight / (2 * viewport_.rowHeight));
            const double cx = rect.x + rect.width * .5;
            const double cy = rect.y + rect.height * .5;
            return HitCandidate{rect, std::hypot(point.x - cx, point.y - cy),
                                note.editable && (options_.includeKeySwitches || !note.keySwitch)};
        };
        const auto stack = collectNoteHits(document_.notes.size(), point, candidateAt,
                                           options_.edgeZone);
        if (tool_ == ToolMode::eraser) {
            eraseAt(point, candidateAt);
            hover(point, modifiers);
            return;
        }
        if (tool_ == ToolMode::slice) {
            sliceAt(point, stack);
            hover(point, modifiers);
            return;
        }
        if (tool_ == ToolMode::glue) {
            glueAt(point, stack);
            hover(point, modifiers);
            return;
        }
        const auto selected = selectionBounds();
        if (stack.size() <= 1 && hitLassoResizeHandle(point, selected)) {
            overlapActive_ = false;
            gesture_.beginSelectionScale(point, viewport_, document_.notes,
                                         document_.revision, selected, modifiers, options_);
            hover(point, modifiers);
            return;
        }
        std::vector<std::size_t> indices;
        indices.reserve(stack.size());
        for (const auto& hit : stack) indices.push_back(hit.index);
        const bool samePoint = overlapActive_ &&
            std::hypot(point.x - overlapPoint_.x, point.y - overlapPoint_.y) <=
                std::max(5.0, options_.deadZone);
        if (indices.empty()) {
            overlapStack_.clear(); overlapCursor_ = noIndex; overlapActive_ = false;
        } else if (samePoint && indices == overlapStack_) {
            overlapCursor_ = (overlapCursor_ == noIndex || overlapCursor_ == 0) ?
                indices.size() - 1 : overlapCursor_ - 1;
        } else {
            overlapStack_ = indices;
            // Last-painted is FL's first choice; subsequent clicks walk the
            // same stack in reverse so the next note becomes deletable.
            overlapCursor_ = indices.size() - 1;
            overlapActive_ = true;
        }
        overlapPoint_ = point;
        gesture_.mouseDown(point, viewport_, document_.notes, document_.revision,
                           modifiers, options_,
                           overlapCursor_ == noIndex ? noIndex : overlapStack_[overlapCursor_]);
        hover(point, modifiers);
    }

    void mouseMove(Point point, InputModifiers modifiers = {}) {
        gesture_.mouseMove(point, modifiers);
        hover(point, modifiers);
    }

    CommitResult mouseUp(Point point, InputModifiers modifiers = {}) {
        const auto result = gesture_.mouseUp(point, document_.revision, modifiers,
            [&](const std::vector<ActionNote>& draft) {
                document_.undo.push_back(document_.notes);
                document_.notes = draft;
                ++document_.revision;
                return true;
            });
        if (result == CommitResult::unchanged)
            document_.notes = gesture_.draft(); // selection changes are not musical undo items
        else if (result == CommitResult::committed || result == CommitResult::stale)
            overlapActive_ = false;
        if (hasHover_) hover(hoverPoint_, hoverModifiers_);
        return result;
    }

    void cancel() noexcept {
        gesture_.cancel();
        overlapActive_ = false;
        if (hasHover_) hover(hoverPoint_, hoverModifiers_);
    }

    void tick(double nowSeconds) {
        presentationNow_ = nowSeconds;
        effects_.update(nowSeconds);
    }

    bool eraseSelected() {
        std::vector<ActionNote> removed;
        for (const auto& note : document_.notes)
            if (note.selected && note.editable) removed.push_back(note);
        if (removed.empty()) return false;
        for (const auto& note : removed)
            effects_.beginDeleteFade(note, presentationNow_, .10);
        auto it = std::remove_if(document_.notes.begin(), document_.notes.end(),
                                 [](const ActionNote& note) { return note.selected && note.editable; });
        document_.undo.push_back(document_.notes);
        document_.notes.erase(it, document_.notes.end());
        ++document_.revision;
        overlapActive_ = false;
        if (hasHover_) hover(hoverPoint_, hoverModifiers_);
        return true;
    }

    bool undo() {
        if (document_.undo.empty()) return false;
        document_.notes = document_.undo.back();
        document_.undo.pop_back();
        ++document_.revision;
        return true;
    }

    NavigationResult wheel(Point point, double deltaX, double deltaY,
                           InputModifiers modifiers = {}) {
        const auto result = wheelNavigation(viewState(), viewport_.area, point,
                                              deltaX, deltaY, modifiers,
                                              {.25, 256, 256, 12});
        applyViewState(result.view);
        return result;
    }

    void middleDown(Point point, PointerRegion region = PointerRegion::notes) {
        const auto view = viewState();
        middleDrag_ = beginMiddleDrag(region, point,
                                      view,
                                      viewport_.area);
        middleActive_ = middleDrag_.kind != NavigationKind::none;
    }

    void middleMove(Point point) {
        if (!middleActive_) return;
        const auto result = dragNavigation(middleDrag_, point,
                                           {.25, 256, 256, 12});
        applyViewState(result.view);
    }

    void middleUp() noexcept { middleActive_ = false; }

    std::string status() const {
        std::ostringstream out;
        out << "revision=" << document_.revision
            << " notes=" << document_.notes.size()
            << " undo=" << document_.undo.size()
            << " tool=" << toolModeName(tool_)
            << " phase=" << phaseName(gesture_.phase());
        if (gesture_.hit().valid()) {
            out << " hit=" << gesture_.hit().index
                << ":" << hitName(gesture_.hit().kind);
        }
        return out.str();
    }

private:
    template<class CandidateAt>
    void eraseAt(Point point, CandidateAt candidateAt) {
        const auto hit = noteHitTest(document_.notes.size(), point, candidateAt,
                                     options_.edgeZone, HitPolicy::lastPainted);
        if (!hit.valid() || hit.index >= document_.notes.size()) return;
        effects_.beginDeleteFade(document_.notes[hit.index], presentationNow_, .10);
        document_.undo.push_back(document_.notes);
        document_.notes.erase(document_.notes.begin() + static_cast<std::ptrdiff_t>(hit.index));
        ++document_.revision;
        overlapActive_ = false;
    }

    void sliceAt(Point point, const std::vector<NoteHit>& stack) {
        if (stack.empty()) return;
        const auto index = stack.back().index;
        if (index >= document_.notes.size()) return;
        auto working = document_.notes;
        for (auto& note : working) note.selected = false;
        working[index].selected = true;
        const auto result = splitNoteAt(working, index, viewport_.xToTime(point.x));
        if (!result || !result.changed) return;
        document_.undo.push_back(document_.notes);
        document_.notes = result.notes;
        ++document_.revision;
        overlapActive_ = false;
    }

    void glueSelected() {
        const auto result = glueNotes(document_.notes);
        if (!result || !result.changed) return;
        document_.undo.push_back(document_.notes);
        document_.notes = result.notes;
        ++document_.revision;
        overlapActive_ = false;
    }

    void glueAt(Point point, const std::vector<NoteHit>& stack) {
        (void)point;
        if (stack.empty()) { glueSelected(); return; }
        const auto anchorIndex = stack.back().index;
        if (anchorIndex >= document_.notes.size()) return;
        const auto anchor = document_.notes[anchorIndex];
        std::vector<std::size_t> candidates;
        for (std::size_t i = 0; i < document_.notes.size(); ++i) {
            const auto& note = document_.notes[i];
            if (note.editable && note.source == anchor.source && note.pitch == anchor.pitch &&
                note.keySwitch == anchor.keySwitch) candidates.push_back(i);
        }
        std::stable_sort(candidates.begin(), candidates.end(), [&](auto a, auto b) {
            return document_.notes[a].start < document_.notes[b].start;
        });
        const auto anchorPos = std::find(candidates.begin(), candidates.end(), anchorIndex);
        if (anchorPos == candidates.end()) return;
        std::size_t first = static_cast<std::size_t>(anchorPos - candidates.begin());
        std::size_t last = first;
        while (first > 0 && document_.notes[candidates[first - 1]].end + 1.0e-6 >=
                              document_.notes[candidates[first]].start) --first;
        while (last + 1 < candidates.size() &&
               document_.notes[candidates[last + 1]].start <=
                   document_.notes[candidates[last]].end + 1.0e-6) ++last;
        auto working = document_.notes;
        for (auto& note : working) note.selected = false;
        for (std::size_t i = first; i <= last; ++i) working[candidates[i]].selected = true;
        const auto result = glueNotes(working);
        if (!result || !result.changed) return;
        document_.undo.push_back(document_.notes);
        document_.notes = result.notes;
        ++document_.revision;
        overlapActive_ = false;
    }

    ViewState viewState() const noexcept {
        const double pitchHigh = viewport_.topPitch;
        const double pitchLow = pitchHigh - viewport_.area.height / viewport_.rowHeight + 1;
        return {viewport_.timeStart, viewport_.timeSpan, pitchLow, pitchHigh};
    }

    void applyViewState(const ViewState& view) noexcept {
        viewport_.timeStart = view.timeStart;
        viewport_.timeSpan = view.timeSpan;
        viewport_.topPitch = view.pitchHigh;
        if (view.pitchSpan() > 0 && viewport_.area.height > 0)
            viewport_.rowHeight = viewport_.area.height / view.pitchSpan();
    }

    static const char* phaseName(NoteGesturePhase phase) noexcept {
        switch (phase) {
            case NoteGesturePhase::armed: return "armed";
            case NoteGesturePhase::dragging: return "dragging";
            case NoteGesturePhase::lassoing: return "lassoing";
            case NoteGesturePhase::committed: return "committed";
            case NoteGesturePhase::canceled: return "canceled";
            default: return "idle";
        }
    }

    static const char* hitName(NoteHitKind kind) noexcept {
        switch (kind) {
            case NoteHitKind::body: return "body";
            case NoteHitKind::leftEdge: return "left";
            case NoteHitKind::rightEdge: return "right";
            default: return "none";
        }
    }

    struct Document {
        std::vector<ActionNote> notes;
        std::uint64_t revision = 1;
        std::vector<std::vector<ActionNote>> undo;
    } document_;

    Viewport viewport_;
    NoteGestureOptions options_;
    NoteGesture gesture_;
    NavigationDrag middleDrag_;
    bool middleActive_ = false;
    TransientEffects effects_;
    double presentationNow_ = 0;
    PointerFeedback feedback_{};
    Point hoverPoint_{};
    InputModifiers hoverModifiers_{};
    bool hasHover_ = false;
    std::vector<std::size_t> overlapStack_;
    std::size_t overlapCursor_ = noIndex;
    Point overlapPoint_{};
    bool overlapActive_ = false;
    ToolMode tool_ = ToolMode::pointer;
};

} // namespace pianoroll_lab
