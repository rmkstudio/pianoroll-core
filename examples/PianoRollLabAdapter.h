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
    }

    const std::vector<ActionNote>& notes() const noexcept { return document_.notes; }
    std::uint64_t revision() const noexcept { return document_.revision; }
    std::size_t undoCount() const noexcept { return document_.undo.size(); }
    const Viewport& viewport() const noexcept { return viewport_; }
    const NoteGesture& gesture() const noexcept { return gesture_; }

    void resize(int width, int height) noexcept {
        viewport_.area = {72, 48, std::max(200.0, static_cast<double>(width) - 88.0),
                          std::max(160.0, static_cast<double>(height) - 112.0)};
    }

    void mouseDown(Point point, InputModifiers modifiers = {}) {
        gesture_.mouseDown(point, viewport_, document_.notes, document_.revision,
                           modifiers, options_);
    }

    void mouseMove(Point point, InputModifiers modifiers = {}) {
        gesture_.mouseMove(point, modifiers);
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
        return result;
    }

    void cancel() noexcept { gesture_.cancel(); }

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
            << " phase=" << phaseName(gesture_.phase());
        if (gesture_.hit().valid()) {
            out << " hit=" << gesture_.hit().index
                << ":" << hitName(gesture_.hit().kind);
        }
        return out.str();
    }

private:
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
};

} // namespace pianoroll_lab
