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
    const auto before = lab.viewport().timeStart;
    const auto pitchBefore = lab.viewport().topPitch;
    lab.wheel({500, 250}, 0, .25, {});
    check(lab.viewport().topPitch != pitchBefore || lab.viewport().timeStart != before,
          "wheel path reaches shared navigation");
    std::cout << "PianoRollLab " << passed << " checks passed\n";
}
