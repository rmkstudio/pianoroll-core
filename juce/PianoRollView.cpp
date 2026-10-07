#include <PianoRollCore/PianoRollView.h>
#include <algorithm>
using namespace juce;
namespace pianoroll
{
namespace
{
Rect coreRect(Rectangle<float> r)
{
    return {r.getX(), r.getY(), r.getWidth(), r.getHeight()};
}
Rectangle<float> juceRect(Rect r)
{
    return {(float)r.x, (float)r.y, (float)r.width, (float)r.height};
}
void makeCursors(const ViewPalette& colours, MouseCursor& pencil, MouseCursor& copy)
{
    Image icon(Image::ARGB, 24, 24, true);
    {
        Graphics g(icon);
        g.setColour(colours.ink);
        g.drawLine(5, 18, 19, 4, 5);
        g.setColour(colours.accent);
        g.drawLine(5, 18, 19, 4, 2);
        Path tip;
        tip.addTriangle(2, 22, 4, 15, 9, 20);
        g.fillPath(tip);
    }
    pencil = MouseCursor(icon, 2, 22);
    {
        Graphics g(icon);
        g.setColour(colours.ink);
        g.fillRect(16, 15, 8, 3);
        g.fillRect(19, 12, 3, 9);
    }
    copy = MouseCursor(icon, 2, 22);
}
}

ViewLayout defaultViewLayout(int w, int h, bool velocity, bool cc, float nf, float vf)
{
    ViewLayout r;
    using R = ViewLayout::Rect;
    const int ch = 28;
    // Toolbar: transport | clock (flexible) | history | tools | snap | edit ... follow fit expand
    r.expand = {w - 30, 0, 30, ch};
    r.fit = {w - 66, 0, 30, ch};
    r.follow = {w - 102, 0, 30, ch};
    r.play = {0, 0, 38, ch};
    r.stop = {38, 0, 38, ch};
    const int end = r.follow.getX() - 10;
    const bool compact = end - 86 - 404 < 120;
    const int tool = compact ? 34 : 64;
    const int fixed = 30 + 2 + 30 + 10 + 2 * tool + 10 + 108 + 6 + 70;
    const int clock = jlimit(0, 240, end - 86 - fixed - 10);
    r.clock = {86, 0, clock, ch};
    int x = 86 + clock + 10;
    auto take = [&](int width, int gap)
    {
        R q{x, 0, width, ch};
        x += width + gap;
        return q;
    };
    r.undo = take(30, 2);
    r.redo = take(30, 10);
    r.pencil = take(tool, 0);
    r.pointer = take(tool, 10);
    r.snap = take(108, 6);
    r.edit = take(70, 0);
    const int y = 36, track = jlimit(120, 240, w - 56 - 6 - 84 - 6 - 120 - 6 - 96);
    r.editTrack = {56, y, track, ch};
    r.channel = {r.editTrack.getRight() + 6, y, 84, ch};
    r.allTracks = {r.channel.getRight() + 6, y, 120, ch};
    r.drag = {w - 90, y, 90, ch};
    const int start = 92, bottom = h - 18;
    const int usable = jmax(150, bottom - start);
    int vh = velocity ? jmax(44, roundToInt(usable * vf)) : 0;
    int cch = cc ? jmax(50, roundToInt(usable * jmax(.06f, 1.f - nf - vf))) : 0;
    const int nh = jmax(50, usable - vh - cch);
    r.ruler = {58, start - 20, w - 59, 20};
    r.notes = {58, start, w - 59, nh};
    r.keys = {0, start, 58, nh};
    if (velocity)
        r.velocity = {58, start + nh + 19, w - 59, jmax(24, vh - 19)};
    if (cc)
    {
        r.cc = {58, start + nh + vh + 24, w - 59, jmax(24, cch - 24)};
        r.controller = {0, r.cc.getY() - 23, 120, 22};
    }
    r.scroll = {58, h - 11, w - 59, 9};
    return r;
}

PianoRollView::PianoRollView(PianoRollHost& owner) : host(owner)
{
    for (auto* b : {&undoButton, &redoButton, &playButton, &stopButton})
        b->getProperties().set("quiet", true);
    makeCursors(colours, pencil, copyCursor);
    const std::pair<Button*, const char*> icons[] = {
        {&playButton, "play"},       {&stopButton, "stop"},       {&undoButton, "undo"},
        {&redoButton, "redo"},       {&pencilButton, "pencil"},   {&pointerButton, "pointer"},
        {&followButton, "follow"}};
    for (auto [button, icon] : icons)
        button->getProperties().set("icon", icon);
    for (auto [button, segment] : std::initializer_list<std::pair<Button*, const char*>>{
             {&playButton, "first"},
             {&stopButton, "last"},
             {&pencilButton, "first"},
             {&pointerButton, "last"}})
        button->getProperties().set("segment", segment);
    pencilButton.setName("Draw tool");
    pointerButton.setName("Select tool");
    pencilButton.setTooltip(String::fromUTF8("画笔（1）· Alt 临时取消吸附"));
    pointerButton.setTooltip(String::fromUTF8("选择（2）· Alt 临时取消吸附"));
    for (auto* b : {&pencilButton, &pointerButton})
    {
        b->setClickingTogglesState(true);
        b->setRadioGroupId(0x5052);
        b->onClick = [this]
        {
            updateCursor(hover, ModifierKeys::getCurrentModifiersRealtime());
            repaint();
        };
    }
    pointerButton.setToggleState(true, dontSendNotification);
    undoButton.setTooltip(String::fromUTF8("撤销（Ctrl+Z）"));
    redoButton.setTooltip(String::fromUTF8("重做（Ctrl+Shift+Z）"));
    for (auto* b : {&undoButton, &redoButton, &playButton, &stopButton, &pencilButton,
                    &pointerButton, &editButton})
    {
        addAndMakeVisible(*b);
        b->setWantsKeyboardFocus(false);
    }
    addAndMakeVisible(snapSelect);
    snapSelect.setName("Snap");
    snapSelect.addItemList({String::fromUTF8("不吸附"), "1/4", "1/8", "1/16", "1/32",
                            String::fromUTF8("1/8 三连"), String::fromUTF8("1/16 三连")},
                           1);
    snapSelect.setSelectedId(4);
    snapSelect.getProperties().set("prefix", String::fromUTF8("吸附"));
    snapSelect.setTooltip(String::fromUTF8("吸附网格（音符）· Alt 拖动临时取消吸附"));
    addAndMakeVisible(followButton);
    followButton.setToggleState(true, dontSendNotification);
    followButton.setWantsKeyboardFocus(false);
    followButton.setTooltip(String::fromUTF8("播放时跟随播放头"));
    followButton.onClick = [this]
    {
        follow.setEnabled(followButton.getToggleState());
    };
    addAndMakeVisible(transportLabel);
    transportLabel.setFont(colours.font(13));
    transportLabel.setColour(Label::textColourId, colours.muted);
    playButton.setName("Play / pause");
    stopButton.setName("Stop");
    followButton.setName("Follow playhead");
    playButton.setTooltip(String::fromUTF8("空格／Enter：播放或暂停 · 参考钢琴音色"));
    stopButton.setTooltip(String::fromUTF8("停止回定位点，再按一次回开头（Home 键直接回开头）"));
    for (auto [button, id] : std::initializer_list<std::pair<Button*, const char*>>{
             {&undoButton, "edit.undo"},
             {&redoButton, "edit.redo"},
             {&playButton, "transport.playPause"},
             {&stopButton, "transport.stop"}})
        button->onClick = [this, command = std::string(id)]
        {
            host.invokeCommand(command);
        };
    for (auto* b : {&velocityLaneButton, &ccLaneButton})
    {
        addAndMakeVisible(*b);
        b->setWantsKeyboardFocus(false);
        b->setToggleState(true, dontSendNotification);
    }
    velocityLaneButton.setName("Show velocity lane");
    ccLaneButton.setName("Show CC lane");
    velocityLaneButton.onClick = [this]
    {
        showVelocity = velocityLaneButton.getToggleState();
        host.laneLayoutChanged();
        resized();
        repaint();
    };
    ccLaneButton.onClick = [this]
    {
        showCC = ccLaneButton.getToggleState();
        host.laneLayoutChanged();
        resized();
        repaint();
    };
    editButton.setName("menu");
    editButton.setTooltip(String::fromUTF8("适应选区、命令搜索、数值塑形和视图选项"));
    editButton.onClick = [this]
    {
        showEditMenu();
    };
    addAndMakeVisible(hscroll);
    hscroll.addListener(this);
}
PianoRollView::~PianoRollView()
{
    hscroll.removeListener(this);
}
void PianoRollView::setPalette(ViewPalette palette)
{
    colours = std::move(palette);
    makeCursors(colours, pencil, copyCursor);
    transportLabel.setFont(colours.font(13));
    transportLabel.setColour(Label::textColourId, colours.muted);
    repaint();
}
std::vector<Component*> PianoRollView::keyboardControls()
{
    return {&snapSelect};
}
void PianoRollView::showTool(bool paint)
{
    (paint ? pencilButton : pointerButton).setToggleState(true, dontSendNotification);
}
void PianoRollView::showEditMenu()
{
    PopupMenu menu;
    menu.setLookAndFeel(&getLookAndFeel());
    const bool ready = host.hasDocument() && !host.busy();
    Component::SafePointer<PianoRollView> safe(this);
    auto run = [safe](std::string id)
    {
        return [safe, id]
        {
            if (safe)
                safe->host.invokeCommand(id);
        };
    };
    menu.addSectionHeader(String::fromUTF8("编辑"));
    menu.addItem(String::fromUTF8("适应选区"), ready, false, run("view.fitSelection"));
    PopupMenu::Item search(String::fromUTF8("命令搜索…"));
    search.setEnabled(ready).setAction(run("commands.search"));
    search.shortcutKeyDescription = "Ctrl+K";
    menu.addItem(search);
    menu.addItem(String::fromUTF8("数值塑形…"), ready, false,
                 run(host.controller() >= 0 ? "automation.transformBox" : "velocity.transformBox"));
    host.addEditMenuItems(menu);
    menu.showMenuAsync(PopupMenu::Options().withTargetComponent(&editButton));
}

// ---- View window and geometry ----

void PianoRollView::fit()
{
    const double ppq = host.unitsPerQuarter();
    total = jmax(ppq * 4, host.documentLength());
    int lo = 127, hi = 0;
    for (std::size_t i = 0; i < host.noteCount(); ++i)
    {
        const auto n = host.note(i);
        if (inScope(n))
        {
            total = jmax(total, n.start + n.length);
            lo = jmin(lo, n.pitch);
            hi = jmax(hi, n.pitch);
        }
    }
    left = 0;
    span = total * 1.06;
    if (lo <= hi)
    {
        row = jlimit(jmax(.5f, notesArea.getHeight() / 128.f), 20.f,
                     notesArea.getHeight() / (hi - lo + 9.f));
        topPitch = jmin(127.f, hi + 4.f);
    }
    else
    {
        row = 12;
        topPitch = 80;
    }
    if (host.pitchesFolded())
    {
        topPitch = 127;
        row = jlimit(3.f, 48.f, notesArea.getHeight() / jmax(1, (int)pitchRows.size()));
    }
    clampView();
    repaint();
}
void PianoRollView::fitSelection()
{
    if (!host.hasSelection())
    {
        fit();
        return;
    }
    const double ppq = host.unitsPerQuarter();
    double lo = 1e20, hi = 0;
    int pitchLo = 127, pitchHi = 0;
    for (std::size_t i = 0; i < host.noteCount(); ++i)
    {
        const auto n = host.note(i);
        if (inScope(n) && host.noteSelected(i))
        {
            lo = jmin(lo, n.start);
            hi = jmax(hi, n.start + n.length);
            pitchLo = jmin(pitchLo, n.pitch);
            pitchHi = jmax(pitchHi, n.pitch);
        }
    }
    if (lo > hi)
        return;
    left = jmax(0., lo - ppq * .25);
    span = jmax(ppq, hi - lo + ppq * .5);
    row = jlimit(jmax(.5f, notesArea.getHeight() / 128.f), 48.f,
                 notesArea.getHeight() / (pitchHi - pitchLo + 7.f));
    topPitch = jmin(127.f, pitchHi + 3.f);
    follow.userNavigated();
    clampView();
    repaint();
}
void PianoRollView::showTimeAround(double time)
{
    const double ppq = host.unitsPerQuarter();
    left = jmax(0., time - ppq * 2);
    span = jmax(ppq * 8, jmin(span, ppq * 32));
    follow.transportLocated();
    clampView();
}
void PianoRollView::setTimeWindow(double start, double width)
{
    left = start;
    span = width;
    clampView();
    repaint();
}
void PianoRollView::clampView()
{
    const double ppq = host.unitsPerQuarter();
    span = jlimit(12.0, jmax(ppq * 1024, host.documentLength() + ppq * 8), span);
    left = jlimit(0.0, 268000000.0 - span, left);
    row = jlimit(jmax(.5f, notesArea.getHeight() / 128.f), 48.f, row);
    topPitch = jlimit(jmin(127.f, notesArea.getHeight() / row - 1), 127.f, topPitch);
    hscroll.setRangeLimits(0, jmax(total + ppq * 4, left + span));
    hscroll.setCurrentRange(left, span, dontSendNotification);
}
Viewport PianoRollView::viewport() const
{
    return {coreRect(notesArea), left, span, topPitch, row};
}
SnapPolicy PianoRollView::snapPolicy(bool free, Rounding rounding) const
{
    static const double beats[] = {0, 1, .5, .25, .125, 1.0 / 3, 1.0 / 6};
    // The product applies its own final rounding (Recover: JUCE roundToInt).
    const auto step = host.unitsPerQuarter() * beats[jlimit(0, 6, snapSelect.getSelectedId() - 1)];
    return {free ? host.minimumNoteLength() : jmax(host.minimumNoteLength(), step), rounding, 0, 0};
}
double PianoRollView::quantize(double time, bool free) const
{
    return host.roundTime(snapTime(time, snapPolicy(free)));
}
GroupBounds PianoRollView::selectedBounds(const std::vector<ViewNote>& source) const
{
    GroupBounds bounds;
    for (std::size_t i = 0; i < source.size(); ++i)
        if (host.noteSelected(i))
            bounds.include(source[i].start, source[i].start + source[i].length, source[i].pitch,
                           source[i].editable);
    return bounds;
}
float PianoRollView::timeToX(double time) const
{
    return (float)viewport().timeToX(time);
}
double PianoRollView::xToTime(float px) const
{
    return viewport().xToTime(px);
}
float PianoRollView::pitchToY(int pitch) const
{
    if (host.pitchesFolded() && !pitchRows.empty())
    {
        auto it = std::lower_bound(pitchRows.begin(), pitchRows.end(), pitch);
        if (it == pitchRows.end() || *it != pitch)
            return -1000;
        return notesArea.getY() + ((int)pitchRows.size() - 1 -
                                   (int)std::distance(pitchRows.begin(), it) - (127 - topPitch)) *
                                      row;
    }
    return (float)viewport().pitchToY(pitch);
}
int PianoRollView::yToPitch(float py) const
{
    if (host.pitchesFolded() && !pitchRows.empty())
        return pitchRows[jlimit(0, (int)pitchRows.size() - 1,
                                (int)pitchRows.size() - 1 -
                                    (int)((py - notesArea.getY()) / row + 127 - topPitch))];
    return viewport().yToPitch(py);
}
Rectangle<float> PianoRollView::noteRect(const ViewNote& n) const
{
    auto r =
        juceRect(viewport().noteRect(n.start, n.start + n.length, n.pitch, jmax(1.f, row - 1), 3));
    r.setY(pitchToY(n.pitch));
    return r;
}
bool PianoRollView::displayed(const ViewNote& n) const
{
    return host.groupAlive(n.group) &&
           (!host.pitchesFolded() || pitchRows.empty() ||
            std::find(pitchRows.begin(), pitchRows.end(), n.pitch) != pitchRows.end());
}
int PianoRollView::hit(juce::Point<float> point, bool allowBackground) const
{
    auto find = [&](bool foreground)
    {
        return pianoroll::hitTest(host.noteCount(), {point.x, point.y},
                                  [&](std::size_t i)
                                  {
                                      const auto n = host.note(i);
                                      return HitCandidate{
                                          coreRect(noteRect(n)), 0,
                                          displayed(n) && (foreground ? inScope(n) : !inScope(n))};
                                  });
    };
    // Match the paint order: foreground wins when two notes overlap exactly.
    const int foreground = find(true);
    return foreground >= 0 || !allowBackground ? foreground : find(false);
}
void PianoRollView::setPitchRows(std::vector<int> rows, bool resetFoldedView)
{
    pitchRows = std::move(rows);
    if (resetFoldedView && host.pitchesFolded() && !pitchRows.empty())
    {
        topPitch = 127;
        row = jlimit(3.f, 48.f, notesArea.getHeight() / pitchRows.size());
    }
}
void PianoRollView::setMeter(std::vector<MeterChange> changes, int unitsPerQuarter)
{
    meter.assign(std::move(changes), unitsPerQuarter);
}
ViewState PianoRollView::navigationView() const
{
    return {left, span, topPitch - notesArea.getHeight() / row + 1, topPitch};
}
NavigationLimits PianoRollView::navigationLimits() const
{
    const double ppq = host.unitsPerQuarter();
    return {jmax(host.minimumNoteLength(), ppq / 16.), jmax(ppq * 1024, total * 2), jmax(total + ppq * 4, left + span),
            jmax(4., notesArea.getHeight() / 48.)};
}
void PianoRollView::applyNavigation(ViewState v)
{
    left = v.timeStart;
    span = v.timeSpan;
    topPitch = (float)v.pitchHigh;
    row = (float)(notesArea.getHeight() / v.pitchSpan());
    clampView();
    repaint();
}

// ---- Lanes, tools and transport ----

void PianoRollView::setLaneLayout(bool velocity, bool controller, float notes, float velocities)
{
    showVelocity = velocity;
    showCC = controller;
    noteFraction = notes;
    velocityFraction = velocities;
    velocityLaneButton.setToggleState(showVelocity, dontSendNotification);
    ccLaneButton.setToggleState(showCC, dontSendNotification);
    resized();
}
void PianoRollView::showControllerLane()
{
    if (showCC)
        return;
    showCC = true;
    ccLaneButton.setToggleState(true, dontSendNotification);
    resized();
}
void PianoRollView::setPaintMode(bool paint)
{
    showTool(paint);
    updateCursor(hover, ModifierKeys::getCurrentModifiersRealtime());
    repaint();
}
void PianoRollView::setTransportState(bool playing, bool canPlay, const String& text,
                                      const String& tooltip)
{
    playButton.setButtonText(playing ? String::fromUTF8("暂停") : String::fromUTF8("播放"));
    playButton.getProperties().set("icon", playing ? "pause" : "play");
    playButton.setEnabled(canPlay);
    transportLabel.setText(text, dontSendNotification);
    transportLabel.setTooltip(tooltip);
}
void PianoRollView::followPlayback(double playhead)
{
    auto next =
        follow.update(navigationView(), playhead, true, gestureActive(), navigationLimits());
    applyNavigation(next);
    repaint();
}
void PianoRollView::setFollowEnabled(bool enabled)
{
    follow.setEnabled(enabled);
    followButton.setToggleState(follow.enabled(), dontSendNotification);
}
void PianoRollView::resized()
{
    host.viewWillResize();
    const auto r = host.viewLayout(getWidth(), getHeight(), showVelocity, showCC, noteFraction,
                                   velocityFraction);
    undoButton.setBounds(r.undo);
    redoButton.setBounds(r.redo);
    pencilButton.setBounds(r.pencil);
    pointerButton.setBounds(r.pointer);
    snapSelect.setBounds(r.snap);
    editButton.setBounds(r.edit);
    velocityLaneButton.setVisible(false);
    ccLaneButton.setVisible(false);
    rulerArea = r.ruler.toFloat();
    notesArea = r.notes.toFloat();
    keysArea = r.keys.toFloat();
    velocityArea = r.velocity.toFloat();
    ccArea = r.cc.toFloat();
    laneBottom = showCC && !r.cc.isEmpty()                ? ccArea.getBottom()
                 : showVelocity && !r.velocity.isEmpty() ? velocityArea.getBottom()
                                                         : notesArea.getBottom();
    hscroll.setBounds(r.scroll);
    playButton.setBounds(r.play);
    stopButton.setBounds(r.stop);
    followButton.setBounds(r.follow);
    transportLabel.setBounds(r.clock);
    host.placeProductControls(r);
    clampView();
}

// ---- Drawing ----

void PianoRollView::label(Graphics& g, const String& s, Rectangle<float> r, float size,
                          Colour colour) const
{
    g.setColour(colour);
    g.setFont(colours.font(size));
    g.drawText(s, r, Justification::centredLeft, true);
}
String PianoRollView::pitchName(int n) const
{
    return String(pianoroll::pitchName(n, 5));
}
void PianoRollView::paint(Graphics& g)
{
    const auto& c = colours;
    const double ppq = host.unitsPerQuarter();
    g.fillAll(c.background);
    // Thin dividers group transport, history and tools without boxing every action.
    g.setColour(c.line);
    for (int xx : {undoButton.getX() - 6, pencilButton.getX() - 6})
        g.drawVerticalLine(xx, 6.f, 22.f);
    g.drawHorizontalLine(32, 0.f, (float)getWidth());
    g.setColour(c.panel);
    g.fillRect(notesArea);
    g.fillRect(velocityArea);
    g.fillRect(ccArea);
    // Each region is independently clipped, including the ruler and pitch labels.
    {
        Graphics::ScopedSaveState guard(g);
        g.reduceClipRegion(notesArea.toNearestInt());
        for (int n = 0; n < 128; ++n)
        {
            auto yy = pitchToY(n);
            bool black = n % 12 == 1 || n % 12 == 3 || n % 12 == 6 || n % 12 == 8 || n % 12 == 10;
            g.setColour(host.rowHighlighted(n) ? c.scaleRow : (black ? c.blackRow : c.whiteRow));
            g.fillRect(notesArea.getX(), yy, notesArea.getWidth(), jmax(.3f, row - 1));
        }
    }
    const auto marks = meter.grid(left, left + span, span * 8 / jmax(1.f, notesArea.getWidth()),
                                  2048, snapPolicy().step);
    // Label every Nth bar (N a power of two) so numbers keep about 34 px apart.
    int barStep = 1;
    {
        float firstBar = -1, barWidth = 0;
        for (auto mark : marks)
            if (mark.barStart)
            {
                if (firstBar < 0)
                    firstBar = timeToX(mark.time);
                else
                {
                    barWidth = timeToX(mark.time) - firstBar;
                    break;
                }
            }
        while (barWidth > 0 && barWidth * barStep < 34 && barStep < 65536)
            barStep *= 2;
    }
    float labelEnd = -1e9f;
    for (auto area : {notesArea, velocityArea, ccArea, rulerArea})
    {
        Graphics::ScopedSaveState guard(g);
        g.reduceClipRegion(area.toNearestInt());
        for (auto mark : marks)
        {
            auto xx = timeToX(mark.time);
            g.setColour(mark.barStart ? c.line : mark.beatStart ? c.beatLine : c.subdivisionLine);
            g.drawVerticalLine(roundToInt(xx), area.getY(), area.getBottom());
            if (area == rulerArea && mark.barStart &&
                (mark.signatureChange || (mark.bar - 1) % barStep == 0))
            {
                String name((int64)mark.bar);
                if (mark.signatureChange)
                {
                    auto pos = meter.positionAt(mark.time);
                    name += "  " + String(pos.numerator) + "/" + String(pos.denominator);
                }
                // A wide label (bar plus signature) may still reach the next one: skip it.
                if (xx + 4 >= labelEnd + 6)
                {
                    label(g, name, {xx + 4, area.getY(), 80, 18}, 11, c.muted);
                    labelEnd = xx + 4 + c.font(11).getStringWidthFloat(name);
                }
            }
        }
    }
    {
        Graphics::ScopedSaveState guard(g);
        g.reduceClipRegion(keysArea.toNearestInt());
        for (int n = 0; n < 128; ++n)
        {
            auto yy = pitchToY(n);
            bool black = n % 12 == 1 || n % 12 == 3 || n % 12 == 6 || n % 12 == 8 || n % 12 == 10;
            g.setColour(black ? c.blackKey : c.whiteKey);
            g.fillRect(0.f, yy, black ? 36.f : 56.f, jmax(.3f, row - 1));
            if (showPitchLabel(row, 50, n % 12 == 0))
                label(g, pitchName(n), {3, yy, 50, row - 1}, jmin(12.f, row - 1),
                      n % 12 == 0 ? c.ink : c.muted);
        }
    }
    paintedNotes.clear();
    std::map<int, std::pair<bool, bool>> groups;
    const bool folded = host.pitchesFolded() && !pitchRows.empty();
    for (std::size_t i = 0; i < host.noteCount(); ++i)
    {
        const auto n = host.note(i);
        auto group = groups.find(n.group);
        if (group == groups.end())
            group = groups.emplace(n.group, std::make_pair(host.groupAlive(n.group),
                                                           host.groupInScope(n.group))).first;
        if (!group->second.first ||
            (folded && std::find(pitchRows.begin(), pitchRows.end(), n.pitch) == pitchRows.end()))
            continue;
        const auto bounds = noteRect(n);
        // Keep long notes crossing the left edge; velocity uses the original start.
        if (bounds.getRight() < notesArea.getX() || bounds.getX() > notesArea.getRight())
            continue;
        paintedNotes.push_back({i, n, bounds, group->second.second, host.noteSelected(i)});
    }
    {
        const auto noteLabel = host.noteLabels();
        Graphics::ScopedSaveState guard(g);
        g.reduceClipRegion(notesArea.toNearestInt());
        for (bool foreground : {false, true})
            for (const auto& item : paintedNotes)
            {
                const auto& n = item.note;
                const auto r = item.bounds;
                if (item.foreground != foreground || !r.intersects(notesArea))
                    continue;
                auto colour = c.groupColour(n.group);
                if (foreground && item.selected)
                    colour = c.accent;
                const float alpha = foreground ? (n.editable ? .82f : .3f) : .16f;
                g.setColour(colour.withAlpha(alpha));
                g.fillRoundedRectangle(r, 2);
                if (foreground && showPitchLabel(row, r.getWidth()))
                {
                    String text;
                    label(g, noteLabel(item.index, text) ? text : pitchName(n.pitch), r.reduced(4, 0),
                          jmin(12.f, row - 1), c.background);
                }
            }
        if (auto* marquee = std::get_if<gesture::Marquee>(&gesture))
        {
            g.setColour(c.accent.withAlpha(.12f));
            g.fillRect(marquee->box);
            g.setColour(c.accent);
            g.drawRect(marquee->box, 1);
        }
    }
    const int cc = host.controller();
    if (showVelocity)
        label(g, String::fromUTF8("力度"), {2, velocityArea.getY() - 20, 55, 18}, 11, c.muted);
    if (showCC)
        label(g, cc < 0 ? String::fromUTF8("CC") : "CC" + String(cc),
              {2, ccArea.getY() - 22, 56, 18}, 12, c.accent);
    if (showVelocity)
    {
        Graphics::ScopedSaveState guard(g);
        g.reduceClipRegion(velocityArea.toNearestInt());
        for (bool foreground : {false, true})
            for (const auto& item : paintedNotes)
            {
                if (item.foreground != foreground)
                    continue;
                const auto xx = timeToX(item.note.start);
                if (xx + 3 < velocityArea.getX() || xx > velocityArea.getRight())
                    continue;
                const auto hh = velocityArea.getHeight() * item.note.velocity / 127.f;
                const auto colour = foreground && item.selected ? c.accent : c.velocity;
                g.setColour(colour.withAlpha(foreground ? 1.f : .16f));
                g.fillRect(xx, velocityArea.getBottom() - hh, 3.f, hh);
            }
    }
    if (showCC)
        for (int value : {0, 64, 127})
        {
            auto yy = ccArea.getBottom() - value * ccArea.getHeight() / 127.f;
            // Keep the 0 and 127 labels inside the lane, clear of the lane header.
            const float labelY = jlimit(ccArea.getY(), ccArea.getBottom() - 14, yy - 7);
            label(g, String(value), {10, labelY, 42, 14}, 10, c.muted);
            g.setColour(c.line);
            g.drawHorizontalLine(roundToInt(yy), ccArea.getX(), ccArea.getRight());
        }
    if (showCC)
    {
        Graphics::ScopedSaveState guard(g);
        g.reduceClipRegion(ccArea.toNearestInt());
        const auto points = host.controllerLaneRange(left, left + span);
        for (std::size_t i = 0; i < points.size(); ++i)
        {
            auto p = points[i];
            float xx = timeToX(p.time),
                  yy = ccArea.getBottom() - p.value * ccArea.getHeight() / 127.f;
            float end = i + 1 < points.size() ? timeToX(points[i + 1].time) : ccArea.getRight();
            if (end < ccArea.getX())
                continue;
            if (xx > ccArea.getRight())
                break;
            g.setColour(p.source == 2 ? c.ink : p.source == 1 ? c.accent : c.velocity);
            g.drawLine(xx, yy, end, yy, 1.7f);
            if (i + 1 < points.size())
            {
                float nextY = ccArea.getBottom() - points[i + 1].value * ccArea.getHeight() / 127.f;
                g.drawLine(end, yy, end, nextY, 1.7f);
            }
        }
    }
    for (float yy :
         {showVelocity ? velocityArea.getY() - 10 : -1, showCC ? ccArea.getY() - 12 : -1})
    {
        if (yy < 0)
            continue;
        g.setColour(c.line);
        g.drawHorizontalLine(roundToInt(yy), 60, (float)getWidth());
        g.fillRoundedRectangle(getWidth() * .5f - 14, yy - 1, 28, 3, 1);
    }
    const PaintedNote* hoveredNote = nullptr;
    if (notesArea.contains(hover))
        for (bool foreground : {true, false})
        {
            for (auto it = paintedNotes.rbegin(); it != paintedNotes.rend(); ++it)
                if (it->foreground == foreground && coreRect(it->bounds).contains({hover.x, hover.y}))
                {
                    hoveredNote = &*it;
                    break;
                }
            if (hoveredNote)
                break;
        }
    if (notesArea.contains(hover) && !host.busy())
    {
        Graphics::ScopedSaveState guard(g);
        g.reduceClipRegion(notesArea.toNearestInt());
        if (hoveredNote)
        {
            g.setColour(c.ink);
            g.drawRoundedRectangle(hoveredNote->bounds, 2, 1.2f);
        }
        else if (isPaintMode())
        {
            ViewNote ghost;
            ghost.start =
                host.roundTime(snapTime(xToTime(hover.x), snapPolicy(hoverFree, Rounding::down)));
            ghost.pitch = yToPitch(hover.y);
            ghost.length = lastLength;
            g.setColour(c.accent.withAlpha(.2f));
            g.fillRoundedRectangle(noteRect(ghost), 2);
        }
    }
    String feedback;
    if (ccArea.contains(hover))
        feedback =
            "CC" + String(cc) + String::fromUTF8(" · 通道 ") + String(host.channel() + 1) + " · " +
            String(jlimit(0, 127,
                          roundToInt((ccArea.getBottom() - hover.y) * 127 / ccArea.getHeight())));
    else if (velocityArea.contains(hover))
        feedback = String::fromUTF8("力度 ") +
                   String(jlimit(1, 127,
                                 roundToInt((velocityArea.getBottom() - hover.y) * 127 /
                                            velocityArea.getHeight())));
    else if (notesArea.contains(hover))
    {
        if (hoveredNote)
        {
            const auto& n = hoveredNote->note;
            feedback = pitchName(n.pitch) + String::fromUTF8(" · 力度 ") + String(n.velocity) +
                       String::fromUTF8(" · 长度 ") + String(n.length / ppq, 2) +
                       String::fromUTF8(" 拍");
        }
        else
            feedback = pitchName(yToPitch(hover.y));
    }
    if (feedback.isNotEmpty())
    {
        Rectangle<float> r{jmax(60.f, (float)getWidth() - 285), 62, 280, 18};
        g.setColour(c.background);
        g.fillRect(r);
        label(g, feedback, r.reduced(4, 0), 11, c.ink);
    }
    if (!host.hasDocument())
        label(g, String::fromUTF8("打开或拖入 MIDI 即可编辑；FLP 补全可选。"),
              notesArea.reduced(20), 14, c.muted);
    if (host.hasDocument())
    {
        float xx = timeToX(host.playheadTime());
        for (auto a : {notesArea, velocityArea, ccArea, rulerArea})
            if (xx >= a.getX() && xx <= a.getRight())
            {
                g.setColour(c.accent);
                g.drawVerticalLine(roundToInt(xx), a.getY(), a.getBottom());
            }
        double loopStart = 0, loopEnd = 0;
        if (host.loopRange(loopStart, loopEnd))
        {
            auto a = timeToX(loopStart), b = timeToX(loopEnd);
            g.setColour(c.accent.withAlpha(.2f));
            g.fillRect(Rectangle<float>(a, rulerArea.getY(), b - a, rulerArea.getHeight())
                           .getIntersection(rulerArea));
        }
    }
    if (showCC && cc < 0)
        label(g, String::fromUTF8("添加 CC 后即可绘制"), ccArea.reduced(8), 11, c.muted);
    undoButton.setEnabled(host.canUndo() && !host.busy());
    redoButton.setEnabled(host.canRedo() && !host.busy());
}
}
