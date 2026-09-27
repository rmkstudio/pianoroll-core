#include <PianoRollCore/Navigation.h>
#include <PianoRollCore/Interaction.h>
#include <PianoRollCore/Controllers.h>
#include <PianoRollCore/Meter.h>
#include <cstdlib>
#include <iostream>
#include <limits>

using namespace pianoroll;
namespace {
int passed = 0;
void check(bool value, const char* name) {
    if (!value) { std::cerr << "FAIL: " << name << '\n'; std::exit(1); }
    ++passed;
}
bool near(double a, double b) { return std::abs(a - b) < 1e-8; }
}
int main() {
    ViewState view{16, 16, 36, 84};
    Rect area{80, 40, 800, 490};
    NavigationLimits limits{.25, 256, 256, 12};
    Point anchor{480, 187};
    const double pitchAnchor = view.pitchHigh + .5 - .3 * view.pitchSpan();
    auto middle = beginMiddleDrag(PointerRegion::keys, anchor, view, area);
    auto zoom = dragNavigation(middle, {480, 87}, limits);
    check(zoom.kind == NavigationKind::pitchZoom && zoom.view.pitchSpan() < view.pitchSpan(), "keyboard middle drag zooms in");
    check(near(zoom.view.pitchHigh + .5 - .3 * zoom.view.pitchSpan(), pitchAnchor), "middle pitch zoom preserves pointer pitch");
    auto same = dragNavigation(middle, anchor, limits);
    check(near(same.view.pitchLow, view.pitchLow), "zoom gesture is derived from original snapshot");
    auto zoom2 = dragNavigation(middle, {480, 87}, limits);
    check(near(zoom2.view.pitchHigh, zoom.view.pitchHigh), "repeated drag callback cannot compound zoom");
    auto pan = dragNavigation(beginMiddleDrag(PointerRegion::notes, anchor, view, area), {580, 237}, limits);
    check(pan.kind == NavigationKind::pan && near(pan.view.timeStart, 14), "note-area middle drag pans time");
    check(near(pan.view.pitchLow, 41), "middle drag pans fractional pitch range");
    auto lanePan = beginMiddleDrag(PointerRegion::controllerLane, anchor, view, area);
    check(lanePan.kind == NavigationKind::pan, "controller lane middle drag shares navigation");
    check(beginMiddleDrag(PointerRegion::ruler, anchor, view, area).kind == NavigationKind::none, "ruler does not accidentally start pan");
    auto bounded = dragNavigation(middle, {480, -1e6}, limits);
    check(near(bounded.view.pitchSpan(), 12) && bounded.view.pitchLow >= 0 && bounded.view.pitchHigh <= 127, "zoom limits keep playable pitch range");
    bounded = dragNavigation(middle, {480, 1e6}, limits);
    check(near(bounded.view.pitchSpan(), 128) && near(bounded.view.pitchLow, 0), "zoom out covers exactly MIDI pitches");
    auto wheel = wheelNavigation(view, area, anchor, 0, 1, {true,true,false}, limits);
    check(near(wheel.view.pitchHigh + .5 - .3 * wheel.view.pitchSpan(), pitchAnchor), "Ctrl Shift wheel anchors pitch");
    auto horizontal = wheelNavigation(view, area, anchor, 0, .2, {true,false,false}, limits);
    check(horizontal.kind == NavigationKind::timeZoom && near(horizontal.view.timeStart + .5 * horizontal.view.timeSpan, 24), "Ctrl wheel anchors musical time");
    auto shifted = wheelNavigation(view, area, anchor, 0, .25, {false,true,false}, limits);
    check(near(shifted.view.timeStart, 13.2), "Shift wheel pans horizontal at shared sensitivity");
    auto trackpad = wheelNavigation(view, area, anchor, .25, 0, {}, limits);
    check(near(trackpad.view.timeStart, shifted.view.timeStart), "horizontal trackpad follows horizontal wheel rule");
    auto pitchScroll = wheelNavigation(view, area, anchor, 0, .1, {}, limits);
    check(near(pitchScroll.view.pitchLow, 38) && near(pitchScroll.view.timeStart, 16), "plain wheel scrolls two semitones");
    auto noWheel = wheelNavigation(view, area, anchor, 0, 0, {}, limits);
    check(noWheel.kind == NavigationKind::none && near(noWheel.view.pitchLow, 36), "zero wheel callback does not move downward");
    auto invalid = wheelNavigation(view, {0,0,0,0}, anchor, 0, 1, {}, limits);
    check(invalid.kind == NavigationKind::none, "empty layout prevents division by zero");
    ViewState ticks{view.timeStart * 960, view.timeSpan * 960, view.pitchLow, view.pitchHigh};
    NavigationLimits tickLimits{240, 245760, 245760, 12};
    auto tickZoom = wheelNavigation(ticks, area, anchor, 0, .2, {true,false,false}, tickLimits);
    check(near(tickZoom.view.timeStart / 960, horizontal.view.timeStart) && near(tickZoom.view.timeSpan / 960, horizontal.view.timeSpan), "beat and original tick adapters navigate identically");
    auto nanView = clampNavigation({-10, std::numeric_limits<double>::quiet_NaN(), -500, 500}, limits);
    check(std::isfinite(nanView.timeSpan) && nanView.timeStart >= 0 && nanView.pitchLow >= 0 && nanView.pitchHigh <= 127, "invalid saved view cannot poison navigation");
    PlayheadFollow follow;
    auto followed = follow.update(view, 60, true, false, limits);
    check(near(followed.timeStart, 58.4), "playhead follows after leaving visible range");
    follow.userNavigated();
    check(near(follow.update(view, 60, true, false, limits).timeStart, 16), "manual navigation suspends follow");
    follow.transportLocated();
    check(near(follow.update(view, 60, true, false, limits).timeStart, 58.4), "explicit seek resumes follow");
    check(near(follow.update(view, 60, true, true, limits).timeStart, 16), "follow never fights active edit gesture");
    check(near(follow.update(view, 60, false, false, limits).timeStart, 16), "paused playhead cannot shift viewport");
    follow.setEnabled(false);
    check(near(follow.update(view, 60, true, false, limits).timeStart, 16), "disabled follow retains view");
    check(pitchName(60,5) == "C5" && pitchName(61,5) == "C#5" && pitchName(61,4,true) == "Db4", "pitch labels preserve app octave convention");
    check(pitchName(-1).empty() && showPitchLabel(11,25) && !showPitchLabel(10,25), "labels become readable at same zoom threshold");

    ShortcutInput input{ShortcutKey::space,0,{},false};
    check(editorCommand(input) == EditorCommand::playPause, "Space toggles play pause");
    input.key = ShortcutKey::enter;
    check(editorCommand(input) == EditorCommand::playPause, "Enter toggles play pause");
    input.key = ShortcutKey::home;
    check(editorCommand(input) == EditorCommand::home, "Home locates beginning");
    input = {ShortcutKey::character,'1',{},false};
    check(editorCommand(input) == EditorCommand::pencil, "tool 1 always pencil");
    input.character = '2';
    check(editorCommand(input) == EditorCommand::pointer, "tool 2 always pointer");
    input.character = '3';
    check(editorCommand(input) == EditorCommand::eraser &&
          toolModeForCommand(EditorCommand::eraser) == ToolMode::eraser,
          "tool 3 selects the reusable eraser mode");
    input.character = '4';
    check(editorCommand(input) == EditorCommand::slice &&
          toolModeForCommand(EditorCommand::slice) == ToolMode::slice,
          "tool 4 selects the reusable slice mode");
    input.character = '5';
    check(editorCommand(input) == EditorCommand::glue &&
          toolModeForCommand(EditorCommand::glue) == ToolMode::glue,
          "tool 5 selects the reusable glue mode");
    input = {ShortcutKey::character,'z',{true,true,false}};
    check(editorCommand(input) == EditorCommand::redo, "Ctrl Shift Z is redo including lowercase input");
    input.textInputFocused = true;
    check(editorCommand(input) == EditorCommand::none, "text input retains its native edit commands");
    input = {ShortcutKey::space,0,{true,false,false}};
    check(editorCommand(input) == EditorCommand::none, "modified Space is not a transport shortcut");
    input = {ShortcutKey::deleteKey,0,{},false};
    check(editorCommand(input) == EditorCommand::erase, "Delete belongs to focused editor");
    RulerGesture ruler;
    ruler.begin({280,40}, view, area);
    auto click = ruler.finish({281,41}, {.25,Rounding::nearest,0,0});
    check(click.action == RulerAction::seek && near(click.start,20.02), "ruler click seeks exact time without snapping");
    check(ruler.finish({600,40}, {}).action == RulerAction::none, "ruler release consumed once");
    ruler.begin({580,40}, view, area);
    auto loop = ruler.finish({280,40}, {.25,Rounding::nearest,0,0});
    check(loop.action == RulerAction::setLoop && near(loop.start,20) && near(loop.end,26), "reverse ruler drag normalizes loop and snaps endpoints");
    ruler.begin({280,40}, view, area); ruler.cancel();
    check(ruler.finish({580,40}, {}).action == RulerAction::none, "cancelled ruler gesture cannot affect new document");
    check(RulerGesture::clearLoop().action == RulerAction::clearLoop, "right-click loop clearing has shared intent");
    check(isPointerClick({1,1},{3,3}) && !isPointerClick({1,1},{5,5}), "blank pointer click and lasso share threshold");
    GroupScope scope;
    scope.choose(3);
    check(!scope.all && scope.contains(3) && !scope.contains(2) && scope.writeGroup == 3, "single group updates edit insertion scope");
    scope.toggle(5); scope.toggle(3);
    check(scope.groups == std::set<int>{5} && scope.writeGroup == 5, "multi-group removal keeps valid insertion destination");
    scope.chooseAll();
    check(scope.all && scope.groups.empty() && scope.writeGroup == 5, "All does not erase explicit insertion target");
    SoloState solo;
    solo.toggle(3);
    check(solo.audible(3) && !solo.audible(5) && scope.all, "solo changes audio independently of edit selection");
    solo.toggle(3);
    check(solo.audible(5), "clicking active solo restores all groups");
    auto locked = constrainNoteDrag({3,4}, false, {true,false,false});
    check(near(locked.time,0) && locked.pitch==4, "Ctrl during drag locks time");
    locked = constrainNoteDrag({3,4}, false, {false,true,false});
    check(near(locked.time,3) && locked.pitch==0, "Shift added during drag locks pitch");
    locked = constrainNoteDrag({3,4}, true, {false,true,false});
    check(locked.pitch==4, "Shift copy may move in both axes");
    check(velocityWheelDelta(.001)==1 && velocityWheelDelta(-10)==-1 && velocityWheelDelta(0)==0, "Alt wheel velocity uses one MIDI step");
    check(relativeLaneValue(60,80,100)==80 && relativeLaneValue(120,80,100)==127, "relative velocity preserves offsets with MIDI clamp");
    check(near(smoothLaneValue(0, 100, .5), 50) && smoothedLaneValue(0, 100, .5) == 50,
          "velocity preview keeps fractional pointer response");
    check(velocityLightness(1) > velocityLightness(127),
          "weak velocity receives the lighter note tint");
    check(hitLassoResizeHandle({110, 40}, {10, 20, 100, 40}) &&
          !hitLassoResizeHandle({20, 30}, {10, 20, 100, 40}),
          "selection stretch handle has a narrow hit target");
    Rect lane{0,0,100,127};
    auto stroke = sampleLaneStroke(50,lane,{0,127},{100,0},false);
    check(stroke.hit && stroke.value==64, "fast lane stroke interpolates crossed note columns");
    check(!sampleLaneStroke(70,lane,{50,50},{50,40},false).hit, "stationary lane edit affects only nearby columns");
    check(sampleLaneStroke(50,lane,{50,50},{50,127},false).value==1, "velocity never becomes Note On zero");

    ControllerDirectory directory;
    check(directory.controllers().empty(), "empty file has no phantom controller defaults");
    directory.observeEvent(0,11); directory.observeEvent(0,1); directory.observeEvent(17,1);
    directory.observeEvent(0,1); directory.observeEvent(-1,7); directory.observeEvent(0,128);
    directory.observeStroke(17,7,0);
    check(directory.controllers()==std::vector<int>({1,11}), "actual controllers sorted unique; empty strokes ignored");
    directory.observeStroke(17,7,2);
    check(directory.controllers(17)==std::vector<int>({1,7}), "nonempty CC stroke appears in selected source directory");
    check(directory.sources(1)==std::vector<int>({0,17}), "controller source list keeps channel ownership");
    auto entries=directory.entries();
    check(entries[1].number==7 && entries[1].hasEdits && !entries[1].hasEvents, "directory distinguishes user edits from raw MIDI events");
    auto target=directory.writeTarget(64,-1,{0,17});
    check(target.status==ControllerTargetStatus::chooseSource && target.source==-1, "new CC in All requires a destination when ambiguous");
    target=directory.writeTarget(64,17,{0,17});
    check(target.status==ControllerTargetStatus::ready && target.source==17 && target.controller==64, "new controller resolves explicit editable source without existing event");
    check(!directory.contains(17,64), "choosing new CC does not fabricate data");
    target=directory.writeTarget(64,99,{17,17});
    check(target.status==ControllerTargetStatus::ready && target.source==17, "single valid source resolves after stale preference");
    check(directory.writeTarget(128,0,{0}).status==ControllerTargetStatus::invalid && directory.writeTarget(1,0,{}).status==ControllerTargetStatus::invalid, "invalid CC or no writable source cannot create events");
    check(controllerName(1)=="CC1 - Modulation" && controllerName(0)=="CC0", "shared controller names cover actual CC zero");
    directory.clear();
    check(directory.controllers().empty(), "document replacement clears prior CC presence");
    MeterMap meter;
    auto position=meter.positionAt(8);
    check(position.bar==3 && position.numerator==4 && position.implicitMeter, "missing meter explicitly falls back to 4/4");
    meter.assign({{0,4,4},{8,3,8},{11,2,4}},1);
    position=meter.positionAt(8);
    check(position.bar==3 && near(position.beat,1) && position.numerator==3 && position.denominator==8, "4/4 to 3/8 changes beat unit at exact boundary");
    position=meter.positionAt(10);
    check(position.bar==4 && near(position.beat,2), "eighth-note beats locate correctly in 3/8");
    position=meter.positionAt(11);
    check(position.bar==5 && position.numerator==2 && near(position.beat,1), "mixed-meter bar numbers continue through second change");
    auto marks=meter.grid(7,12,.4);
    bool hasEight=false,hasEleven=false;
    for(auto mark:marks) {
        if(near(mark.time,8)) hasEight=mark.barStart && mark.signatureChange && mark.bar==3;
        if(near(mark.time,11)) hasEleven=mark.signatureChange && mark.bar==5;
    }
    check(hasEight && hasEleven, "visible grid contains actual signature boundaries");
    MeterMap tickMeter;
    tickMeter.assign({{0,4,4},{7680,3,8},{10560,2,4}},960);
    check(tickMeter.positionAt(9600).bar==meter.positionAt(10).bar && near(tickMeter.positionAt(9600).beat,meter.positionAt(10).beat), "meter locator matches in beats and original ticks");
    meter.assign({{0,4,4},{6,3,8}},1);
    check(meter.positionAt(6).bar==3, "mid-bar signature starts next numbered bar after partial bar");
    meter.assign({{0,4,4},{6,4,4}},1);
    check(meter.positionAt(6).bar==2 && near(meter.positionAt(6).beat,3), "repeated unchanged signature does not invent bar boundary");
    meter.assign({{0,3,4},{0,5,8},{9,4,3}},1);
    check(meter.positionAt(0).numerator==5 && meter.positionAt(20).denominator==8, "same-time last signature wins and invalid denominator is ignored");
    check(meter.grid(0,1e12,0,32).size()<=32, "very long files have bounded grid work");
    meter.assign({{0,4,4},{6,3,8}},1);
    auto fine=meter.grid(0,8,.1,2048,.25);
    bool hasSub=false,hasBar=false;
    for(auto mark:fine) {
        if(near(mark.time,.25)) hasSub=!mark.beatStart && !mark.barStart;
        if(near(mark.time,6)) hasBar=mark.barStart && mark.signatureChange;
    }
    check(hasSub && hasBar, "snap subdivisions retain exact changed-meter boundaries");
    auto triplet=meter.grid(0,8,.1,2048,1.0/3);
    bool hasHalf=false;
    for(auto mark:triplet) if(near(mark.time,6.5)) hasHalf=mark.beatStart;
    check(hasHalf, "triplet snap does not hide eighth-note beat line");
    check(meter.grid(0,10000,0,32,.25).size()<=32, "snap subdivision work obeys grid budget");

    // Editable note gesture: FL-style dead-zone, anchored snap, resize/copy,
    // lasso selection, group clamp and one-shot revision-gated commit.
    Viewport noteView{{80, 40, 800, 490}, 0, 16, 84, 12};
    NoteGestureOptions gestureOptions;
    gestureOptions.snap = {.25, Rounding::nearest, 0, 0};
    gestureOptions.minimumLength = .125;
    std::vector<ActionNote> score{{2, 3, 60, 100, 0, 0, true, true, false, false},
                                  {5, 6, 64, 90, 0, 1, true, true, false, false},
                                  {8, 9, 67, 80, 0, 2, false, true, false, false}};
    const auto firstRect = noteView.noteRect(score[0].start, score[0].end, score[0].pitch,
                                             gestureOptions.noteHeight, gestureOptions.minimumWidth,
                                             .5 - gestureOptions.noteHeight / (2 * noteView.rowHeight));
    const Point firstPoint{firstRect.x + firstRect.width * .5, firstRect.y + firstRect.height * .5};
    NoteGesture gesture;
    gesture.mouseDown(firstPoint, noteView, score, 21, {}, gestureOptions);
    gesture.mouseMove({firstPoint.x + 2, firstPoint.y}, {});
    check(gesture.phase() == NoteGesturePhase::armed && !gesture.changed(),
          "note drag keeps the dead-zone as an armed click");
    gesture.mouseMove({noteView.timeToX(2.66), firstPoint.y - noteView.rowHeight}, {});
    check(gesture.phase() == NoteGesturePhase::dragging && gesture.draft()[0].start == 2.25 &&
          gesture.draft()[0].pitch == 61 && gesture.draft()[1].start == 5.25,
          "move snaps from the grabbed note and carries the selected group");
    int gestureCommits = 0;
    std::vector<ActionNote> committed;
    auto committedResult = gesture.mouseUp({noteView.timeToX(2.66), firstPoint.y - noteView.rowHeight}, 21, {},
        [&](const std::vector<ActionNote>& draft) { ++gestureCommits; committed = draft; return true; });
    check(committedResult == CommitResult::committed && gestureCommits == 1 && committed[1].pitch == 65,
          "mouse-up publishes one musical edit");
    check(gesture.mouseUp(firstPoint, 21, {}, [&](const auto&) { ++gestureCommits; return true; }) ==
              CommitResult::inactive && gestureCommits == 1,
          "duplicate mouse-up cannot create a second undo item");

    std::vector<ActionNote> overlapScore{{2, 3, 60, 100, 0, 0, true, true, false, false},
                                         {2, 3, 60, 90, 0, 1, true, true, false, false}};
    NoteGesture preferred;
    preferred.mouseDown(firstPoint, noteView, overlapScore, 21, {}, gestureOptions, 1);
    check(preferred.hit().valid() && preferred.hit().index == 1,
          "overlap cycle can pin the adapter-selected stack member");
    auto feedback = pointerFeedback(firstPoint, noteView, overlapScore, {}, gestureOptions,
                                    NoteGesturePhase::armed, {}, preferred.hit());
    check(feedback.hit.valid() && feedback.note == NoteFeedbackState::pressed &&
          feedback.cursor == CursorHint::move,
          "pressed note feedback exposes move cursor and pressed tint state");

    NoteGesture resize;
    resize.mouseDown({firstRect.x + 1, firstPoint.y}, noteView, score, 22, {}, gestureOptions);
    resize.mouseMove({noteView.timeToX(2.95), firstPoint.y}, {});
    check(resize.mode() == NoteGestureMode::resizeLeft &&
          resize.draft()[0].end - resize.draft()[0].start >= .125,
          "left-edge resize preserves minimum note length");

    NoteGesture copy;
    copy.mouseDown(firstPoint, noteView, score, 23, {false, true, false}, gestureOptions);
    copy.mouseMove({firstPoint.x + 8, firstPoint.y}, {false, true, false});
    check(copy.draft().size() == score.size() + 2 && copy.hit().index >= score.size() &&
          copy.draft().back().generated,
          "Shift-drag creates copies and follows the grabbed copy anchor");

    NoteGesture lasso;
    lasso.mouseDown({noteView.timeToX(1.5), noteView.pitchToY(68)}, noteView, score, 24, {}, gestureOptions);
    lasso.mouseMove({noteView.timeToX(6.5), noteView.pitchToY(58)}, {});
    std::size_t selectedByLasso = 0;
    for (const auto& note : lasso.draft()) selectedByLasso += note.selected ? 1u : 0u;
    check(lasso.phase() == NoteGesturePhase::lassoing && selectedByLasso == 2,
          "blank-area drag selects intersecting notes without an edit undo");
    check(lasso.commit(24, [&](const auto&) { ++gestureCommits; return true; }) == CommitResult::unchanged,
          "lasso selection is not misreported as a musical edit");

    std::vector<ActionNote> selectedScore{{2, 3, 60, 100, 0, 0, true, true, false, false},
                                          {4, 5, 64, 90, 0, 1, true, true, false, false}};
    selectedScore[0].selected = selectedScore[1].selected = true;
    const auto selectedBounds = Rect{noteView.timeToX(2), noteView.pitchToY(64),
                                    noteView.timeToX(5) - noteView.timeToX(2), 20};
    NoteGesture scale;
    scale.beginSelectionScale({selectedBounds.right(), selectedBounds.y + 10}, noteView,
                               selectedScore, 28, selectedBounds, {}, gestureOptions);
    scale.mouseMove({selectedBounds.right() + 80, selectedBounds.y + 10}, {});
    check(scale.mode() == NoteGestureMode::scaleSelection && scale.changed() &&
          scale.draft()[1].end > selectedScore[1].end,
          "selection stretch scales the selected group from its left anchor");

    NoteGesture stale;
    stale.mouseDown(firstPoint, noteView, score, 25, {}, gestureOptions);
    stale.mouseMove({firstPoint.x + 8, firstPoint.y}, {});
    check(stale.commit(26, [&](const auto&) { ++gestureCommits; return true; }) == CommitResult::stale &&
          gestureCommits == 1,
          "document replacement makes the old gesture stale");
    NoteGesture canceled;
    canceled.mouseDown(firstPoint, noteView, score, 27, {}, gestureOptions);
    canceled.mouseMove({firstPoint.x + 8, firstPoint.y}, {});
    canceled.cancel();
    check(canceled.commit(27, [&](const auto&) { ++gestureCommits; return true; }) == CommitResult::inactive,
          "cancelled gesture cannot commit");
    std::cout << passed << " experience checks passed\n";
}
