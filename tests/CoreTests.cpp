#include <PianoRollCore/PianoRollCore.h>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>

using namespace pianoroll;
namespace {
int passed = 0;
void check(bool condition, const char* message) {
    if (!condition) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
    ++passed;
}
bool near(double a, double b) { return std::abs(a - b) < 1e-9; }
}
int main() {
    // Same screen gesture in beats and ticks must map to the same screen geometry.
    Viewport beat{{58, 52, 960, 480}, 2, 16, 80, 12};
    Viewport tick{{58, 52, 960, 480}, 192, 1536, 80, 12};
    check(near(beat.timeToX(6), tick.timeToX(576)), "tick and beat coordinate equivalence");
    check(near(beat.xToTime(298), 6), "pointer/time inverse");
    check(near(tick.xToTime(298), 576), "tick pointer inverse");
    check(near(beat.pitchToY(60), 292), "row top position");
    check(near(beat.pitchToY(60, .5), 298), "centred line note position");
    check(beat.yToPitch(292, Viewport::PitchHit::ceilFromTop) == 60,
          "Recover keeps exact pitch-boundary choice");
    check(beat.yToPitch(292, Viewport::PitchHit::floorFromUpperEdge) == 61,
          "Lines keeps exact pitch-boundary choice");
    check(beat.yToPitch(298, Viewport::PitchHit::ceilFromTop) == 60 &&
          beat.yToPitch(298, Viewport::PitchHit::floorFromUpperEdge) == 60,
          "both policies agree inside a pitch row");
    check(beat.yToPitch(-100000) == 127 && beat.yToPitch(100000) == 0,
          "pitch clamps to MIDI range");
    auto shortNote = beat.noteRect(3, 3.00001, 60, 8, 3, .5 - 8.0 / 24);
    check(near(shortNote.width, 3) && near(shortNote.y, 294), "minimum width and centred geometry");
    Rect box = Rect::fromPoints({20, 40}, {10, 30});
    check(near(box.x, 10) && near(box.height, 10), "reverse lasso normalises");
    check(box.contains({10, 30}) && !box.contains({20, 40}), "half-open pointer boundaries");
    check(!box.intersects({20, 30, 5, 10}), "touching edge is not lasso intersection");
    check(!box.intersects({15, 35, 0, 2}), "zero-width lasso selects nothing");

    // Grid behavior is deliberately policy controlled; no forced common PPQ.
    SnapPolicy nearest{.25, Rounding::nearest, 0, 0};
    SnapPolicy floor{24, Rounding::down, 0, 1};
    check(near(snapTime(.38, nearest), .5), "Lines insertion snaps to nearest grid");
    check(near(snapTime(36.48, floor), 24), "Recover insertion floors to grid");
    check(near(snappedDelta(.13, .16, nearest), .12), "movement snaps grabbed note, not delta");
    check(near(snapTime(4.6, {0, Rounding::nearest, 0, 1}), 5), "free Recover still uses integer ticks");
    check(near(snapTime(.12345, {0, Rounding::nearest, 0, 0}), .12345), "free Lines retains sub-grid beat");
    check(near(snapTime(-3, nearest), 0), "minimum insertion time");
    check(near(snapTime(50, {100.0 / 3, Rounding::nearest, 0, 1}), 67),
          "nondivisible original PPQ quantises only edited field");
    FlTickGrid flGrid;
    flGrid.enabled = true;
    flGrid.ticksPerQuarter = 96;
    flGrid.division = 4;
    check(flGridQuantizeTick(108, flGrid) == 96,
          "FL grid converts beat-domain input to 96 PPQ ticks");
    check(near(flGridQuantizeTime(1.16, flGrid), 1.25) &&
          near(flGridSnappedDelta(1.0, .16, flGrid), .25),
          "FL grid anchors snap to the grabbed note");
    flGrid.segments = {{48, 191, 12, 12}};
    check(flGridQuantizeTick(70, flGrid) == 72 &&
          flGridQuantizeTick(47, flGrid) == 48,
          "FL grid uses inclusive time segments and nonzero segment bases");
    flGrid.mode = 1;
    check(flGridQuantizeTick(60, flGrid) == 48,
          "FL mode bias follows the reconstructed minus-half branch");

    GroupBounds group;
    group.include(0, .001, 0, false); // read-only placeholder at bounds must not block edits
    group.include(2, 3, 60);
    group.include(5, 5.5, 64);
    check(group.count == 2 && near(group.earliest, 2) && near(group.latest, 5.5),
          "read-only note excluded from movement bounds");
    auto move = clampMove(group, {-4, 100});
    check(near(move.time, -2) && move.pitch == 63, "mouse move clamps whole group preserving distances");
    check(near(clampMove(group, {100, 0}, 0, 0, 127, 5.25).time, -.25),
          "maximum document time clamps the whole group");
    check(!allowsMove(group, {-4, 0}) && allowsMove(group, {-2, -60}),
          "keyboard can reject instead of clamp");
    check(near(clampResize(group, -10, .125), -.375), "shortest eligible note limits resize");
    GroupBounds readOnly;
    readOnly.include(0, 1, 0, false);
    check(clampMove(readOnly, {10, 12}).pitch == 0 && near(clampResize(readOnly, 3, 1), 0),
          "empty editable selection is no-op");
    check(!allowsMove(readOnly, {0, 0}), "empty selection cannot commit a move");
    GroupBounds invalid;
    invalid.include(0, std::numeric_limits<double>::quiet_NaN(), 60);
    check(invalid.count == 0, "invalid note cannot contaminate group limits");

    std::vector<int> selection{0, 2, 3, 2, -1, 99};
    auto copies = makeCopyPlan(selection, 4, 3, [](std::size_t i) { return i != 0; });
    check(copies.entries.size() == 2, "copy skips readonly duplicate stale indices");
    check(copies.entries[0].source == 2 && copies.entries[0].target == 4,
          "copy keeps requested source order");
    check(copies.anchor == 5 && copies.entries[1].source == 3,
          "grabbed second note maps to its own copy");
    check(makeCopyPlan(selection, 4, 0, [](std::size_t i) { return i != 0; }).anchor == noIndex,
          "uncopied anchor cannot silently become first copy");

    std::vector<HitCandidate> candidates{{{0, 0, 20, 10}, 2, true},
                                        {{0, 0, 20, 10}, 1, true},
                                        {{0, 0, 20, 10}, 1, true},
                                        {{0, 0, 20, 10}, 0, false}};
    auto at = [&](std::size_t i) { return candidates[i]; };
    check(hitTest(4, {5, 5}, at, HitPolicy::lastPainted) == 2, "Recover overlap selects last painted eligible note");
    check(hitTest(4, {5, 5}, at, HitPolicy::closest) == 1, "Lines overlap picks closest with stable first tie");
    check(hitTest(4, {30, 5}, at) == -1, "empty hit reports no index");
    auto noteHit = noteHitTest(3, {0, 5}, [&](std::size_t i) {
        return HitCandidate{{0, 0, i == 2 ? 4.0 : 20.0, 10}, static_cast<double>(i), true};
    }, 5, HitPolicy::closest);
    check(noteHit.valid() && noteHit.kind == NoteHitKind::leftEdge,
          "edge hit is classified before body movement");
    noteHit = noteHitTest(3, {20, 5}, [&](std::size_t i) {
        return HitCandidate{{0, 0, 20, 10}, static_cast<double>(i), true};
    }, 5, HitPolicy::lastPainted);
    check(noteHit.valid() && noteHit.kind == NoteHitKind::rightEdge && noteHit.index == 2,
          "right boundary uses the last-painted stacked note");
    std::vector<std::size_t> hits;
    forEachIntersecting(4, {2, 2, 3, 3}, [&](std::size_t i) { return candidates[i].rect; },
                        [&](std::size_t i) { return i == 1 || i == 3; },
                        [&](std::size_t i) { hits.push_back(i); });
    check(hits == std::vector<std::size_t>{1, 3}, "lasso delegates selectable scope independently of editable policy");

    EditSession edit;
    int commits = 0, undoEntries = 0;
    auto commit = [&] { ++commits; ++undoEntries; return true; };
    edit.begin(10); edit.markChanged(); edit.markChanged();
    check(edit.commit(10, commit) == CommitResult::committed, "changed gesture commits");
    check(edit.commit(10, commit) == CommitResult::inactive && commits == 1 && undoEntries == 1,
          "duplicate mouse-up cannot duplicate commit or undo");
    edit.begin(11);
    check(edit.commit(11, commit) == CommitResult::unchanged && commits == 1,
          "click without change creates no undo entry");
    edit.begin(11); edit.markChanged();
    check(edit.commit(12, commit) == CommitResult::stale && commits == 1,
          "document replacement discards old gesture");
    edit.begin(12); edit.markChanged(); edit.cancel();
    check(edit.commit(12, commit) == CommitResult::inactive && commits == 1,
          "escape cancellation cannot commit");
    edit.begin(12); edit.markChanged();
    check(edit.commit(12, [] { return false; }) == CommitResult::rejected && !edit.active(),
          "rejected commit consumes gesture");
    edit.begin(13); edit.markChanged();
    check(edit.commit(13, [&] {
        return edit.commit(13, commit) == CommitResult::inactive;
    }) == CommitResult::committed && commits == 1, "reentrant commit cannot run twice");
    edit.begin(14); edit.markChanged();
    try { edit.commit(14, []() -> bool { throw std::runtime_error("adapter failure"); }); }
    catch (const std::runtime_error&) {}
    check(!edit.active(), "throwing adapter cannot leave old gesture committable");
    std::cout << "PianoRollCore " << version << ": " << passed << " checks passed\n";
}
