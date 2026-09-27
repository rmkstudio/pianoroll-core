#include "../examples/PianoRollLabAdapter.h"

#include <cstdlib>
#include <iostream>

using namespace pianoroll;
using pianoroll_lab::Adapter;

namespace {
int passed = 0;
void check(bool value, const char* message) {
    if (!value) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
    ++passed;
}
}

int main() {
    Adapter lab;
    check(lab.notes().size() == 6 && lab.revision() == 1, "demo document loads");

    const auto& view = lab.viewport();
    const auto rect = view.noteRect(1, 2, 60, 10, 2, .5 - 10.0 / (2 * view.rowHeight));
    const Point down{rect.x + rect.width * .5, rect.y + rect.height * .5};
    lab.mouseDown(down);
    lab.mouseMove({view.timeToX(1.66), down.y - view.rowHeight});
    check(lab.gesture().phase() == NoteGesturePhase::dragging &&
          lab.gesture().draft()[0].start == 1.25 && lab.gesture().draft()[0].pitch == 61,
          "adapter exposes snapped drag preview");
    check(lab.mouseUp({view.timeToX(1.66), down.y - view.rowHeight}) == CommitResult::committed,
          "adapter commits one gesture");
    check(lab.revision() == 2 && lab.undoCount() == 1, "commit increments revision and undo");
    check(lab.undo() && lab.revision() == 3 && lab.undoCount() == 0 && lab.notes()[0].start == 1,
          "adapter undo restores the document");

    lab.resetDemo();
    lab.mouseDown(down, {false, true, false});
    lab.mouseMove({down.x + 10, down.y}, {false, true, false});
    check(lab.gesture().draft().size() == 8, "shift drag copies the selected group in the adapter");
    lab.cancel();
    check(lab.mouseUp(down) == CommitResult::inactive, "cancelled adapter gesture is inactive");

    lab.resetDemo();
    const auto lassoStart = Point{lab.viewport().timeToX(.5), lab.viewport().pitchToY(68)};
    const auto lassoEnd = Point{lab.viewport().timeToX(2.75), lab.viewport().pitchToY(58)};
    lab.mouseDown(lassoStart);
    lab.mouseMove(lassoEnd);
    check(lab.mouseUp(lassoEnd) == CommitResult::unchanged,
          "lasso selection does not create a musical undo item");
    std::size_t selected = 0;
    for (const auto& note : lab.notes()) selected += note.selected ? 1u : 0u;
    check(selected == 3, "lasso selection is published without changing note timing");

    lab.resetDemo();
    const auto overlap = lab.viewport().noteRect(4, 5, 60, 10, 2,
        .5 - 10.0 / (2 * lab.viewport().rowHeight));
    const Point overlapPoint{overlap.x + overlap.width * .5,
                             overlap.y + overlap.height * .5};
    lab.mouseDown(overlapPoint); lab.mouseUp(overlapPoint);
    check(lab.gesture().hit().index == 4 && lab.notes()[4].selected,
          "first overlap click chooses the last-painted note");
    lab.mouseDown(overlapPoint); lab.mouseUp(overlapPoint);
    check(lab.gesture().hit().index == 3 && lab.notes()[3].selected,
          "second overlap click cycles to the next deletable note");
    lab.tick(1.0);
    check(lab.eraseSelected() && lab.notes().size() == 5,
          "delete acts on the cycled overlap selection");
    check(!lab.effects().fades().empty(), "delete keeps a render-only fade snapshot");
    lab.tick(1.05);
    check(!lab.effects().fades().empty(), "fade remains visible during its short lifetime");
    lab.tick(1.2);
    check(lab.effects().fades().empty(), "fade expires without retaining deleted notes");

    lab.resetDemo();
    const auto first = lab.viewport().noteRect(1, 2, 60, 10, 2,
        .5 - 10.0 / (2 * lab.viewport().rowHeight));
    const Point firstCenter{first.x + first.width * .5, first.y + first.height * .5};
    lab.mouseDown(firstCenter); lab.mouseUp(firstCenter);
    const auto bounds = lab.selectionBounds();
    check(bounds.width > 0 && hitLassoResizeHandle({bounds.right(), bounds.y + bounds.height * .5}, bounds),
          "selected notes expose the stretch affordance");
    lab.mouseDown({bounds.right(), bounds.y + bounds.height * .5});
    lab.mouseMove({bounds.right() + 60, bounds.y + bounds.height * .5});
    check(lab.gesture().mode() == NoteGestureMode::scaleSelection && lab.gesture().changed(),
          "stretch affordance starts a scale gesture");
    check(lab.mouseUp({bounds.right() + 60, bounds.y + bounds.height * .5}) == CommitResult::committed,
          "selection stretch commits as one undo item");

    lab.resetDemo();
    lab.setTool(ToolMode::slice);
    const auto sliceRect = lab.viewport().noteRect(1, 2, 60, 10, 2,
        .5 - 10.0 / (2 * lab.viewport().rowHeight));
    lab.mouseDown({lab.viewport().timeToX(1.5), sliceRect.y + sliceRect.height * .5});
    check(lab.notes().size() == 7 && lab.notes()[0].end == 1.5,
          "slice tool splits the note at the pointer time");
    lab.setTool(ToolMode::eraser);
    const auto eraseRect = lab.viewport().noteRect(1.5, 2, 60, 10, 2,
        .5 - 10.0 / (2 * lab.viewport().rowHeight));
    lab.mouseDown({eraseRect.x + eraseRect.width * .5, eraseRect.y + eraseRect.height * .5});
    check(lab.notes().size() == 6, "eraser tool removes one hit note and leaves a fade snapshot");
    lab.resetDemo();
    lab.setTool(ToolMode::glue);
    const auto glueRect = lab.viewport().noteRect(4, 5, 60, 10, 2,
        .5 - 10.0 / (2 * lab.viewport().rowHeight));
    lab.mouseDown({glueRect.x + glueRect.width * .25, glueRect.y + glueRect.height * .5});
    check(lab.notes().size() == 5 && lab.notes()[3].end == 5,
          "glue tool merges the touching same-pitch run under the pointer");
    lab.setTool(ToolMode::pointer);

    lab.resetDemo();
    const auto before = lab.viewport().timeStart;
    const auto pitchBefore = lab.viewport().topPitch;
    lab.wheel({500, 250}, 0, .25, {});
    check(lab.viewport().topPitch != pitchBefore || lab.viewport().timeStart != before,
          "wheel path reaches shared navigation");
    std::cout << "PianoRollLab " << passed << " checks passed\n";
}
