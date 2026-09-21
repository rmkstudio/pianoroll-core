#include <PianoRollCore/Playback.h>
#include <array>
#include <iostream>

int main()
{
    // An adapter converts its edited notes/CC through its final tempo map first.
    auto schedule = pianoroll::makePlaybackSchedule(
        {{0, .5, 60, 100, 0, 0}, {0, .75, 67, 85, 1, 16}},
        {{0, 0, 11, 100}, {.25, 16, 64, 127}, {1, 16, 64, 0}}, 1);
    pianoroll::PlaybackEngine preview;
    preview.prepare(48000);
    preview.setSchedule(schedule);
    preview.setSolo(-1); // all tracks; view/edit scope remains the adapter's job
    preview.play();
    std::array<float, 480> left{}, right{};
    float* channels[]{left.data(), right.data()};
    preview.render(channels, 2, int(left.size())); // audio thread: additive, no locks/allocations
    preview.pause();
    std::cout << "Paused at " << preview.position() << " seconds\n";
    preview.seek(.4); preview.setLoop(.25, .75); preview.play();
    preview.render(channels, 2, int(left.size()));
    preview.requestSilence(); // a host restore can issue this from any thread
    preview.clear();          // message thread; render must stop before destruction
}
