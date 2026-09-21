#include <PianoRollCore/Playback.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>
#include <thread>

namespace {
thread_local bool inRender = false;
std::atomic<int> realtimeAllocations{0}, realtimeDeletes{0};
int checks = 0, failures = 0;
void check(bool yes, const char* label) {
    ++checks;
    if (!yes) { ++failures; std::cerr << "FAIL: " << label << '\n'; }
}
bool near(double a, double b, double tolerance = 1.e-8) { return std::abs(a-b) <= tolerance; }
struct Buffer {
    std::vector<float> left, right;
    explicit Buffer(int count = 480) : left(static_cast<std::size_t>(count)), right(left.size()) {}
    void run(pianoroll::PlaybackEngine& p) {
        std::fill(left.begin(), left.end(), 0); std::fill(right.begin(), right.end(), 0);
        float* channels[]{left.data(), right.data()};
        inRender = true; p.render(channels, 2, static_cast<int>(left.size())); inRender = false;
    }
    double power(const std::vector<float>& v) const { double sum=0; for(auto f:v) sum+=f*f; return sum; }
    bool silent() const { return power(left) == 0 && power(right) == 0; }
};
using namespace pianoroll;
std::shared_ptr<const PlaybackSchedule> one(double end = 1) {
    return makePlaybackSchedule({{0,end,69,100,0,17}});
}
}
void* operator new(std::size_t n) {
    if (inRender) ++realtimeAllocations;
    if (void* p = std::malloc(n ? n : 1)) return p;
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept { if (inRender && p) ++realtimeDeletes; std::free(p); }
void operator delete(void* p, std::size_t) noexcept { ::operator delete(p); }
void* operator new[](std::size_t n) { return ::operator new(n); }
void operator delete[](void* p) noexcept { ::operator delete(p); }
void operator delete[](void* p, std::size_t) noexcept { ::operator delete(p); }
void* operator new(std::size_t n, const std::nothrow_t&) noexcept {
    try { return ::operator new(n); } catch (...) { return nullptr; }
}
void* operator new[](std::size_t n, const std::nothrow_t&) noexcept {
    try { return ::operator new[](n); } catch (...) { return nullptr; }
}
void operator delete(void* p, const std::nothrow_t&) noexcept { ::operator delete(p); }
void operator delete[](void* p, const std::nothrow_t&) noexcept { ::operator delete[](p); }

int main() {
    std::string error;
    check(bool(makePlaybackSchedule({})), "empty schedule is valid");
    check(!makePlaybackSchedule({{0,0,60,100,0,0}}, {}, 0, &error) && !error.empty(), "zero-duration display placeholders rejected");
    check(!makePlaybackSchedule({{0,1,128,100,0,0}}), "invalid pitch rejected");
    check(!makePlaybackSchedule({{0,1,60,0,0,0}}), "note-on zero velocity rejected");
    check(!makePlaybackSchedule({{0,std::numeric_limits<double>::infinity(),60,100,0,0}}), "nonfinite note rejected");
    check(!makePlaybackSchedule({}, {{0,1,11,128}}), "invalid controller rejected");
    const auto sustain = makePlaybackSchedule({{0,.3,60,100,0,1},{.6,.7,60,100,0,1},{0,.3,60,100,1,2}},
        {{0,1,64,127},{1,1,64,0}}, 1.2);
    check(near(sustain->notes[0].end,.6), "pedal ends at same-source retrigger");
    check(near(sustain->notes[1].end,.3), "pedal isolated by source");
    check(near(sustain->notes[2].end,1), "pedal ends at release");
    const auto overlap = makePlaybackSchedule({{0,1,60,100,0,1},{.5,1.5,60,100,0,1}});
    check(near(overlap->notes[0].end,1), "no-pedal overlapping notes unchanged");
    const auto held = makePlaybackSchedule({{0,.2,60,100,0,1}}, {{0,1,64,127}}, .8);
    check(near(held->notes[0].end,.8), "unreleased pedal bounded by document end");

    PlaybackEngine p; p.prepare(48000); p.setSchedule(one());
    Buffer b;
    b.run(p); check(b.silent(), "schedule installation starts paused");
    p.play(); b.run(p);
    check(!b.silent() && p.isPlaying(), "play starts reference audio");
    check(near(p.position(),.01), "48k cursor is sample accurate");
    check(b.left == b.right, "no-controller default is old centred mono reference");
    // Direct check against the old Lines 440Hz / three-sine / .008 envelope DSP.
    double phase=0; float env=0; bool legacy=true;
    const double twoPi=6.283185307179586476925286766559;
    for(std::size_t i=0;i<b.left.size();++i) {
        env += (1-env)*.008f;
        const float wave=float(std::sin(phase)+.18*std::sin(phase*2)+.05*std::sin(phase*3));
        const float old=std::tanh(wave*(100/127.f*.07f)*env);
        if(std::abs(old-b.left[i])>2.e-7f) legacy=false;
        phase += twoPi*440/48000; if(phase>twoPi) phase-=twoPi;
    }
    check(legacy, "extracted oscillator/envelope matches legacy Lines samples");
    p.pause(); const auto paused=p.position(); b.run(p);
    check(b.silent() && !p.isPlaying() && near(p.position(),paused), "pause leaves cursor and kills voices");
    p.play(); b.run(p);
    check(!b.silent() && near(p.position(),paused+.01), "resume chases held note");
    p.seek(.5); b.run(p);
    check(p.isPlaying() && !b.silent() && near(p.position(),.51), "seek preserves playing and chases held note");
    p.stop(); b.run(p);
    check(!p.isPlaying() && b.silent() && near(p.position(),.5), "first stop returns to seek anchor");
    p.stop(); check(near(p.position(),0), "second stop returns to zero");
    p.seek(.4); p.play(); p.home(); b.run(p);
    check(b.silent() && near(p.position(),0) && near(p.startPosition(),0), "home resets cursor and seek anchor");
    p.setLoop(.2,.205); p.play(); b.run(p);
    check(p.position() >= .2 && p.position() < .205+1./48000 && !b.silent(), "short loop wraps and chases held notes");
    p.clearLoop(); p.pause(); p.setStartPosition(.3);
    check(near(p.startPosition(),.3), "adapter may move anchor without seeking");
    p.seek(.3); p.play(); p.setSchedule(one(2), .7, .6); b.run(p);
    check(near(p.position(),.71) && near(p.startPosition(),.6) && p.isPlaying(), "tempo rebuild carries position and anchor independently");
    p.setSchedule(one(.02),0,0); p.clearLoop(); p.play(); Buffer longBuffer(24000); longBuffer.run(p);
    check(!p.isPlaying() && near(p.position(),.02), "end stops and cursor returns to musical length");
    b.run(p); check(b.silent(), "no hung tail after end");

    const auto ensemble = makePlaybackSchedule({{0,1,60,100,0,1},{0,1,76,100,1,2}});
    p.setSchedule(ensemble); p.setSolo(-1); p.play(); b.run(p); const double allPower=b.power(b.left);
    p.setSolo(0); b.run(p); const double soloPower=b.power(b.left);
    check(allPower>0 && soloPower>0 && std::abs(allPower-soloPower)>.01, "all-track ensemble and group solo both render");
    p.setSolo(42); b.run(p); check(b.silent(), "solo switch clears previous sounding group");
    p.setSolo(-1); b.run(p); check(!b.silent(), "return to all chases every held group");

    p.setSchedule(makePlaybackSchedule({{0,1,69,100,0,1}}, {{0,1,7,0}})); p.play(); b.run(p);
    check(b.silent(), "real CC7 zero mutes gain");
    p.setSchedule(makePlaybackSchedule({{0,1,69,100,0,1}}, {{0,1,11,0},{.005,1,11,127}})); b.run(p);
    check(std::all_of(b.left.begin(),b.left.begin()+240,[](float v){return v==0;}) && b.power(b.left)>0, "CC11 applies at its actual event time");
    p.seek(.003); b.run(p); check(b.left.front()==0 && b.power(b.left)>0, "seek chases controller then plays later change");
    p.setSchedule(makePlaybackSchedule({{0,1,69,100,0,1}}, {{0,1,10,0}})); b.run(p);
    check(b.power(b.left)>0 && b.power(b.right)==0, "CC10 hard-left pan");
    p.setSchedule(makePlaybackSchedule({{0,1,69,100,0,1}}, {{0,2,7,0}})); b.run(p);
    check(!b.silent(), "controller source cannot mute another track/channel");
    p.setSchedule(makePlaybackSchedule({{0,1,69,100,0,1}}, {{0,1,11,0},{0,1,11,127}})); b.run(p);
    check(!b.silent(), "same-tick controller order is stable, last wins");
    p.setSchedule(makePlaybackSchedule({{0,1,69,100,0,1}}, {{0,1,11,127},{.2,1,11,0},{.4,1,11,127}}));
    p.setLoop(.25,.26); p.play(); b.run(p); b.run(p);
    check(b.silent(), "loop boundary restores controller value before loop start");
    p.clearLoop(); p.home(); p.audition(60,100); b.run(p);
    check(!b.silent() && !p.isPlaying(), "single note audition works while paused");
    p.endAudition(); longBuffer.run(p); b.run(p); check(b.silent(), "released audition completes minimum tap then decays");
    p.audition(72); b.run(p); p.pause(); b.run(p); check(b.silent(), "pause also silences keyboard audition");
    p.play(); b.run(p); p.requestSilence(); check(!p.isPlaying(), "host-thread silence immediately marks transport stopped");
    p.play(); p.audition(60); b.run(p); check(b.silent(), "play or audition cannot reopen stale host state");
    p.setSchedule(one()); p.play(); b.run(p); check(!b.silent(), "new schedule explicitly reopens host-state silence gate");
    p.clear(); b.run(p); check(b.silent() && !p.isPlaying() && p.solo()==-1 && near(p.position(),0), "document clear resets all transport state");
    p.setSchedule(one()); p.play();
    std::fill(b.left.begin(),b.left.end(),.2f); std::fill(b.right.begin(),b.right.end(),.2f);
    float* output[]{b.left.data(),b.right.data()};
    p.render(output,2,int(b.left.size()));
    check(near(b.left.front(),.2,1.e-6) && b.left[2]>.2f, "reference audio adds to host input buffers");
    p.prepare(96000); p.setSchedule(one()); p.play(); Buffer b96(960); b96.run(p);
    check(near(p.position(),.01), "96k cursor matches 48k real duration");
    p.seek(.5); b96.run(p); check(near(p.position(),.51), "96k seek matches 48k position");
    p.suspendAudio(); b96.run(p); check(b96.silent() && !p.isPlaying(), "device suspension immediately gates audio");
    p.prepare(48000); p.play(); b.run(p);
    check(!b.silent() && near(p.position(),.52), "device prepare restores same document and position without schedule rebuild");
    p.requestSilence(); p.suspendAudio(); p.prepare(48000); p.play(); b.run(p);
    check(b.silent() && !p.isPlaying(), "device restart never reopens pending host-state silence");
    p.setSchedule(one()); p.play(); b.run(p); check(!b.silent(), "message-thread restored schedule can reopen both lifecycle stages");

    // Stress the actual lock-free handoff. Audio runs concurrently while the
    // message thread replaces schedules, seeks, solos and invalidates restores.
    PlaybackEngine stress; stress.prepare(48000); stress.setSchedule(one()); stress.play();
    std::atomic<bool> done{false}; std::atomic<int> blocks{0};
    std::thread audio([&] {
        Buffer block(64);
        while(!done.load()) { block.run(stress); ++blocks; }
    });
    for(int i=0;i<1500;++i) {
        stress.setSchedule(makePlaybackSchedule({{0,1,48+i%36,100,i%3,16+i%2}}), .01*(i%20));
        stress.setSolo(i%4-1); stress.play();
        if(i%17==0) stress.requestSilence();
        if(i%5==0) stress.pause();
    }
    done=true; audio.join();
    check(blocks>0, "concurrent replacement/seek/solo/restore handoff ran");
    check(realtimeAllocations==0, "render performs zero heap allocations");
    check(realtimeDeletes==0, "render never frees retired state or schedules");
    std::cout << checks-failures << '/' << checks << " playback checks passed\n";
    return failures ? 1 : 0;
}
