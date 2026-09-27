#include "PianoRollCore/NoteActions.h"
#include <iostream>
#include <stdexcept>
#include <limits>

using namespace pianoroll;
namespace {
int checks = 0;
void check(bool condition, const char* message) {
    ++checks;
    if (!condition) throw std::runtime_error(message);
}
bool near(double a, double b) { return std::abs(a - b) < 1.0e-9; }
ActionNote note(double start, double end, int pitch, bool selected = true, int source = 0) {
    ActionNote n; n.start = start; n.end = end; n.pitch = pitch; n.selected = selected; n.source = source;
    return n;
}
std::vector<ActionNote> score(std::initializer_list<ActionNote> input) {
    std::vector<ActionNote> result(input);
    for (std::size_t i = 0; i < result.size(); ++i) result[i].originalIndex = i;
    return result;
}
void quantizeChecks() {
    auto input = score({note(1.12, 1.51, 60), note(2.12, 2.51, 64, false), note(3.12, 3.51, 20), note(4.12, 4.51, 90)});
    input[2].keySwitch = true; input[3].editable = false;
    QuantizeOptions q;
    auto result = quantizeNotes(input, q);
    check(result && result.changed, "Q changes a late selected note");
    check(near(result.notes[0].start, 1) && near(result.notes[0].end, 1.39), "Q moves the onset and preserves performed duration");
    for (int i = 1; i < 4; ++i)
        check(result.notes[i].start == input[i].start && result.notes[i].end == input[i].end,
              "unselected notes, key switches and display placeholders retain exact timing");
    q.startStrength = .5;
    result = quantizeNotes(input, q);
    check(near(result.notes[0].start, 1.06) && near(result.notes[0].end, 1.45), "half quantize retains half the timing deviation");
    q.startStrength = 1; q.captureRange = .01;
    check(!quantizeNotes(input, q).changed, "capture range protects intentional off-grid notes");
    q.captureRange = std::numeric_limits<double>::infinity(); q.range = {true, 0, 1};
    check(!quantizeNotes(input, q).changed, "quantize time range excludes notes outside it");
    q.range = {}; q.starts = false; q.ends = true; q.endStrength = .5;
    result = quantizeNotes(input, q);
    check(result.notes[0].start == input[0].start && near(result.notes[0].end, 1.505), "tail-only strength never moves onset");
    q.starts = true; q.endStrength = 1;
    auto shortNote = score({note(.24, .26, 72)});
    result = quantizeNotes(shortNote, q);
    check(!result && !result.changed && result.notes[0].end == .26, "collapsing both boundaries rejects atomically");
    auto performed = score({note(1.06, 1.45, 72)}); performed[0].velocity = 45;
    auto restored = restoreNoteTiming(performed, {{0, 1.12, 1.51}});
    check(restored && restored.notes[0].start == 1.12 && restored.notes[0].end == 1.51,
          "restore timing recovers the recorded performance");
    check(restored.notes[0].pitch == 72 && restored.notes[0].velocity == 45, "restore timing preserves later pitch and dynamics edits");
    check(!restoreNoteTiming(performed, {}), "missing original timing does not silently restore a different note");
    check(!restoreNoteTiming(performed, {{0, 1, 2}, {0, 2, 3}}), "duplicate timing identities are rejected");
    ActionGrid grid; grid.step = .5; grid.swing = .5;
    check(near(nearestActionGrid(.62, grid), .75), "swing moves the offbeat within the pair");
    grid.swing = 0; grid.division = GridDivision::triplet;
    check(near(nearestActionGrid(.32, grid), 1.0 / 3), "eighth triplets divide a beat into thirds");
    grid.step = .25; grid.division = GridDivision::quintuplet;
    check(near(nearestActionGrid(.39, grid), .4), "sixteenth quintuplets divide a beat into fifths");
    grid.division = GridDivision::straight; grid.origin = 4;
    check(near(nearestActionGrid(4.12, grid), 4), "grid origin aligns local phrases");
    grid.step = 0;
    check(std::isnan(nearestActionGrid(1, grid)), "invalid grid cannot manufacture a valid time");
}
void mirrorAndTimeChecks() {
    auto melody = score({note(4, 5, 60), note(5, 5.5, 64), note(5.5, 6, 67)});
    auto inverted = mirrorNotes(melody);
    check(inverted && inverted.notes[0].pitch == 67 && inverted.notes[1].pitch == 63 && inverted.notes[2].pitch == 60,
          "pitch inversion reverses chromatic melodic intervals");
    MirrorOptions m; m.pitchPivot = MirrorPivot::first;
    auto result = mirrorNotes(melody, m);
    check(result.notes[0].pitch == 60 && result.notes[2].pitch == 53, "first-note mirror pivot is fixed");
    m.pitchPivot = MirrorPivot::last;
    result = mirrorNotes(melody, m);
    check(result.notes[0].pitch == 74 && result.notes[2].pitch == 67, "last-note mirror pivot is fixed");
    m.direction = MirrorDirection::time;
    result = mirrorNotes(melody, m);
    check(result && result.notes[0].start == 5 && result.notes[0].end == 6 &&
          result.notes[2].start == 4 && result.notes[2].end == 4.5,
          "retrograde reverses complete note intervals including unequal durations");
    m.direction = MirrorDirection::both; m.pitchPivot = MirrorPivot::middle;
    result = mirrorNotes(melody, m);
    check(result.notes[0].start == 5 && result.notes[0].pitch == 67, "both axes combine without changing note identity");
    m.direction = MirrorDirection::pitch; m.pitchPivot = MirrorPivot::custom; m.customPitch = 60.25;
    check(!mirrorNotes(melody, m), "fractional resulting MIDI pitch is rejected without rounding melody");
    m.customPitch = 60.5;
    check(mirrorNotes(melody, m).notes[0].pitch == 61, "half-semitone pivot can produce valid integer pitches");
    result = setNoteLength(melody, .25);
    check(result && near(result.notes[0].end, 4.25) && near(result.notes[2].end, 5.75), "fixed lengths retain each onset");
    result = alignNoteEnds(melody, 0);
    check(result && result.notes[0].end == 6 && result.notes[1].end == 6, "align endings finds latest selected tail");
    check(!alignNoteEnds(melody, 4.5, false), "an end before another selected onset rejects all notes");
    auto legato = score({note(0, .5, 60), note(.5, 1, 62), note(.5, 1, 65, true, 1)});
    result = moveLinkedBoundary(legato, 0, true, .7, .001);
    check(result && near(result.notes[0].end, .7) && near(result.notes[1].start, .7), "linked boundary keeps adjacent notes gapless");
    check(result.notes[2].start == .5, "linked boundary does not edit another source");
    check(!moveLinkedBoundary(legato, 0, true, 1.2, .001), "linked resize cannot invert its neighbor");
    result = scaleNoteTime(melody, .5, 4);
    check(result && result.notes[0].start == 4 && result.notes[0].end == 4.5 && result.notes[2].end == 5,
          "phrase compression scales rhythm and durations around explicit anchor");
    auto tooHigh = score({note(0, 1, 60), note(1, 2, 127)});
    result = transposeNotes(tooHigh, 1);
    check(!result && result.notes[0].pitch == 60, "a pitch overflow rolls back the complete phrase");
    tooHigh[1].keySwitch = true;
    result = transposeNotes(tooHigh, 12);
    check(result && result.notes[0].pitch == 72 && result.notes[1].pitch == 127, "transpose protects explicit key switches");
    result = changeNoteVelocity(melody, -200);
    check(result && result.notes[0].velocity == 1, "velocity operations cannot create note-on-zero deletion semantics");
}
void selectionChecks() {
    // The long high note starts earlier than the low note. Same-onset grouping
    // would misclassify the later low note as a new top voice.
    auto polyphony = score({note(0, 4, 72), note(1, 2, 60), note(1.5, 3, 67), note(0, 4, 12)});
    polyphony[3].keySwitch = true;
    NoteSelectionOptions s;
    auto result = selectNotes(polyphony, s);
    check(result && result.indices == std::vector<std::size_t>{0}, "highest uses sounding intervals, not only coincident onsets");
    s.kind = NoteSelectionKind::lowest;
    result = selectNotes(polyphony, s);
    check(result.indices == std::vector<std::size_t>({0, 1, 2}), "lowest includes each note when it is the audible lower boundary");
    s.kind = NoteSelectionKind::highest; polyphony.push_back(note(0, 4, 72));
    result = selectNotes(polyphony, s);
    check(result.indices == std::vector<std::size_t>({0, 4}), "unison top notes retain all matching sources");
    auto melody = score({note(0, .1, 60), note(.5, .75, 62), note(1, 2, 64), note(1.5, 2.5, 65)});
    melody[0].velocity = 30; melody[1].velocity = 50; melody[2].velocity = 100; melody[3].velocity = 110;
    s.kind = NoteSelectionKind::velocityRange; s.minimum = 0; s.maximum = 60;
    check(selectNotes(melody, s).indices == std::vector<std::size_t>({0, 1}), "quiet-note selection isolates dynamics for repair");
    s.kind = NoteSelectionKind::lengthRange; s.maximumLength = .11;
    check(selectNotes(melody, s).indices == std::vector<std::size_t>{0}, "short-note selection finds likely stray notes");
    s.kind = NoteSelectionKind::pitchRange; s.minimum = 62; s.maximum = 64;
    check(selectNotes(melody, s).indices == std::vector<std::size_t>({1, 2}), "pitch range includes both musical endpoints");
    s.kind = NoteSelectionKind::everyNth; s.every = 2; s.offset = 1;
    check(selectNotes(melody, s).indices == std::vector<std::size_t>({1, 3}), "alternating-note selection supports repeated rhythmic accents");
    s.kind = NoteSelectionKind::beatPosition; s.period = 1; s.position = .5; s.tolerance = .01;
    check(selectNotes(melody, s).indices == std::vector<std::size_t>({1, 3}), "offbeat selection spans multiple beats");
    s.range = {true, 1, 2};
    check(selectNotes(melody, s).indices == std::vector<std::size_t>{3}, "selection range limits processing to a phrase");
    s.range = {}; s.withinSelection = true; melody[1].selected = false;
    check(selectNotes(melody, s).indices == std::vector<std::size_t>{3}, "selection refinement never expands beyond the existing selection");
    s.every = 0;
    check(!selectNotes(melody, s), "invalid every-N selection is rejected");
}
void splitRepeatStrumChecks() {
    auto sustained = score({note(.1, 1.1, 60), note(0, 1, 12)}); sustained[1].keySwitch = true;
    ActionGrid grid; grid.step = .25;
    auto result = splitNotes(sustained, grid);
    check(result && result.notes.size() == 6, "off-grid sustained note splits at four interior grid lines");
    check(result.notes[0].start == .1 && result.notes[0].end == .25 && !result.notes[0].generated,
          "first split segment preserves the original note identity and pickup");
    check(result.notes.back().start == 1 && result.notes.back().end == 1.1,
          "last split segment preserves the performed release");
    for (std::size_t i = 2; i < result.notes.size(); ++i)
        check(result.notes[i].generated && result.notes[i].originalIndex == 0, "split copies retain source lineage for non-note MIDI attributes");
    check(result.notes[1].start == 0 && result.notes[1].end == 1, "split protects key switch duration");
    auto pointerSplit = score({note(0, 1, 60)});
    result = splitNoteAt(pointerSplit, 0, .4);
    check(result && result.notes.size() == 2 && near(result.notes[0].end, .4) &&
          near(result.notes[1].start, .4) && result.notes[1].generated,
          "slice tool splits one selected note at the pointer time");
    auto glueInput = score({note(0, .5, 60), note(.5, 1, 60), note(1.1, 1.5, 60),
                            note(.5, 1, 64, true)});
    glueInput[2].selected = false;
    result = glueNotes(glueInput);
    check(result && result.notes.size() == 3 && near(result.notes[0].start, 0) &&
          near(result.notes[0].end, 1),
          "glue merges selected touching notes but leaves an unselected note alone");
    GlueOptions allGlue; allGlue.selectedOnly = false; allGlue.tolerance = .11;
    result = glueNotes(glueInput, allGlue);
    check(result && result.notes.size() == 2 && near(result.notes[0].end, 1.5),
          "glue tolerance can join a short intentional gap");
    auto phrase = score({note(0, .5, 60), note(.5, 1, 62)});
    RepeatOptions repeat; repeat.start = 0; repeat.end = 2.25;
    result = repeatNotes(phrase, repeat);
    check(result && result.notes.size() == 5, "repeat fills explicit range without creating notes beyond its end");
    bool clipped = false;
    for (const auto& n : result.notes) {
        check(n.end <= 2.25, "repeat end is bounded");
        clipped = clipped || (n.start == 2 && n.end == 2.25);
    }
    check(clipped, "repeat clips final partial note when enabled");
    repeat.clipAtEnd = false;
    check(repeatNotes(phrase, repeat).notes.size() == 4, "whole-note repeat omits a partial final note");
    auto chord = score({note(4, 5, 67), note(4, 5, 60), note(4, 5, 64), note(5, 6, 65)});
    StrumOptions strum; strum.spacing = .05;
    result = strumNotes(chord, strum);
    check(result && near(result.notes[1].start, 4) && near(result.notes[2].start, 4.05) && near(result.notes[0].start, 4.1),
          "upward strum orders a chord by pitch rather than storage order");
    check(near(result.notes[0].end, 5.1) && result.notes[3].start == 5, "strum preserves duration and leaves later single notes in place");
    strum.descending = true; strum.preserveEnds = true;
    result = strumNotes(chord, strum);
    check(result.notes[0].start == 4 && near(result.notes[1].start, 4.1) && result.notes[1].end == 5,
          "downward strum can keep the original release boundary");
    strum.spacing = 2;
    check(!strumNotes(chord, strum), "strum cannot push a note onset past its fixed end");
}
void guardChecks() {
    auto input = score({note(0, .5, 60), note(.6, 1, 60)});
    ActionRules rules; rules.rejectNewOverlaps = true;
    check(!setNoteLength(input, 1, rules), "optional overlap guard rejects new same-source pitch overlap");
    input[1].source = 1;
    check(static_cast<bool>(setNoteLength(input, 1, rules)), "different source overlap remains valid polyphony");
    input[1].source = 0; input[1].start = .25;
    check(static_cast<bool>(setNoteLength(input, 1, rules)), "existing same-pitch overlap does not prevent unrelated editing");
    input[0].start = std::numeric_limits<double>::quiet_NaN();
    check(!transposeNotes(input, 1), "nonfinite source timing cannot propagate into output");
    input = score({note(0, 100, 60)});
    rules = {}; rules.maximumNotes = 10;
    ActionGrid grid; grid.step = .25;
    auto result = splitNotes(input, grid, rules);
    check(!result && result.notes.size() == 1 && result.notes[0].end == 100, "split limit rolls back the whole operation");
    grid.step = 1.0e-30;
    check(!splitNotes(input, grid), "pathologically fine split does not run an unbounded loop");
    RepeatOptions repeat; repeat.end = 1000; repeat.period = .001;
    check(!repeatNotes(input, repeat, rules), "repeat expansion is bounded before publishing any note");
    repeat.end = 1; repeat.clipAtEnd = false; repeat.period = 1.0e-9;
    check(!repeatNotes(input, repeat).changed, "long note outside short whole-note range exits without billions of skipped copies");
    check(!scaleNoteTime(input, std::numeric_limits<double>::infinity(), 0), "nonfinite stretch is rejected");
    check(!transposeNotes(input, std::numeric_limits<int>::max()), "extreme transpose avoids integer overflow");
}
} // namespace
int main() {
    try {
        quantizeChecks(); mirrorAndTimeChecks(); selectionChecks(); splitRepeatStrumChecks(); guardChecks();
        std::cout << "Note actions: " << checks << " semantic checks passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Note actions failed after " << checks << " checks: " << e.what() << '\n';
        return 1;
    }
}
