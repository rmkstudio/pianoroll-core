#include <PianoRollCore/MusicTools.h>
#include <algorithm>
#include <cmath>
#include <set>

namespace pianoroll {
bool ScaleDefinition::contains(int pitch) const noexcept {
    if (pitch < 0 || pitch > 127) return false;
    const int degree = ((pitch - root % 12) % 12 + 12) % 12;
    for (int interval : intervals) if (((interval % 12) + 12) % 12 == degree) return true;
    return false;
}
int PitchLayout::rowForPitch(int pitch) const noexcept {
    auto it = std::lower_bound(pitches.begin(), pitches.end(), pitch);
    return it != pitches.end() && *it == pitch ? static_cast<int>(it - pitches.begin()) : -1;
}
int PitchLayout::pitchForRow(int row) const noexcept {
    if (pitches.empty()) return 60;
    return pitches[static_cast<std::size_t>(std::max(0, std::min(static_cast<int>(pitches.size()) - 1, row)))];
}
PitchLayout makePitchLayout(PitchVisibility mode, const std::vector<int>& used, const ScaleDefinition& scale) {
    PitchLayout result;
    std::set<int> active;
    for (int p : used) if (p >= 0 && p <= 127) active.insert(p);
    for (int p = 0; p <= 127; ++p)
        if (mode == PitchVisibility::all || (mode == PitchVisibility::used && active.count(p)) ||
            (mode == PitchVisibility::scale && scale.contains(p))) result.pitches.push_back(p);
    // An empty file or scale remains editable and visibly distinct from no UI.
    if (result.pitches.empty()) for (int p = 48; p <= 72; ++p) result.pitches.push_back(p);
    return result;
}
const Articulation* ArticulationMap::find(int pitch, int channel) const noexcept {
    const Articulation* wildcard = nullptr;
    for (const auto& item : entries) if (item.pitch == pitch) {
        if (item.channel == channel && channel >= 0) return &item;
        if (item.channel < 0) wildcard = &item;
    }
    return wildcard;
}
bool ArticulationMap::valid() const noexcept {
    if (entries.size() > 2048) return false;
    for (std::size_t i = 0; i < entries.size(); ++i) {
        const auto& e = entries[i];
        if (e.name.empty() || e.name.size() > 128 || e.pitch < 0 || e.pitch > 127 || e.channel < -1 || e.channel > 16) return false;
        for (std::size_t j = 0; j < i; ++j)
            if (entries[j].pitch == e.pitch && entries[j].channel == e.channel) return false;
    }
    return true;
}
void StepInput::setEnabled(bool value) noexcept { if (enabled_ != value) cancelPending(); enabled_ = value; }
bool StepInput::setPosition(double value) noexcept {
    if (!std::isfinite(value) || value < 0 || hasPendingChord()) return false;
    position_ = value; return true;
}
bool StepInput::setLength(double value) noexcept {
    if (!std::isfinite(value) || value <= 0 || hasPendingChord()) return false;
    length_ = value; return true;
}
bool StepInput::noteOn(int pitch, int velocity, int channel) {
    if (!enabled_ || pitch < 0 || pitch > 127 || velocity <= 0 || channel < 0 || channel > 16) return false;
    const auto key = std::make_pair(channel, pitch);
    if (held_.count(key)) return false; // OS key-repeat must not produce extra notes
    held_[key] = true;
    pending_[key] = {pitch, std::min(127, velocity), channel}; return true;
}
StepChord StepInput::noteOff(int pitch, int channel) {
    if (!enabled_ || !held_.erase(std::make_pair(channel, pitch)) || !held_.empty()) return {};
    return finishChord();
}
StepChord StepInput::finishChord() {
    if (!enabled_ || pending_.empty() || !std::isfinite(position_ + length_)) return {};
    StepChord result{position_, length_, {}};
    for (const auto& item : pending_) result.notes.push_back(item.second);
    position_ += length_; cancelPending(); return result;
}
bool StepInput::rest() noexcept {
    if (!enabled_ || hasPendingChord() || !std::isfinite(position_ + length_)) return false;
    position_ += length_; return true;
}
bool StepInput::back() noexcept {
    if (!enabled_ || hasPendingChord()) return false;
    position_ = std::max(0.0, position_ - length_); return true;
}
void StepInput::cancelPending() noexcept { held_.clear(); pending_.clear(); }
} // namespace pianoroll
