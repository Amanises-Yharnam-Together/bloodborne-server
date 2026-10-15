// The frame limiter's slot rule (gpu/shim/bbport_frame_slots.h): two flips are never closer than
// a period minus 1/16, and on time the slots stay on their grid. No game, Vulkan device or window.
//   ninja -C out/gpu frame-slots-test && out/gpu/frame-slots-test.exe
#include <cassert>
#include <chrono>
#include <cstdio>
#include "bbport_frame_slots.h"

using namespace std::chrono;
using Clock = steady_clock;

int main() {
    const nanoseconds period{1000000000 / 120}; // 8.33 ms
    const Clock::time_point start{};

    // On time: the next slot is one period after this one (no drift from the wake-up delay).
    Clock::time_point slot = start;
    for (int i = 0; i < 1000; ++i) {
        const auto flipped = slot + microseconds(200); // woken a little late
        slot = BbFrameSlots::NextSlot(slot, flipped, period);
    }
    assert(slot == start + period * 1000);

    // A late frame (5 ms past its slot): the next one may not follow it closely.
    const auto late = start + milliseconds(5);
    const auto next = BbFrameSlots::NextSlot(start, late, period);
    assert(next - late >= period - period / 16);
    // Nor later than a period after the flip.
    assert(next - late <= period);

    // Very late (more than a period): one period minus the slack after the flip, not at once.
    const auto very_late = start + milliseconds(30);
    assert(BbFrameSlots::NextSlot(start, very_late, period) == very_late + period - period / 16);

    // Simulated: 1200 frames at a cap of 120, every 16th late by 4-12 ms: no interval between
    // flips shorter than the period minus 1/16.
    slot = start;
    Clock::time_point previous{};
    nanoseconds shortest = hours(1);
    for (int i = 0; i < 1200; ++i) {
        const auto flipped = slot + (i % 16 == 5 ? milliseconds(4 + i % 9) : microseconds(50));
        if (i > 0) {
            shortest = std::min(shortest, duration_cast<nanoseconds>(flipped - previous));
        }
        previous = flipped;
        slot = BbFrameSlots::NextSlot(slot, flipped, period);
    }
    std::printf("shortest interval %.2f ms\n", shortest.count() / 1e6);
    assert(shortest >= period - period / 16);
    std::printf("frame slots: ok\n");
    return 0;
}
