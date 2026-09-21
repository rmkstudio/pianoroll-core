#pragma once
#include <map>
#include <string>
#include <vector>

namespace pianoroll {
struct ScaleDefinition {
    int root = 0;
    std::vector<int> intervals{0, 2, 4, 5, 7, 9, 11};
    bool contains(int pitch) const noexcept;
};
enum class PitchVisibility { all, used, scale };
struct PitchLayout {
    std::vector<int> pitches; // low to high; note events never change
    int rowForPitch(int pitch) const noexcept; // -1 when hidden
    int pitchForRow(int row) const noexcept; // clamps to visible rows
};
PitchLayout makePitchLayout(PitchVisibility, const std::vector<int>& usedPitches,
                           const ScaleDefinition& = {});
struct Articulation {
    std::string name;
    int pitch = 0;
    int channel = -1; // -1 all channels; otherwise adapter's original MIDI channel
};
struct ArticulationMap {
    std::vector<Articulation> entries;
    const Articulation* find(int pitch, int channel = -1) const noexcept;
    bool protects(int pitch, int channel = -1) const noexcept { return find(pitch, channel) != nullptr; }
    bool valid() const noexcept;
};

struct StepNote { int pitch = 60, velocity = 100, channel = 0; };
struct StepChord {
    double start = 0, length = 1;
    std::vector<StepNote> notes;
    bool empty() const noexcept { return notes.empty(); }
};
// UI-thread note-entry state. Adapter routes a bounded MIDI-input queue here;
// never access this model from the audio callback. Keys accumulate into one
// chord until all are released. A single published chord is one undo step.
class StepInput {
public:
    void setEnabled(bool) noexcept;
    bool enabled() const noexcept { return enabled_; }
    bool setPosition(double) noexcept;
    double position() const noexcept { return position_; }
    bool setLength(double) noexcept;
    double length() const noexcept { return length_; }
    bool noteOn(int pitch, int velocity = 100, int channel = 0);
    StepChord noteOff(int pitch, int channel = 0);
    StepChord finishChord();
    bool rest() noexcept;
    bool back() noexcept;
    bool hasPendingChord() const noexcept { return !pending_.empty(); }
    void cancelPending() noexcept;
private:
    bool enabled_ = false;
    double position_ = 0, length_ = 1;
    std::map<std::pair<int, int>, StepNote> pending_;
    std::map<std::pair<int, int>, bool> held_;
};
} // namespace pianoroll
