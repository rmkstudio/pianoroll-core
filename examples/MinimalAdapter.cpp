// A third tool keeps its own note format and undo storage.
#include <PianoRollCore/PianoRollCore.h>
#include <iostream>
#include <vector>

struct ToolNote { double tick, length; int pitch; bool editable; };
int main() {
    using namespace pianoroll;
    std::vector<ToolNote> notes{{96, 48, 60, true}, {144, 48, 64, true}};
    std::vector<ToolNote> draft = notes;
    std::vector<std::vector<ToolNote>> undo;
    std::uint64_t documentRevision = 1;
    EditSession gesture;
    gesture.begin(documentRevision);

    // Pointer movement is converted to document ticks by this adapter's view.
    Viewport view{{0, 0, 768, 400}, 0, 768, 84, 12};
    const double pointerDelta = view.xToTime(40) - view.xToTime(0);
    const double timeDelta = snappedDelta(notes[1].tick, pointerDelta,
                                         {24, Rounding::nearest, 0, 1});
    GroupBounds bounds;
    for (const auto& n : notes) bounds.include(n.tick, n.tick + n.length, n.pitch, n.editable);
    const auto movement = clampMove(bounds, {timeDelta, 2});
    for (auto& n : draft) if (n.editable) { n.tick += movement.time; n.pitch += movement.pitch; }
    gesture.markChanged(movement.time != 0 || movement.pitch != 0);
    const auto result = gesture.commit(documentRevision, [&] {
        undo.push_back(notes); // adapter owns persistent history, one entry per committed gesture
        notes = draft;
        ++documentRevision;
        return true;
    });
    std::cout << "Committed: " << (result == CommitResult::committed)
              << "; undo entries: " << undo.size() << "; first note tick: " << notes[0].tick << '\n';
    return result == CommitResult::committed && undo.size() == 1 ? 0 : 1;
}
