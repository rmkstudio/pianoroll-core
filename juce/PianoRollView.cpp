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
    const int g = 6, ch = 28;
    int x = 0;
    auto take = [&](int width)
    {
        R q{x, 0, width, ch};
        x += width + g;
        return q;
    };
    r.undo = take(42);
    r.redo = take(42);
    x += 4;
    r.tool = take(84);
    r.snap = take(100);
    x += 4;
    r.fitSelection = take(48);
    r.actions = take(58);
    r.transform = take(58);
    r.view = take(78);
    r.fit = {w - 90, 0, 90, ch};
    const int trackWidth = jmax(120, w - 354), y = 36;
    r.editTrack = {0, y, trackWidth, ch};
    r.channel = {trackWidth + g, y, 84, ch};
    r.allTracks = {trackWidth + 96, y, 50, ch};
    r.controller = {w - 198, y, 102, ch};
    r.drag = {w - 90, y, 90, ch};
    const int start = 92, bottom = h - 50;
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
        r.cc = {58, start + nh + vh + 24, w - 59, jmax(24, cch - 24)};
    r.scroll = {58, h - 43, w - 59, 9};
    r.play = {0, h - 29, 52, 27};
    r.stop = {58, h - 29, 52, 27};
    r.home = {116, h - 29, 65, 27};
    r.follow = {188, h - 29, 66, 27};
    r.clock = {258, h - 29, w - 258, 27};
    return r;
}

PianoRollView::PianoRollView(PianoRollHost& owner) : host(owner)
{
    for (auto* b : {&undoButton, &redoButton, &fitSelectionButton, &actionsButton, &transformButton,
                    &viewButton, &playButton, &stopButton, &homeButton})
        b->getProperties().set("quiet", true);
    makeCursors(colours, pencil, copyCursor);
    addAndMakeVisible(toolSelect);
    toolSelect.setName("Edit tool");
    toolSelect.addItem(String::fromUTF8("1  画笔"), 1);
    toolSelect.addItem(String::fromUTF8("2  选择"), 2);
    toolSelect.setSelectedId(2);
    toolSelect.setTooltip(String::fromUTF8("1 画笔 · 2 选择 · Alt 临时取消吸附"));
    for (auto* b :
         {&undoButton, &redoButton, &playButton, &stopButton, &homeButton, &fitSelectionButton})
    {
        addAndMakeVisible(*b);
        b->setWantsKeyboardFocus(false);
    }
    addAndMakeVisible(snapSelect);
    snapSelect.addItemList({String::fromUTF8("不吸附"), String::fromUTF8("1/4 音符"),
                            String::fromUTF8("1/8 音符"), String::fromUTF8("1/16 音符"),
                            String::fromUTF8("1/32 音符"), String::fromUTF8("八分三连"),
                            String::fromUTF8("十六分三连")},
                           1);
    snapSelect.setSelectedId(4);
    snapSelect.setTooltip(String::fromUTF8("Alt 拖动临时取消吸附"));
    addAndMakeVisible(followButton);
    followButton.setToggleState(true, dontSendNotification);
    followButton.setWantsKeyboardFocus(false);
    followButton.onClick = [this]
    {
        follow.setEnabled(followButton.getToggleState());
    };
    addAndMakeVisible(transportLabel);
    transportLabel.setFont(colours.font(13));
    transportLabel.setColour(Label::textColourId, colours.muted);
    playButton.setName("Play / pause");
    stopButton.setName("Stop");
    homeButton.setName("Home");
    followButton.setName("Follow playhead");
    playButton.setTooltip(String::fromUTF8("空格／Enter：播放或暂停 · 参考钢琴音色"));
    stopButton.setTooltip(String::fromUTF8("停止回定位点，再按回开头"));
    toolSelect.onChange = [this]
    {
        updateCursor(hover, ModifierKeys::getCurrentModifiersRealtime());
        repaint();
    };
    for (auto* b : {&actionsButton, &transformButton})
    {
        addAndMakeVisible(*b);
        b->setWantsKeyboardFocus(false);
    }
    actionsButton.setName("Actions / commands");
    actionsButton.onClick = [this]
    {
        host.invokeCommand("commands.search");
    };
    transformButton.setName("Value transform");
    transformButton.onClick = [this]
    {
        host.invokeCommand(host.controller() >= 0 ? "automation.transformBox"
                                                  : "velocity.transformBox");
    };
    for (auto [button, id] : std::initializer_list<std::pair<Button*, const char*>>{
             {&undoButton, "edit.undo"},
             {&redoButton, "edit.redo"},
             {&playButton, "transport.playPause"},
             {&stopButton, "transport.stop"},
             {&homeButton, "transport.home"},
             {&fitSelectionButton, "view.fitSelection"}})
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
    addAndMakeVisible(viewButton);
    viewButton.setName("menu");
    viewButton.setWantsKeyboardFocus(false);
    viewButton.onClick = [this]
    {
        host.showViewMenu(viewButton);
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
    return {&toolSelect, &snapSelect};
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
    return {free ? 1
                 : jmax(1.0, host.unitsPerQuarter() *
                                 beats[jlimit(0, 6, snapSelect.getSelectedId() - 1)]),
            rounding, 0, 0};
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
    return {jmax(1., ppq / 16.), jmax(ppq * 1024, total * 2), jmax(total + ppq * 4, left + span),
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
    toolSelect.setSelectedId(paint ? 1 : 2, dontSendNotification);
    updateCursor(hover, ModifierKeys::getCurrentModifiersRealtime());
    repaint();
}
void PianoRollView::setTransportState(bool playing, bool canPlay, const String& text,
                                      const String& tooltip)
{
    playButton.setButtonText(playing ? String::fromUTF8("暂停") : String::fromUTF8("播放"));
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
    toolSelect.setBounds(r.tool);
    snapSelect.setBounds(r.snap);
    fitSelectionButton.setBounds(r.fitSelection);
    actionsButton.setBounds(r.actions);
    transformButton.setBounds(r.transform);
    viewButton.setBounds(r.view);
    velocityLaneButton.setVisible(false);
    ccLaneButton.setVisible(false);
    rulerArea = r.ruler.toFloat();
    notesArea = r.notes.toFloat();
    keysArea = r.keys.toFloat();
    velocityArea = r.velocity.toFloat();
    ccArea = r.cc.toFloat();
    hscroll.setBounds(r.scroll);
    playButton.setBounds(r.play);
    stopButton.setBounds(r.stop);
    homeButton.setBounds(r.home);
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
    // Group the existing controls without surrounding each action with a box.
    g.setColour(c.line);
    for (int xx : {toolSelect.getX() - 7, fitSelectionButton.getX() - 7})
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
    for (auto area : {notesArea, velocityArea, ccArea, rulerArea})
    {
        Graphics::ScopedSaveState guard(g);
        g.reduceClipRegion(area.toNearestInt());
        for (auto mark : marks)
        {
            auto xx = timeToX(mark.time);
            g.setColour(mark.barStart ? c.line : mark.beatStart ? c.beatLine : c.subdivisionLine);
            g.drawVerticalLine(roundToInt(xx), area.getY(), area.getBottom());
            if (area == rulerArea && mark.barStart)
            {
                String name((int64)mark.bar);
                if (mark.signatureChange)
                {
                    auto pos = meter.positionAt(mark.time);
                    name += "  " + String(pos.numerator) + "/" + String(pos.denominator);
                }
                label(g, name, {xx + 4, area.getY(), 80, 18}, 11, c.muted);
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
    const auto count = host.noteCount();
    {
        const auto noteLabel = host.noteLabels();
        Graphics::ScopedSaveState guard(g);
        g.reduceClipRegion(notesArea.toNearestInt());
        for (bool foreground : {false, true})
            for (std::size_t i = 0; i < count; ++i)
            {
                const auto n = host.note(i);
                if (displayed(n) && inScope(n) == foreground)
                {
                    auto r = noteRect(n);
                    if (!r.intersects(notesArea))
                        continue;
                    auto colour = c.groupColour(n.group);
                    if (foreground && host.noteSelected(i))
                        colour = c.accent;
                    const float alpha = foreground ? (n.editable ? .82f : .3f) : .16f;
                    g.setColour(colour.withAlpha(alpha));
                    g.fillRoundedRectangle(r, 2);
                    if (foreground && showPitchLabel(row, r.getWidth()))
                    {
                        String text;
                        label(g, noteLabel(i, text) ? text : pitchName(n.pitch), r.reduced(4, 0),
                              jmin(12.f, row - 1), c.background);
                    }
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
    {
        Graphics::ScopedSaveState guard(g);
        g.reduceClipRegion(velocityArea.toNearestInt());
        for (bool foreground : {false, true})
            for (std::size_t i = 0; i < count; ++i)
            {
                const auto n = host.note(i);
                if (displayed(n) && inScope(n) == foreground)
                {
                    auto xx = timeToX(n.start);
                    auto hh = velocityArea.getHeight() * n.velocity / 127.f;
                    auto colour = foreground && host.noteSelected(i) ? c.accent : c.velocity;
                    g.setColour(colour.withAlpha(foreground ? 1.f : .16f));
                    g.fillRect(xx, velocityArea.getBottom() - hh, 3.f, hh);
                }
            }
    }
    if (showCC)
        for (int value : {0, 64, 127})
        {
            auto yy = ccArea.getBottom() - value * ccArea.getHeight() / 127.f;
            label(g, String(value), {10, yy - 7, 42, 14}, 10, c.muted);
            g.setColour(c.line);
            g.drawHorizontalLine(roundToInt(yy), ccArea.getX(), ccArea.getRight());
        }
    {
        Graphics::ScopedSaveState guard(g);
        g.reduceClipRegion(ccArea.toNearestInt());
        const auto points = host.controllerLane();
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
    if (notesArea.contains(hover) && !host.busy())
    {
        Graphics::ScopedSaveState guard(g);
        g.reduceClipRegion(notesArea.toNearestInt());
        int index = hit(hover, true);
        if (index >= 0)
        {
            g.setColour(c.ink);
            g.drawRoundedRectangle(noteRect(host.note((std::size_t)index)), 2, 1.2f);
        }
        else if (toolSelect.getSelectedId() == 1)
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
        int i = hit(hover, true);
        if (i >= 0)
        {
            const auto n = host.note((std::size_t)i);
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
