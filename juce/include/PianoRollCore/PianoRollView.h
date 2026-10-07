#pragma once
// Shared piano-roll view: drawing, mouse gestures, cursors and the generic
// toolbar. It reads notes, lanes and transport through PianoRollHost and
// reports every edit back through it; the product keeps the document, undo
// history and the commit gate. See juce/README.md.
#include <juce_gui_basics/juce_gui_basics.h>
#include <PianoRollCore/Core.h>
#include <PianoRollCore/Interaction.h>
#include <PianoRollCore/Meter.h>
#include <PianoRollCore/Navigation.h>
#include <PianoRollCore/NoteActions.h>
#include <PianoRollCore/ViewGestures.h>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace pianoroll
{
// One note as the view sees it. Times use the product's unit (ticks or beats);
// `group` is the product's track or voice.
struct ViewNote
{
    double start = 0, length = 1;
    int pitch = 60, velocity = 100, group = 0, channel = 0;
    bool editable = true;
};
// A step of the controller lane. `source` selects the palette colour:
// 0 original, 1 recovered, 2 edited.
struct LanePoint
{
    double time = 0;
    int value = 0, source = 0;
};
struct ViewPalette
{
    juce::Colour background{0xff1b1d1f}, panel{0xff222527}, line{0xff393d3f}, ink{0xffe6e5e0},
        muted{0xff979d9a}, accent{0xffdc9a68}, velocity{0xff80a7b1};
    juce::Colour scaleRow{0xff263a39}, blackRow{0xff202325}, whiteRow{0xff272a2c},
        beatLine{0xff303436}, subdivisionLine{0xff292d2f}, blackKey{0xff191c1e},
        whiteKey{0xff424649};
    std::function<juce::Font(float)> font = [](float size)
    {
        return juce::Font(size);
    };
    std::function<juce::Colour(int group)> groupColour = [](int group)
    {
        return juce::Colour::fromHSV(std::fmod(group * .173f + .48f, 1.f), .35f, .78f, 1.f);
    };
};
// Pixel rectangles of every view region and toolbar slot. Product slots
// (group selector, channel, All, controller, fit, export) are placed by the host.
struct ViewLayout
{
    using Rect = juce::Rectangle<int>;
    // One toolbar row: transport and clock, history, tools, snap and the edit menu.
    Rect play, stop, clock, undo, redo, pencil, pointer, snap, edit, follow;
    // Product slots: fit and expand sit at the toolbar end, the rest on the context row
    // or in a lane header; a product may leave any of them empty.
    Rect fit, expand, editTrack, channel, allTracks, controller, drag;
    Rect ruler, notes, keys, velocity, cc, scroll;
    // Optional explicit lane grab areas; custom layouts keep their own header geometry.
    Rect velocityDivider, ccDivider;
};
ViewLayout defaultViewLayout(int width, int height, bool velocity, bool cc, float noteFraction,
                             float velocityFraction);

class PianoRollHost
{
  public:
    virtual ~PianoRollHost() = default;

    // Document and timing.
    virtual bool hasDocument() const = 0;
    virtual bool busy() const = 0;
    virtual int unitsPerQuarter() const = 0;
    virtual double documentLength() const = 0;
    virtual double maximumTime() const = 0;
    // The product's final time rounding (Recover: JUCE roundToInt to ticks).
    virtual double roundTime(double) const = 0;
    // Tick hosts retain a one-tick minimum; fractional-time hosts override this.
    virtual double minimumNoteLength() const { return 1; }
    // Free time resolution is independent of the shortest legal note (zero is continuous).
    virtual double freeTimeStep() const { return minimumNoteLength(); }
    virtual NavigationLimits navigationLimits(NavigationLimits defaults) const { return defaults; }
    virtual void viewChanged(ViewState) {}
    // Real document indices, in paint order. The default keeps unindexed hosts compatible.
    virtual std::vector<std::size_t> noteIndicesInRange(double, double) const
    {
        std::vector<std::size_t> indices;
        indices.reserve(noteCount());
        for (std::size_t i = 0; i < noteCount(); ++i) indices.push_back(i);
        return indices;
    }

    // Notes, by index into the product's current draft.
    virtual std::size_t noteCount() const = 0;
    virtual ViewNote note(std::size_t) const = 0;
    virtual bool noteSelected(std::size_t) const = 0;
    virtual bool hasSelection() const = 0;
    // Per-pass lookups: the product builds its index once per paint or drag step.
    // noteLabels() returns true with the text to draw instead of the pitch name.
    virtual std::function<bool(std::size_t, juce::String&)> noteLabels() const = 0;
    virtual std::function<bool(std::size_t)> keySwitches() const = 0;
    virtual bool groupAlive(int group) const = 0;
    virtual bool groupInScope(int group) const = 0;
    virtual bool pitchesFolded() const = 0;
    virtual bool rowHighlighted(int pitch) const = 0;
    // Geometry and skin policies never change note identity or own a document.
    virtual juce::Rectangle<float> noteBounds(const ViewNote& n, const Viewport& v, float pitchY) const
    {
        const auto r = v.noteRect(n.start, n.start + n.length, n.pitch,
                                  std::max(1.0, v.rowHeight - 1), 3);
        return {(float)r.x, pitchY, (float)r.width, (float)r.height};
    }
    virtual juce::Point<float> noteHitExpansion() const { return {}; }
    virtual HitPolicy noteHitPolicy() const { return HitPolicy::lastPainted; }
    virtual bool preferForegroundHits() const { return true; }
    virtual double linkedBoundaryTolerance() const { return .001; }
    virtual bool relativeVelocityUsesScope() const { return false; }
    virtual bool noteDrawEndsAtPointer() const { return false; }
    virtual void paintOverlay(juce::Graphics&) const {}
    virtual int linkedBoundarySource(std::size_t index) const
    { const auto n = note(index); return n.group * 16 + n.channel; }
    virtual bool paintPitchRows(juce::Graphics&, juce::Rectangle<float>, juce::Rectangle<float>,
                                float, const std::function<float(int)>&) const { return false; }
    virtual bool paintNote(juce::Graphics&, std::size_t, const ViewNote&, juce::Rectangle<float>,
                           bool, bool) const { return false; }
    virtual bool paintVelocityNote(juce::Graphics&, std::size_t, const ViewNote&,
                                   juce::Rectangle<float>, float, bool, bool) const { return false; }
    virtual bool showCanvasFeedback() const { return true; }
    virtual juce::String emptyDocumentMessage() const
    { return juce::String::fromUTF8("打开或拖入 MIDI 即可编辑；FLP 补全可选。"); }
    virtual void pointerMoved(juce::Point<float>) {}
    virtual bool noteGestureDropped(juce::Point<float>) { return false; }
    virtual bool resizeLane(bool, float) { return false; }

    // Lanes and transport.
    virtual int controller() const = 0;
    virtual int channel() const = 0;
    virtual std::vector<LanePoint> controllerLane() const = 0;
    // Include the preceding step and the first following point so clipping preserves edges.
    // Hosts with indexed lanes can avoid materializing the complete lane on every paint.
    virtual std::vector<LanePoint> controllerLaneRange(double, double) const
    {
        return controllerLane();
    }
    virtual double playheadTime() const = 0;
    // Returns false when no loop is set.
    virtual bool loopRange(double& start, double& end) const = 0;
    virtual void seek(double time) = 0;
    virtual void setLoop(double start, double end) = 0;
    virtual void clearLoop() = 0;
    virtual bool canUndo() const = 0;
    virtual bool canRedo() const = 0;

    // Gesture lifecycle. gestureStarted() runs at every mouse-down; beginEdit()
    // captures the product's before-image; editChanged() marks the draft dirty;
    // commitEdit() publishes at most once; abandonEdit() drops an unused image.
    virtual void gestureStarted(double pasteTime) = 0;
    virtual void beginEdit() = 0;
    virtual void abandonEdit() = 0;
    virtual void editChanged() = 0;
    virtual void commitEdit() = 0;
    // The view re-read its before-image (after grabbing, creating or copying).
    virtual void baselineCaptured() = 0;
    // The current selection becomes the gesture's start selection.
    virtual void startSelectionCaptured() = 0;
    virtual bool startSelected(std::size_t) const = 0;
    virtual void focusGroupOf(std::size_t) = 0;

    // Draft edits.
    virtual void moveNote(std::size_t, double start, int pitch) = 0;
    virtual void setNoteLength(std::size_t, double length) = 0;
    virtual void setNoteTiming(std::size_t, double start, double length) = 0;
    virtual void setNoteVelocity(std::size_t, int velocity) = 0;
    virtual void eraseNote(std::size_t) = 0;
    virtual bool canCreateNote() const = 0;
    // Adds a note to the write group and makes it the only selected note.
    virtual std::size_t createNote(double start, int pitch, double length, int velocity) = 0;
    // Appends a copy of before-image note `source` and selects it.
    virtual void appendBaselineCopy(std::size_t source) = 0;
    virtual void clearSelection() = 0;
    virtual void selectNote(std::size_t) = 0;
    virtual void deselectNote(std::size_t) = 0;
    // Selection becomes the start selection (keep) or empty.
    virtual void restoreStartSelection(bool keep) = 0;
    virtual bool canDrawController() const = 0;
    virtual void beginControllerStroke(double time, bool erase) = 0;
    virtual bool controllerStrokeOpen() const = 0;
    virtual void updateControllerStroke(double start, double end,
                                        const std::map<double, int>& points, bool erase) = 0;
    // After a move: product follow rules. False when it rejected the move and
    // ended the gesture.
    virtual bool followMovedNotes(bool copied) = 0;
    // Resizing a note end also moves the adjacent note's start.
    virtual bool linkedNoteEnds() const = 0;

    // Product actions.
    virtual bool auditionNote(int pitch, int velocity) = 0;
    virtual void stopAudition() = 0;
    virtual void pianoKeyDown(int pitch) = 0;
    virtual bool pianoKeyDragged(int pitch) { return auditionNote(pitch, 100); }
    // True when the release was consumed (step input).
    virtual bool pianoKeyUp(int pitch) = 0;
    virtual void exportSelectedNotes() = 0;
    virtual void laneLayoutChanged() = 0;
    virtual void reportMessage(const juce::String&) = 0;
    virtual void invokeCommand(const std::string& id) = 0;
    // Product entries appended to the edit menu (view options, audition, solo...).
    virtual void addEditMenuItems(juce::PopupMenu&) = 0;
    virtual ViewLayout viewLayout(int width, int height, bool velocity, bool cc, float noteFraction,
                                  float velocityFraction) const
    {
        return defaultViewLayout(width, height, velocity, cc, noteFraction, velocityFraction);
    }
    // Called first in resized(); product overlays that depend on geometry close here.
    virtual void viewWillResize() = 0;
    // Called after the view placed its own controls.
    virtual void placeProductControls(const ViewLayout&) = 0;
};

class PianoRollView : public juce::Component, private juce::ScrollBar::Listener
{
  public:
    explicit PianoRollView(PianoRollHost&);
    ~PianoRollView() override;
    void setPalette(ViewPalette);
    const ViewPalette& palette() const
    {
        return colours;
    }

    // View window.
    void fit();
    void fitSelection();
    void clampView();
    void showTimeAround(double time);
    void setTimeWindow(double start, double span);
    double timeOffset() const
    {
        return left;
    }
    double timeSpan() const
    {
        return span;
    }
    float pitchTop() const
    {
        return topPitch;
    }
    ViewState viewState() const { return navigationView(); }
    void setViewState(ViewState state) { applyNavigation(state); }
    void setSnapPolicy(SnapPolicy policy) { suppliedSnap = policy; hasSuppliedSnap = true; }
    float rowHeight() const { return row; }
    float timeToX(double time) const;
    double xToTime(float x) const;
    float pitchToY(int pitch) const;
    int yToPitch(float y) const;
    juce::Rectangle<float> noteRect(const ViewNote&) const;
    juce::Rectangle<float> noteArea() const
    {
        return notesArea;
    }
    juce::Rectangle<float> velocityLaneBounds() const
    {
        return velocityArea;
    }
    juce::Rectangle<float> controllerLaneBounds() const
    {
        return ccArea;
    }
    SnapPolicy snapPolicy(bool free = false, Rounding rounding = Rounding::nearest) const;
    double quantize(double time, bool free = false) const;
    void setPitchRows(std::vector<int> rows, bool resetFoldedView);
    void setMeter(std::vector<MeterChange>, int unitsPerQuarter);
    const MeterMap& meterMap() const
    {
        return meter;
    }

    // Lanes.
    bool velocityLaneVisible() const
    {
        return showVelocity;
    }
    bool controllerLaneVisible() const
    {
        return showCC;
    }
    void setLaneLayout(bool velocity, bool controller, float noteFraction, float velocityFraction);
    void showControllerLane();
    float noteLaneFraction() const
    {
        return noteFraction;
    }
    float velocityLaneFraction() const
    {
        return velocityFraction;
    }
    juce::ToggleButton& velocityLaneToggle()
    {
        return velocityLaneButton;
    }
    juce::ToggleButton& controllerLaneToggle()
    {
        return ccLaneButton;
    }

    // Tools, gestures and transport.
    void setPaintMode(bool paint);
    bool isPaintMode() const
    {
        return pencilButton.getToggleState();
    }
    bool gestureActive() const
    {
        return !std::holds_alternative<std::monostate>(gesture);
    }
    // Ends the current gesture without committing (the product already did).
    void endGesture()
    {
        gesture = {};
    }
    void setDefaultNoteLength(double length)
    {
        lastLength = length;
    }
    void noteAuditioned(int pitch)
    {
        lastAudition = pitch;
    }
    void setTransportState(bool playing, bool canPlay, const juce::String& text,
                           const juce::String& tooltip);
    void followPlayback(double playhead);
    void followTransportLocated()
    {
        follow.transportLocated();
    }
    void userNavigated()
    {
        follow.userNavigated();
    }
    bool followEnabled() const
    {
        return follow.enabled();
    }
    void setFollowEnabled(bool);
    std::vector<juce::Component*> keyboardControls();

    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

  protected:
    // First toolbar row; the host may also attach tooltips or names.
    // Buttons carry an "icon" property; a product look-and-feel may draw it instead of the
    // text. "segment" (first/middle/last) marks buttons drawn as one joined group.
    juce::TextButton undoButton{juce::String::fromUTF8("撤销")},
        redoButton{juce::String::fromUTF8("重做")}, pencilButton{juce::String::fromUTF8("画笔")},
        pointerButton{juce::String::fromUTF8("选择")}, editButton{juce::String::fromUTF8("编辑")};
    juce::ComboBox snapSelect;
    juce::TextButton playButton{juce::String::fromUTF8("播放")},
        stopButton{juce::String::fromUTF8("停止")};
    juce::ToggleButton followButton{juce::String::fromUTF8("跟随")},
        velocityLaneButton{juce::String::fromUTF8("力度栏")},
        ccLaneButton{juce::String::fromUTF8("CC 栏")};
    juce::Label transportLabel;
    juce::ScrollBar hscroll{false};

  private:
    struct PaintedNote
    {
        std::size_t index;
        ViewNote note;
        juce::Rectangle<float> bounds;
        bool foreground, selected;
    };
    std::vector<PaintedNote> paintedNotes;
    PianoRollHost& host;
    ViewPalette colours;
    double left = 0, span = 768, total = 768;
    double topPitch = 80, row = 12;
    float noteFraction = .76f, velocityFraction = .12f, laneBottom = 0;
    bool showVelocity = true, showCC = true;
    juce::Rectangle<float> notesArea, keysArea, rulerArea, velocityArea, ccArea;
    juce::Rectangle<float> velocityDivider, ccDivider;
    bool hasSuppliedSnap = false;
    SnapPolicy suppliedSnap;
    std::vector<int> pitchRows;
    MeterMap meter;
    PlayheadFollow follow;
    juce::MouseCursor pencil, copyCursor;
    juce::Point<float> anchor, hover{-1, -1};
    bool hoverFree = false, shiftAtDown = false;
    double lastLength = 96;
    int lastVelocity = 100, lastAudition = -1;
    gesture::State gesture;
    std::vector<ViewNote> before;

    void scrollBarMoved(juce::ScrollBar*, double pos) override
    {
        left = pos;
        host.viewChanged(navigationView());
        repaint();
    }
    Viewport viewport() const;
    ViewState navigationView() const;
    NavigationLimits navigationLimits() const;
    void applyNavigation(ViewState);
    GroupBounds selectedBounds(const std::vector<ViewNote>&) const;
    bool displayed(const ViewNote&) const;
    bool inScope(const ViewNote& n) const
    {
        return host.groupAlive(n.group) && host.groupInScope(n.group);
    }
    int hit(juce::Point<float>, bool allowBackground = false) const;
    void label(juce::Graphics&, const juce::String&, juce::Rectangle<float>, float size,
               juce::Colour) const;
    juce::String pitchName(int pitch) const;
    void audition(int pitch, int velocity);
    void captureBaseline();
    void updateCursor(juce::Point<float>, juce::ModifierKeys);
    void showTool(bool paint);
    void showEditMenu();

    // Gestures, one block per tool (PianoRollViewGestures.cpp).
    void beginPan(const juce::MouseEvent&);
    void beginRuler(const juce::MouseEvent&);
    void finishRuler(gesture::Ruler&, const juce::MouseEvent&);
    void dragLaneSplit(const gesture::LaneSplit&, const juce::MouseEvent&);
    void beginControllerStroke(const juce::MouseEvent&);
    bool beginNoteGesture(const juce::MouseEvent&);
    bool beginVelocityStroke(const juce::MouseEvent&);
    void dragMarquee(gesture::Marquee&, const juce::MouseEvent&);
    void dragNotes(gesture::NoteDrag&, const juce::MouseEvent&);
    void eraseAt(gesture::Erase&, juce::Point<float>);
    void paintVelocity(gesture::Velocity&, juce::Point<float>, bool first = false);
    void paintController(gesture::ControllerStroke&, juce::Point<float>);
};
}
