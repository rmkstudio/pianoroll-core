// Standalone checks for PianoRollView with a minimal host. They drive the real
// JUCE mouse handlers and record which host callbacks each gesture issues.
#include <PianoRollCore/PianoRollView.h>
#include <cmath>
#include <iostream>
#include <stdexcept>

using namespace pianoroll;
namespace {
int checks = 0;
void require(bool condition, const char* message)
{ ++checks; if (!condition) throw std::runtime_error(message); }

struct Host final : PianoRollHost {
    std::vector<ViewNote> notes, baseline;
    std::vector<bool> selected, startSelection;
    int ppq = 96, cc = 1, begins = 0, commits = 0, changes = 0, baselines = 0, seeks = 0;
    bool isBusy = false, loaded = true, dirty = false, rejectFollow = false, linked = false;
    double lastSeek = -1, loopA = 0, loopB = 0, strokeStart = -1, strokeEnd = -1;
    std::map<double, int> strokePoints;
    bool strokeOpen = false;
    std::vector<std::string> commands;
    PianoRollView* view = nullptr;

    bool hasDocument() const override { return loaded; }
    bool busy() const override { return isBusy; }
    int unitsPerQuarter() const override { return ppq; }
    double documentLength() const override { return ppq * 8.0; }
    double maximumTime() const override { return 0x0ffffffe; }
    double roundTime(double t) const override { return std::round(t); }
    std::size_t noteCount() const override { return notes.size(); }
    ViewNote note(std::size_t i) const override { return notes[i]; }
    bool noteSelected(std::size_t i) const override { return selected[i]; }
    bool hasSelection() const override
    { for (bool s : selected) if (s) return true; return false; }
    std::function<bool(std::size_t, juce::String&)> noteLabels() const override
    { return [](std::size_t, juce::String&) { return false; }; }
    std::function<bool(std::size_t)> keySwitches() const override
    { return [](std::size_t) { return false; }; }
    bool groupAlive(int) const override { return true; }
    bool groupInScope(int group) const override { return group == 0; }
    bool pitchesFolded() const override { return false; }
    bool rowHighlighted(int) const override { return false; }
    int controller() const override { return cc; }
    int channel() const override { return 0; }
    std::vector<LanePoint> controllerLane() const override { return {{0, 64, 0}, {192, 90, 2}}; }
    double playheadTime() const override { return lastSeek < 0 ? 0 : lastSeek; }
    bool loopRange(double& a, double& b) const override
    { a = loopA; b = loopB; return loopB > loopA; }
    void seek(double t) override { lastSeek = t; ++seeks; }
    void setLoop(double a, double b) override { loopA = a; loopB = b; lastSeek = a; }
    void clearLoop() override { loopA = loopB = 0; }
    bool canUndo() const override { return commits > 0; }
    bool canRedo() const override { return false; }
    void gestureStarted(double) override { startSelection = selected; dirty = false; }
    void beginEdit() override { ++begins; baseline = notes; }
    void abandonEdit() override {}
    void editChanged() override { ++changes; dirty = true; }
    void commitEdit() override { if (dirty) ++commits; dirty = false; }
    void baselineCaptured() override { ++baselines; baseline = notes; }
    void startSelectionCaptured() override { startSelection = selected; }
    bool startSelected(std::size_t i) const override
    { return i < startSelection.size() && startSelection[i]; }
    void focusGroupOf(std::size_t) override {}
    void moveNote(std::size_t i, double start, int pitch) override
    { notes[i].start = start; notes[i].pitch = pitch; }
    void setNoteLength(std::size_t i, double length) override { notes[i].length = length; }
    void setNoteTiming(std::size_t i, double start, double length) override
    { notes[i].start = start; notes[i].length = length; }
    void setNoteVelocity(std::size_t i, int v) override { notes[i].velocity = v; }
    void eraseNote(std::size_t i) override
    { notes.erase(notes.begin() + (std::ptrdiff_t)i); selected.erase(selected.begin() + (std::ptrdiff_t)i); }
    bool canCreateNote() const override { return true; }
    std::size_t createNote(double start, int pitch, double length, int velocity) override
    {
        ViewNote n; n.start = start; n.pitch = pitch; n.length = length; n.velocity = velocity;
        notes.push_back(n); selected.assign(notes.size(), false); selected.back() = true;
        return notes.size() - 1;
    }
    void appendBaselineCopy(std::size_t source) override
    { notes.push_back(baseline[source]); selected.push_back(true); }
    void clearSelection() override { selected.assign(notes.size(), false); }
    void selectNote(std::size_t i) override { selected[i] = true; }
    void deselectNote(std::size_t i) override { selected[i] = false; }
    void restoreStartSelection(bool keep) override
    { selected = keep ? startSelection : std::vector<bool>(notes.size(), false); selected.resize(notes.size()); }
    bool canDrawController() const override { return cc >= 0; }
    void beginControllerStroke(double t, bool) override { strokeOpen = true; strokeStart = strokeEnd = t; }
    bool controllerStrokeOpen() const override { return strokeOpen; }
    void updateControllerStroke(double a, double b, const std::map<double, int>& points, bool) override
    { strokeStart = a; strokeEnd = b; strokePoints = points; }
    bool followMovedNotes(bool) override
    { if (!rejectFollow) return true; notes = baseline; view->endGesture(); return false; }
    bool linkedNoteEnds() const override { return linked; }
    bool auditionNote(int, int) override { return true; }
    void stopAudition() override {}
    void pianoKeyDown(int) override {}
    bool pianoKeyUp(int) override { return false; }
    void exportSelectedNotes() override {}
    void laneLayoutChanged() override {}
    void reportMessage(const juce::String&) override {}
    void invokeCommand(const std::string& id) override { commands.push_back(id); }
    void addEditMenuItems(juce::PopupMenu&) override {}
    void viewWillResize() override {}
    void placeProductControls(const ViewLayout&) override {}
};

juce::MouseEvent mouse(juce::Component& c, juce::Point<float> at, int mods, juce::Point<float> down,
                       bool moved = false)
{
    auto now = juce::Time::getCurrentTime();
    return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), at, juce::ModifierKeys(mods),
                            1, 0, 0, 0, 0, &c, &c, now, down, now, 1, moved);
}
constexpr int left = juce::ModifierKeys::leftButtonModifier, right = juce::ModifierKeys::rightButtonModifier,
              shift = juce::ModifierKeys::shiftModifier, alt = juce::ModifierKeys::altModifier,
              ctrl = juce::ModifierKeys::ctrlModifier;
void drag(PianoRollView& view, juce::Point<float> a, juce::Point<float> b, int mods)
{
    view.mouseDown(mouse(view, a, mods, a));
    for (int i = 1; i <= 4; ++i) view.mouseDrag(mouse(view, a + (b - a) * (i / 4.f), mods, a, true));
    view.mouseUp(mouse(view, b, 0, a, b != a));
}
void click(PianoRollView& view, juce::Point<float> at, int mods = left) { drag(view, at, at, mods); }

struct Fixture {
    Host host;
    PianoRollView view{host};
    Fixture()
    {
        host.view = &view;
        auto add = [&](double start, double length, int pitch, int group, bool editable)
        { ViewNote n; n.start = start; n.length = length; n.pitch = pitch; n.group = group; n.editable = editable;
          n.velocity = 80; host.notes.push_back(n); host.selected.push_back(false); };
        add(0, 96, 60, 0, true);     // 0
        add(96, 192, 64, 0, true);   // 1
        add(48, 48, 67, 0, false);   // 2 read-only
        add(0, 384, 55, 1, true);    // 3 background group
        view.setSize(900, 600);
        view.fit();
    }
    juce::Point<float> centre(std::size_t i) { return view.noteRect(host.notes[i]).getCentre(); }
    float ticks(double t) { return (float)(t * view.noteArea().getWidth() / view.timeSpan()); }
    float rowHeight() { return view.noteRect(host.notes[0]).getHeight() + 1; }
};

void geometryAndPaint()
{
    Fixture f;
    require(f.view.noteArea().getWidth() > 0 && f.view.velocityLaneBounds().getHeight() > 0 &&
                f.view.controllerLaneBounds().getHeight() > 0, "Default layout places all regions");
    require(f.view.noteArea().contains(f.centre(0)) && f.view.noteArea().contains(f.centre(1)),
            "Fit shows in-scope notes");
    require(std::abs(f.view.xToTime(f.view.timeToX(123.0)) - 123.0) < 1e-2, "Time/x round trip (float pixels)");
    require(f.view.yToPitch(f.centre(1).y) == 64, "Pitch from row centre");
    auto image = f.view.createComponentSnapshot(f.view.getLocalBounds());
    require(image.isValid() && image.getWidth() == 900, "View paints");
    require(f.view.quantize(50) == 48 && f.view.quantize(50, true) == 50, "1/16 snap at PPQ 96 and Alt free");
}
void selectionAndMove()
{
    Fixture f;
    f.view.setPaintMode(false);
    click(f.view, f.centre(1));
    require(f.host.selected[1] && !f.host.selected[0], "Click selects one note");
    require(f.host.commits == 0, "Selecting is not an edit");
    const int begins = f.host.begins;
    drag(f.view, f.centre(1), f.centre(1) + juce::Point<float>(f.ticks(50), -2 * f.rowHeight()), left);
    require(f.host.notes[1].start == 144 && f.host.notes[1].pitch == 66, "Move snaps to the grid and rows");
    require(f.host.notes[1].length == 192 && f.host.notes[1].velocity == 80, "Move keeps length and velocity");
    require(f.host.begins == begins + 1 && f.host.commits == 1, "One gesture, one before-image, one commit");
    drag(f.view, f.centre(1), f.centre(1) + juce::Point<float>(f.ticks(13), 0), left | alt);
    require(f.host.notes[1].start == 157, "Alt moves without snapping, host rounding applied");
    const auto before = f.host.notes[1];
    drag(f.view, f.centre(1), f.centre(1) + juce::Point<float>(2, 0), left);
    require(f.host.notes[1].start == before.start, "Drags under three pixels do nothing");
    drag(f.view, f.centre(2), f.centre(2) + juce::Point<float>(f.ticks(96), 0), left);
    require(f.host.notes[2].start == 48, "Read-only notes cannot be dragged");
}
void resizeCopyAndFollow()
{
    Fixture f;
    click(f.view, f.centre(0));
    auto edge = f.view.noteRect(f.host.notes[0]);
    juce::Point<float> grab(edge.getRight() - 1.5f, edge.getCentreY());
    drag(f.view, grab, grab + juce::Point<float>(f.ticks(48), 0), left);
    require(f.host.notes[0].length == 144 && f.host.notes[0].start == 0, "Right edge resizes");
    drag(f.view, f.centre(0), f.centre(0) + juce::Point<float>(f.ticks(192), 0), left | shift);
    require(f.host.notes.size() == 5 && f.host.notes[4].start == 192 && f.host.notes[0].start == 0,
            "Shift drag copies, original stays");
    require(f.host.selected[4] && !f.host.selected[0], "Copy becomes the selection");
    f.host.rejectFollow = true;
    click(f.view, f.centre(1));
    const auto start = f.host.notes[1].start;
    f.view.mouseDown(mouse(f.view, f.centre(1), left, f.centre(1)));
    f.view.mouseDrag(mouse(f.view, f.centre(1) + juce::Point<float>(f.ticks(96), 0), left, f.centre(1), true));
    require(!f.view.gestureActive() && f.host.notes[1].start == start,
            "Rejected follow ends the gesture and keeps the host rollback");
    f.view.mouseUp(mouse(f.view, f.centre(1), 0, f.centre(1), true));
}
void pencilEraseMarquee()
{
    Fixture f;
    f.view.setPaintMode(true);
    const juce::Point<float> empty(f.view.noteArea().getX() + f.ticks(500 - f.view.timeOffset()), f.centre(0).y);
    click(f.view, empty);
    require(f.host.notes.size() == 5 && f.host.notes[4].start == 480 && f.host.notes[4].pitch == 60 &&
                f.host.selected[4], "Pencil creates a snapped note (rounding down) and selects it");
    f.view.setPaintMode(false);
    drag(f.view, f.centre(0) - juce::Point<float>(4, 0), f.centre(1), right);
    require(f.host.notes.size() == 3, "Right drag erases notes along the path");
    std::size_t readOnly = 0;
    while (f.host.notes[readOnly].editable) ++readOnly;
    drag(f.view, f.centre(readOnly), f.centre(readOnly), right);
    require(f.host.notes.size() == 3, "Read-only notes are not erased");
    const auto from = juce::Point<float>(f.view.noteArea().getX() + 2, f.view.noteArea().getY() + 2);
    drag(f.view, from, f.view.noteArea().getBottomRight() - juce::Point<float>(2, 2), left);
    bool background = false;
    for (std::size_t i = 0; i < f.host.notes.size(); ++i)
        if (f.host.notes[i].group == 1) background = f.host.selected[i];
    require(f.host.hasSelection() && !background, "Marquee selects only in-scope notes");
}
void lanesNavigationAndRuler()
{
    Fixture f;
    const auto lane = f.view.velocityLaneBounds();
    drag(f.view, {f.view.timeToX(0) + 1, lane.getBottom() - 10}, {f.view.timeToX(96) + 1, lane.getY() + 2}, left);
    require(f.host.notes[0].velocity != 80 && f.host.notes[1].velocity > f.host.notes[0].velocity,
            "Velocity stroke interpolates across notes");
    const auto cc = f.view.controllerLaneBounds();
    drag(f.view, {f.view.timeToX(10), cc.getBottom() - 5}, {f.view.timeToX(200), cc.getY() + 5}, left);
    require(f.host.strokeStart >= 0 && f.host.strokeEnd > f.host.strokeStart && f.host.strokePoints.size() >= 2,
            "Controller stroke reports its range and points");
    const auto span = f.view.timeSpan();
    const auto notes = f.host.notes;
    juce::MouseWheelDetails wheel{0, .25f, false, false, false};
    f.view.mouseWheelMove(mouse(f.view, f.centre(1), ctrl, f.centre(1)), wheel);
    require(f.view.timeSpan() < span, "Ctrl+wheel zooms time");
    require(f.host.notes[1].start == notes[1].start, "Navigation does not edit");
    f.view.fit();
    const float ruler = f.view.noteArea().getY() - 10;
    click(f.view, {f.view.timeToX(200), ruler});
    require(std::abs(f.host.lastSeek - 200) < .5, "Ruler click seeks to the clicked position");
    drag(f.view, {f.view.timeToX(97), ruler}, {f.view.timeToX(290), ruler}, left);
    require(f.host.loopA == 96 && f.host.loopB == 288, "Ruler drag sets a snapped loop");
}
juce::Button* button(juce::Component& root, const juce::String& name)
{
    for (auto* child : root.getChildren())
        if (child->getName() == name)
            return dynamic_cast<juce::Button*>(child);
    return nullptr;
}
void toolbar()
{
    using Rect = ViewLayout::Rect;
    for (int width : {560, 688, 900, 1400})
    {
        const auto r = defaultViewLayout(width, 600, true, true, .76f, .12f);
        const std::vector<Rect> row{r.play, r.stop, r.clock, r.undo, r.redo, r.pencil, r.pointer,
                                    r.snap, r.edit, r.follow, r.fit, r.expand};
        const std::vector<Rect> context{r.editTrack, r.channel, r.allTracks, r.drag};
        for (const auto* group : {&row, &context})
            for (std::size_t i = 0; i < group->size(); ++i)
            {
                const auto a = (*group)[i];
                if (a.isEmpty())
                    continue;
                require(a.getX() >= 0 && a.getRight() <= width, "Toolbar control outside the view");
                for (std::size_t j = i + 1; j < group->size(); ++j)
                    require(!a.intersects((*group)[j]), "Toolbar controls overlap");
            }
        require(r.clock.getWidth() >= (width >= 688 ? 120 : 0), "Clock keeps room at usual widths");
        require(r.notes.getY() > r.allTracks.getBottom() && r.scroll.getBottom() <= 600,
                "Lanes start below the toolbar and end in the view");
        require(r.controller.getBottom() <= r.cc.getY() && r.controller.getRight() <= r.cc.getX() + 62,
                "Controller slot sits in the CC lane header");
    }
    Fixture f;
    f.view.setPaintMode(true);
    require(f.view.isPaintMode(), "Paint mode shown on the draw button");
    auto* pointer = button(f.view, "Select tool");
    auto* pencil = button(f.view, "Draw tool");
    require(pointer && pencil && pencil->getToggleState(), "Tool buttons present");
    auto pump = [] { juce::MessageManager::getInstance()->runDispatchLoopUntil(40); };
    pointer->triggerClick();
    pump();
    require(!f.view.isPaintMode() && pointer->getToggleState() && !pencil->getToggleState(),
            "Clicking select switches tools exclusively");
    button(f.view, "Stop")->triggerClick();
    pump();
    button(f.view, "Play / pause")->triggerClick();
    pump();
    require(f.host.commands.size() == 2 && f.host.commands[0] == "transport.stop" &&
                f.host.commands[1] == "transport.playPause",
            "Transport buttons issue their commands");
    f.view.setTransportState(true, true, "1:00", {});
    require(button(f.view, "Play / pause")->getProperties()["icon"].toString() == "pause",
            "Playing shows the pause icon");
    require(button(f.view, "Home") == nullptr && button(f.view, "menu") != nullptr,
            "Home button removed; one edit menu");
}
void laneSplitFollowsMouse()
{
    Fixture f;
    const float before = f.view.noteArea().getHeight(), split = f.view.velocityLaneBounds().getY() - 10;
    drag(f.view, {400, split}, {400, split - 40}, left);
    require(std::abs(f.view.noteArea().getHeight() - (before - 40)) <= 2,
            "Velocity splitter follows the mouse");
    const float cc = f.view.controllerLaneBounds().getHeight(), ccSplit = f.view.controllerLaneBounds().getY() - 12;
    drag(f.view, {400, ccSplit}, {400, ccSplit - 20}, left);
    require(std::abs(f.view.controllerLaneBounds().getHeight() - (cc + 20)) <= 2,
            "CC splitter follows the mouse");
}
void busyBlocksEdits()
{
    Fixture f;
    f.host.isBusy = true;
    click(f.view, f.centre(1));
    drag(f.view, f.centre(1), f.centre(1) + juce::Point<float>(f.ticks(96), 0), left);
    require(f.host.notes[1].start == 96 && f.host.begins == 0, "Busy host receives no edits");
}
}
int main()
{
    try {
        juce::ScopedJuceInitialiser_GUI initialiser;
        geometryAndPaint(); selectionAndMove(); resizeCopyAndFollow(); pencilEraseMarquee();
        lanesNavigationAndRuler(); busyBlocksEdits(); toolbar(); laneSplitFollowsMouse();
        std::cout << checks << " piano-roll view checks passed\n"; return 0;
    } catch (const std::exception& error) { std::cerr << "FAILED: " << error.what() << '\n'; return 1; }
}
