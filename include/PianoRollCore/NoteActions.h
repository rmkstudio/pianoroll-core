#pragma once
#include "Core.h"
#include <string>

namespace pianoroll {
// Adapters retain their own note IDs and untouched MIDI fields. All times use
// the adapter's units (beats or original ticks); only the adapter rounds ticks.
struct ActionNote {
    double start = 0, end = 1;
    int pitch = 60, velocity = 100, source = 0;
    std::size_t originalIndex = noIndex;
    bool selected = false, editable = true, keySwitch = false;
    // A generated note inherits other fields from originalIndex, but needs a new ID.
    bool generated = false;
};
struct ActionRange { bool enabled = false; double start = 0, end = 0; };
struct ActionRules {
    double minimumTime = 0, maximumTime = 1.0e12, minimumLength = 1.0e-9;
    int minimumPitch = 0, maximumPitch = 127;
    std::size_t maximumNotes = 200000;
    bool includeKeySwitches = false;
    // Allows existing overlaps. Rejects newly introduced overlaps at the same
    // source/pitch; disabled by default since overlap is musically intentional.
    bool rejectNewOverlaps = false;
};
struct NoteActionResult {
    std::vector<ActionNote> notes;
    std::string error;
    bool changed = false;
    explicit operator bool() const noexcept { return error.empty(); }
};
enum class GridDivision { straight, triplet, quintuplet };
struct ActionGrid {
    double step = 0.25, origin = 0, swing = 0;
    GridDivision division = GridDivision::straight;
};
double actionGridStep(const ActionGrid&) noexcept;
// NaN means invalid parameters. Swing delays alternate grid points by step*swing.
double nearestActionGrid(double time, const ActionGrid&) noexcept;
struct QuantizeOptions {
    ActionGrid grid;
    bool starts = true, ends = false;
    double startStrength = 1, endStrength = 1;
    double captureRange = std::numeric_limits<double>::infinity();
    ActionRange range;
};
NoteActionResult quantizeNotes(const std::vector<ActionNote>&, const QuantizeOptions&,
                               const ActionRules& = {});
struct OriginalTiming { std::size_t originalIndex = noIndex; double start = 0, end = 0; };
NoteActionResult restoreNoteTiming(const std::vector<ActionNote>&,
                                 const std::vector<OriginalTiming>&, const ActionRules& = {});
enum class MirrorDirection { pitch, time, both };
enum class MirrorPivot { first, middle, last, custom };
struct MirrorOptions {
    MirrorDirection direction = MirrorDirection::pitch;
    MirrorPivot pitchPivot = MirrorPivot::middle, timePivot = MirrorPivot::middle;
    double customPitch = 60, customTime = 0;
};
NoteActionResult mirrorNotes(const std::vector<ActionNote>&, const MirrorOptions& = {},
                            const ActionRules& = {});
NoteActionResult setNoteLength(const std::vector<ActionNote>&, double length,
                              const ActionRules& = {});
// Aligns selected note ends. If toLatestEnd, target is computed from selection.
NoteActionResult alignNoteEnds(const std::vector<ActionNote>&, double target,
                              bool toLatestEnd = true, const ActionRules& = {});
// Changes one boundary and the touching selected note(s) at the same source.
// Starts/ends elsewhere are untouched; all participating notes must stay valid.
NoteActionResult moveLinkedBoundary(const std::vector<ActionNote>&, std::size_t noteIndex,
                                   bool endBoundary, double newTime, double tolerance,
                                   const ActionRules& = {});
NoteActionResult scaleNoteTime(const std::vector<ActionNote>&, double factor,
                              double anchor, const ActionRules& = {});
NoteActionResult transposeNotes(const std::vector<ActionNote>&, int semitones,
                               const ActionRules& = {});
NoteActionResult changeNoteVelocity(const std::vector<ActionNote>&, int delta,
                                   const ActionRules& = {});
enum class NoteSelectionKind { highest, lowest, pitchRange, velocityRange,
                               lengthRange, everyNth, beatPosition };
struct NoteSelectionOptions {
    NoteSelectionKind kind = NoteSelectionKind::highest;
    bool withinSelection = false;
    int minimum = 0, maximum = 127;
    double minimumLength = 0, maximumLength = 1.0e12;
    std::size_t every = 2, offset = 0;
    double period = 1, position = 0, origin = 0, tolerance = 1.0e-6;
    ActionRange range;
};
struct NoteSelectionResult {
    std::vector<std::size_t> indices;
    std::string error;
    explicit operator bool() const noexcept { return error.empty(); }
};
// Highest/lowest means a note is an extreme of the sounding eligible texture
// for at least one positive-length interval. Equal pitches all qualify.
NoteSelectionResult selectNotes(const std::vector<ActionNote>&, const NoteSelectionOptions&,
                                const ActionRules& = {});
NoteActionResult splitNotes(const std::vector<ActionNote>&, const ActionGrid&,
                           const ActionRules& = {});
// Pointer-driven split at an exact time. The selected editable note is split
// into two notes while preserving velocity and note metadata.
NoteActionResult splitNoteAt(const std::vector<ActionNote>&, std::size_t noteIndex,
                             double time, const ActionRules& = {});
struct GlueOptions {
    double tolerance = 1.0e-6;
    bool selectedOnly = true;
};
// Merges touching/overlapping notes with matching source, pitch and keyswitch
// semantics. The first note's metadata and velocity are retained.
NoteActionResult glueNotes(const std::vector<ActionNote>&, const GlueOptions& = {},
                           const ActionRules& = {});
struct RepeatOptions {
    // Copies the selected phrase at positive period offsets into [start,end).
    // Originals remain unchanged. period=0 uses the selected phrase's extent.
    double start = 0, end = 0, period = 0;
    bool clipAtEnd = true;
};
NoteActionResult repeatNotes(const std::vector<ActionNote>&, const RepeatOptions&,
                            const ActionRules& = {});
struct StrumOptions {
    double spacing = 0.025, onsetTolerance = 1.0e-6;
    bool descending = false, preserveEnds = false;
};
NoteActionResult strumNotes(const std::vector<ActionNote>&, const StrumOptions& = {},
                           const ActionRules& = {});
} // namespace pianoroll
