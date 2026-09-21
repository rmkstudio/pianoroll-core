#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

namespace pianoroll {
struct MeterChange { double time = 0; int numerator = 4, denominator = 4; };
struct MusicalPosition {
    std::int64_t bar = 1;
    double beat = 1;
    int numerator = 4, denominator = 4;
    bool implicitMeter = true;
};
struct MeterGridMark {
    double time = 0;
    std::int64_t bar = 1;
    int beat = 1;
    bool barStart = true, signatureChange = false, beatStart = true;
};
// Time may be beats or original ticks: pass unitsPerQuarter=1 or original PPQ.
// A signature change begins a new bar. A preceding partial bar is counted once.
// Same-time signature events use stable last-event-wins ordering.
class MeterMap {
public:
    void assign(std::vector<MeterChange>, double unitsPerQuarter = 1);
    MusicalPosition positionAt(double time) const noexcept;
    std::vector<MeterGridMark> grid(double from, double to, double minimumSpacing,
                                    std::size_t maximumMarks = 2048, double subdivisionStep = 0) const;
private:
    struct Segment {
        double time = 0, beatLength = 1, barLength = 4;
        std::int64_t bar = 1;
        int numerator = 4, denominator = 4;
        bool implicit = true;
    };
    std::vector<Segment> segments_;
};
} // namespace pianoroll
