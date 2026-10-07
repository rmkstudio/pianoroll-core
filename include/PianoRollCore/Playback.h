// Shared reference audition and transport, extracted from MIDI Lines.
// GPL-3.0; see the package LICENSE and NOTICE.md.
#pragma once

#include <array>
#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace pianoroll {

// Times are seconds after the adapter has applied its document tempo map.
// group selects a voice/track for solo; source distinguishes original track/channel.
struct PlaybackNote {
    double start = 0, end = 0;
    int pitch = 60, velocity = 100, group = 0, source = 0;
    // Derived by makePlaybackSchedule before CC64 extends end; never exported.
    double keyEnd = -1;
};
struct PlaybackController {
    double time = 0;
    int source = 0, controller = 0, value = 0;
};
struct PlaybackValue { double time = 0; int value = 0; };
struct PlaybackSource {
    int source = 0;
    // CC 7, 11, 10 respectively; default gain is unity and pan is centred.
    std::array<std::vector<PlaybackValue>, 3> lanes;
};
struct PlaybackSchedule {
    std::vector<PlaybackNote> notes;
    std::vector<PlaybackController> controllers;
    std::vector<PlaybackSource> sources;
    double length = 0;
};

// Message thread only. Validates, stable-sorts, builds controller lookups and
// extends CC64-held notes to pedal release / same-source pitch retrigger / length.
// Supply unextended note ends. Empty valid input returns an empty schedule;
// invalid input returns nullptr and, when provided, a concise error string.
std::shared_ptr<const PlaybackSchedule> makePlaybackSchedule(
    std::vector<PlaybackNote> notes,
    std::vector<PlaybackController> controllers = {}, double length = 0,
    std::string* error = nullptr);

class PlaybackEngine {
public:
    PlaybackEngine();
    // The host must have stopped render() before destruction or prepare().
    ~PlaybackEngine();
    PlaybackEngine(const PlaybackEngine&) = delete;
    PlaybackEngine& operator=(const PlaybackEngine&) = delete;

    void prepare(double sampleRate);
    // Audio-device lifecycle only: gate render until the next prepare(). Does
    // not change the document or release the independent host-state silence gate.
    void suspendAudio() noexcept;
    // All controls/getters below have a single message-thread owner. They publish
    // immutable control snapshots; render() never locks, allocates or frees them.
    // Preserve playing/solo/loop, replace sounding notes, clamp to new duration.
    // Optional anchor converts the last user seek point through a new tempo map.
    void setSchedule(std::shared_ptr<const PlaybackSchedule>,
                     double positionSeconds = 0, double startPositionSeconds = -1);
    void clear();
    // May be called from any host restore thread. Immediately gates all
    // audition until the message thread installs a new schedule or calls clear().
    // It neither touches the document nor destroys a published schedule. A
    // normal device stop/start uses suspendAudio(), then prepare().
    void requestSilence() noexcept;
    void play();
    void pause();                 // retains the current position
    void stop();                  // seek anchor; a second stop returns to zero
    void home();                  // stops and returns to zero immediately
    void seek(double seconds);    // sets seek anchor, preserves play/pause state
    void setStartPosition(double seconds); // changes anchor without moving cursor
    void setLoop(double startSeconds, double endSeconds);
    void clearLoop();
    void setSolo(int group);      // -1 means all; caller implements toggle UI
    double position() const noexcept;
    double startPosition() const noexcept;
    double loopStart() const noexcept;
    double loopEnd() const noexcept;
    double length() const noexcept;
    bool isPlaying() const noexcept;
    int solo() const noexcept;

    void audition(int pitch, int velocity = 100);
    void endAudition();
    void stopAudition();
    // Add reference audio to existing host buffers. nullptr channels are allowed.
    // No MIDI output: this reference oscillator does not emulate plugin instruments.
    void render(float* const* channels, int numChannels, int numSamples) noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
} // namespace pianoroll
