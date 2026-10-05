#pragma once
#include <juce_graphics/juce_graphics.h>
#include <PianoRollCore/Interaction.h>
#include <PianoRollCore/Navigation.h>
#include <map>
#include <variant>
#include <vector>

// Mouse gesture state of PianoRollView. At most one gesture is active between
// mouse-down and mouse-up, and each tool keeps only its own fields.
namespace pianoroll::gesture
{
// Middle-button pan / zoom.
struct Pan
{
    NavigationDrag drag;
};
// Click to seek, drag to set the loop.
struct Ruler
{
    RulerGesture ruler;
};
// Dragging the boundary above the velocity lane or above the CC lane.
struct LaneSplit
{
    bool aboveVelocity = true;
};
// Ctrl+Alt drag of selected notes out of the plugin.
struct ExportNotes
{
};
// Rectangle selection; Shift adds to the selection held at mouse-down.
struct Marquee
{
    bool add = false;
    juce::Rectangle<float> box;
};
// Right-button erase along the mouse path.
struct Erase
{
    juce::Point<float> last;
};
// Move, resize or copy existing notes, or place a new note with the pencil.
struct NoteDrag
{
    // draw: Shift+pencil keeps changing the new note's length while dragging.
    enum class Edit
    {
        move,
        resize,
        draw
    };
    Edit edit = Edit::move;
    int grab = -1;
    bool cloning = false, cloned = false, creation = false;
};
// Velocity lane stroke; Ctrl adjusts relative to the nearest note. When
// `limited` is set only notes whose index is marked in `targets` change.
struct Velocity
{
    bool limited = false, relative = false;
    std::vector<bool> targets;
    int anchorVelocity = 100;
    juce::Point<float> last;
};
// Controller lane stroke; the right button erases.
struct ControllerStroke
{
    bool erase = false;
    double start = 0, end = 0;
    std::map<double, int> points;
    juce::Point<float> last;
};
using State = std::variant<std::monostate, Pan, Ruler, LaneSplit, ExportNotes, Marquee, Erase,
                           NoteDrag, Velocity, ControllerStroke>;
}
