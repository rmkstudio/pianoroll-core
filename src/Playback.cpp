#include <PianoRollCore/Playback.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <map>
#include <utility>

namespace pianoroll {
namespace {
static_assert(std::atomic<const void*>::is_always_lock_free &&
              std::atomic<std::uint64_t>::is_always_lock_free &&
              std::atomic<double>::is_always_lock_free &&
              std::atomic<bool>::is_always_lock_free,
              "Playback requires lock-free pointer, double and 64-bit atomics.");
constexpr double twoPi = 6.283185307179586476925286766559;
int laneIndex(int cc) noexcept { return cc == 7 ? 0 : cc == 11 ? 1 : cc == 10 ? 2 : -1; }
bool finiteTime(double t) noexcept { return std::isfinite(t) && t >= 0; }
int valueAt(const std::vector<PlaybackValue>& lane, double time, int fallback) noexcept
{
    const auto i = std::upper_bound(lane.begin(), lane.end(), time,
        [](double t, const PlaybackValue& p) { return t < p.time; });
    return i == lane.begin() ? fallback : std::prev(i)->value;
}
}

std::shared_ptr<const PlaybackSchedule> makePlaybackSchedule(
    std::vector<PlaybackNote> notes, std::vector<PlaybackController> controls,
    double length, std::string* error)
{
    if (error) error->clear();
    const auto fail = [&](const char* text) -> std::shared_ptr<const PlaybackSchedule> {
        if (error) *error = text;
        return {};
    };
    if (!finiteTime(length)) return fail("Invalid preview duration.");
    for (const auto& n : notes) {
        if (!finiteTime(n.start) || !finiteTime(n.end) || n.end <= n.start ||
            n.pitch < 0 || n.pitch > 127 || n.velocity < 1 || n.velocity > 127 || n.group < 0)
            return fail("Invalid preview note.");
        length = std::max(length, n.end);
    }
    for (const auto& c : controls) {
        if (!finiteTime(c.time) || c.controller < 0 || c.controller > 127 || c.value < 0 || c.value > 127)
            return fail("Invalid preview controller.");
        length = std::max(length, c.time);
    }
    std::stable_sort(notes.begin(), notes.end(), [](const auto& a, const auto& b) { return a.start < b.start; });
    std::stable_sort(controls.begin(), controls.end(), [](const auto& a, const auto& b) { return a.time < b.time; });
    std::map<int, std::vector<PlaybackValue>> pedals;
    std::map<std::pair<int,int>, std::vector<double>> starts;
    std::map<int, PlaybackSource> sources;
    for (const auto& n : notes) starts[{n.source, n.pitch}].push_back(n.start);
    for (const auto& c : controls) {
        if (c.controller == 64) pedals[c.source].push_back({c.time, c.value});
        const int lane = laneIndex(c.controller);
        if (lane >= 0) {
            auto& s = sources[c.source]; s.source = c.source;
            s.lanes[static_cast<std::size_t>(lane)].push_back({c.time, c.value});
        }
    }
    for (auto& n : notes) {
        const auto p = pedals.find(n.source);
        if (p == pedals.end()) continue;
        const auto& lane = p->second;
        auto at = std::upper_bound(lane.begin(), lane.end(), n.end,
            [](double t, const PlaybackValue& v) { return t < v.time; });
        if (at == lane.begin() || std::prev(at)->value < 64) continue;
        double end = length;
        for (; at != lane.end(); ++at) if (at->value < 64) { end = at->time; break; }
        const auto& samePitch = starts[{n.source, n.pitch}];
        const auto next = std::lower_bound(samePitch.begin(), samePitch.end(), n.end);
        if (next != samePitch.end()) end = std::min(end, *next);
        n.end = std::max(n.end, end);
    }
    auto schedule = std::make_shared<PlaybackSchedule>();
    schedule->notes = std::move(notes);
    schedule->controllers = std::move(controls);
    schedule->length = length;
    for (auto& kv : sources) schedule->sources.push_back(std::move(kv.second));
    return schedule;
}

struct PlaybackEngine::Impl {
    struct Control {
        std::shared_ptr<const PlaybackSchedule> schedule;
        std::uint64_t revision = 0;
        std::uint64_t resumeEpoch = 0;
        double cursor = 0, anchor = 0, loopStart = 0, loopEnd = 0;
        int solo = -1;
        bool playing = false, stoppedAtStart = false;
    } desired;
    // A hazard protects the audio reader across complete render blocks. The
    // publisher alone owns/deletes controls and the schedules they keep alive.
    std::vector<std::unique_ptr<Control>> retained;
    std::atomic<const Control*> published{nullptr}, hazard{nullptr};
    std::atomic<std::uint64_t> silenceEpoch{0};
    std::atomic<bool> deviceSuspended{false};
    std::atomic<std::uint64_t> feedbackSequence{0}, feedbackRevision{0};
    std::atomic<double> feedbackCursor{0};
    std::atomic<bool> feedbackPlaying{false};
    struct Feedback { double cursor = 0; bool playing = false; };
    double rate = 44100;
    const PlaybackSchedule* audioSchedule = nullptr;
    std::uint64_t audioRevision = 0;
    double cursor = 0, loopStart = 0, loopEnd = 0;
    int solo = -1;
    bool playing = false;
    std::size_t nextNote = 0, nextControl = 0;
    struct Tone {
        bool active = false;
        int pitch = 0, group = 0, source = 0, volume = 127, expression = 127, pan = 64;
        double phase = 0, step = 0, off = 0;
        float level = 0, env = 0;
    };
    std::array<Tone, 256> tones{};
    std::atomic<std::uint64_t> auditionRequest{0};
    std::uint64_t auditionSerial = 0, audioAuditionSerial = 0;
    double auditionPhase = 0, auditionStep = 0, auditionAge = 0;
    float auditionLevel = 0, auditionEnvelope = 0;
    bool auditionActive = false;

    Feedback feedback() const noexcept {
        if (desired.resumeEpoch != silenceEpoch.load()) return {desired.cursor, false};
        for (int i = 0; i < 3; ++i) {
            const auto before = feedbackSequence.load();
            if (before & 1u) continue;
            const auto revision = feedbackRevision.load();
            const Feedback f{feedbackCursor.load(), feedbackPlaying.load()};
            if (before == feedbackSequence.load() && revision == desired.revision) return f;
        }
        return {desired.cursor, desired.playing};
    }
    void adoptFeedback() {
        const auto f = feedback(); desired.cursor = f.cursor; desired.playing = f.playing;
    }
    double length() const noexcept { return desired.schedule ? desired.schedule->length : 0; }
    double clamp(double seconds) const noexcept {
        return std::isfinite(seconds) ? std::clamp(seconds, 0.0, length()) : 0;
    }
    void publish() {
        ++desired.revision;
        auto state = std::make_unique<Control>(desired);
        const auto* ptr = state.get();
        retained.push_back(std::move(state));
        published.store(ptr);
        // Sequentially consistent hazard/publication ordering is intentional:
        // a reader must recheck publication before dereferencing a new pointer.
        const auto* protectedState = hazard.load();
        retained.erase(std::remove_if(retained.begin(), retained.end(),
            [=](const auto& old) { return old.get() != ptr && old.get() != protectedState; }), retained.end());
    }
    void report() noexcept {
        feedbackSequence.fetch_add(1);
        feedbackCursor.store(cursor);
        feedbackPlaying.store(playing);
        feedbackRevision.store(audioRevision);
        feedbackSequence.fetch_add(1);
    }
    const Control* acquire() noexcept {
        // Bounded handoff: excessive concurrent UI updates may skip one block,
        // but can never turn into waiting for a message-thread lock.
        for (int i = 0; i < 3; ++i) {
            const auto* p = published.load();
            hazard.store(p);
            if (p == published.load()) return p;
        }
        hazard.store(nullptr);
        return nullptr;
    }
    void beginTone(const PlaybackNote& n) noexcept {
        for (auto& tone : tones) if (!tone.active) {
            tone = {};
            tone.active = true; tone.pitch = n.pitch; tone.group = n.group; tone.source = n.source;
            tone.step = twoPi * 440 * std::pow(2.0, (n.pitch - 69) / 12.0) / rate;
            tone.off = n.end; tone.level = n.velocity / 127.0f * .07f;
            if (audioSchedule) {
                const auto& sources = audioSchedule->sources;
                const auto s = std::lower_bound(sources.begin(), sources.end(), n.source,
                    [](const PlaybackSource& p, int id) { return p.source < id; });
                if (s != sources.end() && s->source == n.source) {
                    tone.volume = valueAt(s->lanes[0], cursor, 127);
                    tone.expression = valueAt(s->lanes[1], cursor, 127);
                    tone.pan = valueAt(s->lanes[2], cursor, 64);
                }
            }
            return;
        }
        // Match the original 256-voice reference synth: excess starts are dropped.
    }
    void resetAt(double time) noexcept {
        for (auto& tone : tones) tone = {};
        nextNote = nextControl = 0;
        if (!audioSchedule) return;
        const auto& notes = audioSchedule->notes;
        while (nextNote < notes.size() && notes[nextNote].start < time) {
            const auto& n = notes[nextNote++];
            if (n.end > time && (solo < 0 || n.group == solo)) beginTone(n);
        }
        const auto& controls = audioSchedule->controllers;
        nextControl = static_cast<std::size_t>(std::upper_bound(controls.begin(), controls.end(), time,
            [](double t, const PlaybackController& c) { return t < c.time; }) - controls.begin());
    }
    void renderAudition(float* const* channels, int channelCount, int count) noexcept {
        const auto command = auditionRequest.load();
        if (command == 0) {
            auditionActive = false; audioAuditionSerial = 0; auditionEnvelope = 0; auditionPhase = 0;
            return;
        }
        const auto serial = command >> 17;
        if (serial != audioAuditionSerial) {
            audioAuditionSerial = serial;
            const int pitch = int(command & 255) - 1;
            auditionStep = twoPi * 440 * std::pow(2.0, (pitch - 69) / 12.0) / rate;
            auditionLevel = float((command >> 8) & 127) / 127 * .1f;
            auditionAge = 0; auditionActive = true;
        }
        if (!auditionActive) return;
        const bool held = (command & 0x10000u) != 0;
        const float attack = 1 - std::exp(-1.f / (float(rate) * .004f));
        const float release = std::exp(-1.f / (float(rate) * .012f));
        for (int i = 0; i < count; ++i) {
            const bool gate = (held || auditionAge < .12) && auditionAge < 10;
            auditionEnvelope = gate ? auditionEnvelope + (1 - auditionEnvelope) * attack : auditionEnvelope * release;
            if (!gate && auditionEnvelope < .0001f) { auditionEnvelope = 0; auditionActive = false; break; }
            const float value = float(std::sin(auditionPhase) + .18 * std::sin(auditionPhase * 2) +
                .05 * std::sin(auditionPhase * 3)) * auditionLevel * auditionEnvelope;
            for (int c = 0; c < channelCount; ++c) if (channels && channels[c]) channels[c][i] += value;
            auditionPhase += auditionStep;
            if (auditionPhase > twoPi) auditionPhase -= twoPi;
            auditionAge += 1 / rate;
        }
    }
};

PlaybackEngine::PlaybackEngine() : impl(std::make_unique<Impl>()) { impl->publish(); }
PlaybackEngine::~PlaybackEngine() = default;
void PlaybackEngine::prepare(double sampleRate) {
    auto& p = *impl;
    p.rate = std::isfinite(sampleRate) && sampleRate > 0 ? sampleRate : 44100;
    p.auditionActive = false; p.audioAuditionSerial = 0; p.auditionEnvelope = 0; p.auditionPhase = 0;
    // The host has stopped render(). Its last hazard still owns audioSchedule,
    // so recalculating voice steps is safe even if the message thread publishes.
    // Do not touch desired controls here: prepare can be a separate host thread.
    p.resetAt(p.cursor);
    p.deviceSuspended.store(false);
}
void PlaybackEngine::suspendAudio() noexcept {
    impl->deviceSuspended.store(true);
    impl->auditionRequest.store(0);
}
void PlaybackEngine::setSchedule(std::shared_ptr<const PlaybackSchedule> s, double position, double anchor) {
    auto& p = *impl; p.adoptFeedback(); p.desired.schedule = std::move(s);
    p.desired.resumeEpoch = p.silenceEpoch.load();
    p.desired.cursor = p.clamp(position);
    p.desired.anchor = p.clamp(anchor >= 0 ? anchor : p.desired.anchor);
    p.desired.loopStart = p.clamp(p.desired.loopStart); p.desired.loopEnd = p.clamp(p.desired.loopEnd);
    if (!p.desired.schedule || p.desired.schedule->notes.empty()) p.desired.playing = false;
    stopAudition(); p.publish();
}
void PlaybackEngine::clear() {
    auto& p = *impl;
    const auto revision = p.desired.revision;
    p.desired = {}; p.desired.revision = revision;
    p.desired.resumeEpoch = p.silenceEpoch.load();
    stopAudition(); p.publish();
}
void PlaybackEngine::requestSilence() noexcept { impl->silenceEpoch.fetch_add(1); }
void PlaybackEngine::play() {
    auto& p = *impl; p.adoptFeedback();
    if (!p.desired.schedule || p.desired.schedule->notes.empty()) return;
    auto& d = p.desired;
    if (d.loopEnd > d.loopStart && (d.cursor < d.loopStart || d.cursor >= d.loopEnd)) d.cursor = d.loopStart;
    else if (d.cursor >= p.length()) d.cursor = d.anchor < p.length() ? d.anchor : 0;
    d.playing = true; d.stoppedAtStart = false; p.publish();
}
void PlaybackEngine::pause() {
    auto& p = *impl; p.adoptFeedback(); p.desired.playing = false; p.desired.stoppedAtStart = false;
    stopAudition(); p.publish();
}
void PlaybackEngine::stop() {
    auto& p = *impl; p.adoptFeedback();
    if (!p.desired.playing && p.desired.stoppedAtStart) p.desired.anchor = 0;
    p.desired.cursor = p.desired.anchor; p.desired.playing = false; p.desired.stoppedAtStart = true;
    stopAudition(); p.publish();
}
void PlaybackEngine::home() {
    auto& p = *impl; p.desired.cursor = p.desired.anchor = 0;
    p.desired.playing = false; p.desired.stoppedAtStart = true; stopAudition(); p.publish();
}
void PlaybackEngine::seek(double seconds) {
    auto& p = *impl; p.adoptFeedback(); p.desired.cursor = p.desired.anchor = p.clamp(seconds);
    p.desired.stoppedAtStart = false; stopAudition(); p.publish();
}
void PlaybackEngine::setStartPosition(double seconds) {
    auto& p = *impl; p.adoptFeedback(); p.desired.anchor = p.clamp(seconds); p.publish();
}
void PlaybackEngine::setLoop(double start, double end) {
    auto& p = *impl; p.adoptFeedback();
    p.desired.loopStart = p.clamp(std::min(start, end)); p.desired.loopEnd = p.clamp(std::max(start, end));
    if (p.desired.loopEnd - p.desired.loopStart < 1.e-9) p.desired.loopStart = p.desired.loopEnd = 0;
    p.publish();
}
void PlaybackEngine::clearLoop() {
    auto& p = *impl; p.adoptFeedback(); p.desired.loopStart = p.desired.loopEnd = 0; p.publish();
}
void PlaybackEngine::setSolo(int group) {
    auto& p = *impl; p.adoptFeedback(); p.desired.solo = std::max(-1, group); stopAudition(); p.publish();
}
double PlaybackEngine::position() const noexcept { return impl->feedback().cursor; }
double PlaybackEngine::startPosition() const noexcept { return impl->desired.anchor; }
double PlaybackEngine::loopStart() const noexcept { return impl->desired.loopStart; }
double PlaybackEngine::loopEnd() const noexcept { return impl->desired.loopEnd; }
double PlaybackEngine::length() const noexcept { return impl->length(); }
bool PlaybackEngine::isPlaying() const noexcept {
    return !impl->deviceSuspended.load() && impl->feedback().playing;
}
int PlaybackEngine::solo() const noexcept { return impl->desired.solo; }
void PlaybackEngine::audition(int pitch, int velocity) {
    auto& p = *impl;
    if (p.deviceSuspended.load()) return;
    p.auditionRequest.store((++p.auditionSerial << 17) | 0x10000u |
        (std::uint64_t(std::clamp(velocity, 1, 127)) << 8) | std::uint64_t(std::clamp(pitch, 0, 127) + 1));
}
void PlaybackEngine::endAudition() { impl->auditionRequest.fetch_and(~std::uint64_t(0x10000u)); }
void PlaybackEngine::stopAudition() { impl->auditionRequest.store(0); }
void PlaybackEngine::render(float* const* channels, int channelCount, int count) noexcept {
    if (count <= 0 || channelCount < 0) return;
    auto& p = *impl;
    if (p.deviceSuspended.load()) {
        for (auto& tone : p.tones) tone = {};
        return;
    }
    const auto* control = p.acquire();
    if (!control) return;
    if (control->resumeEpoch != p.silenceEpoch.load()) {
        p.playing = false;
        for (auto& tone : p.tones) tone = {};
        p.auditionActive = false; p.audioAuditionSerial = 0; p.auditionEnvelope = 0;
        p.report();
        return;
    }
    p.renderAudition(channels, channelCount, count);
    if (control->revision != p.audioRevision) {
        p.audioRevision = control->revision; p.audioSchedule = control->schedule.get();
        p.cursor = control->cursor; p.playing = control->playing;
        p.solo = control->solo; p.loopStart = control->loopStart; p.loopEnd = control->loopEnd;
        p.resetAt(p.cursor);
    }
    if (!p.playing || !p.audioSchedule) { p.report(); return; }
    const auto& notes = p.audioSchedule->notes;
    const auto& controls = p.audioSchedule->controllers;
    for (int s = 0; s < count; ++s) {
        if (p.loopEnd > p.loopStart && p.cursor >= p.loopEnd) {
            p.cursor = p.loopStart + std::fmod(p.cursor - p.loopEnd, p.loopEnd - p.loopStart);
            p.resetAt(p.cursor);
        }
        while (p.nextControl < controls.size() && controls[p.nextControl].time <= p.cursor) {
            const auto& c = controls[p.nextControl++];
            if (laneIndex(c.controller) < 0) continue;
            for (auto& tone : p.tones) if (tone.active && tone.source == c.source) {
                if (c.controller == 7) tone.volume = c.value;
                else if (c.controller == 11) tone.expression = c.value;
                else tone.pan = c.value;
            }
        }
        while (p.nextNote < notes.size() && notes[p.nextNote].start <= p.cursor) {
            const auto& n = notes[p.nextNote++];
            if (n.end > p.cursor && (p.solo < 0 || n.group == p.solo)) p.beginTone(n);
        }
        float left = 0, right = 0;
        for (auto& tone : p.tones) if (tone.active) {
            if (p.cursor >= tone.off) tone.env *= .996f;
            else tone.env += (1 - tone.env) * .008f;
            if (p.cursor >= tone.off && tone.env < .0001f) { tone.active = false; continue; }
            const float wave = float(std::sin(tone.phase) + .18 * std::sin(tone.phase * 2) +
                .05 * std::sin(tone.phase * 3));
            const float gain = (tone.volume / 127.f) * (tone.expression / 127.f);
            const float value = wave * tone.level * tone.env * gain;
            left += value * (tone.pan <= 64 ? 1.f : (127 - tone.pan) / 63.f);
            right += value * (tone.pan >= 64 ? 1.f : tone.pan / 64.f);
            tone.phase += tone.step; if (tone.phase > twoPi) tone.phase -= twoPi;
        }
        left = std::tanh(left); right = std::tanh(right);
        for (int c = 0; c < channelCount; ++c) if (channels && channels[c])
            channels[c][s] += channelCount == 1 ? (left + right) * .5f : (c % 2 == 0 ? left : right);
        p.cursor += 1 / p.rate;
        if (p.loopEnd <= p.loopStart && p.cursor > p.audioSchedule->length + .3) {
            p.playing = false; p.cursor = p.audioSchedule->length;
            for (auto& tone : p.tones) tone = {};
            break;
        }
    }
    p.report();
}
} // namespace pianoroll
