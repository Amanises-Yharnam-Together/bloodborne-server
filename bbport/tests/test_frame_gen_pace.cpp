// Frame generation's pacing rules (gpu/shim/bbport_frame_gen_pace.h): how many frames are made
// per game frame for the provider and multiplier, how many the display has room for when fitted
// (or with V-Sync), the pause when presenting falls behind, and the FPS counter's text. No game,
// Vulkan device or window.
//   ninja -C out/gpu frame-gen-pace-test && out/gpu/frame-gen-pace-test.exe
#include <cassert>
#include <cstdio>
#include <cstring>
#include "bbport_frame_gen_pace.h"

using namespace BbFrameGenPace;

namespace {

/// `seconds` of game frames at `fps` fed to `limiter`: the limit after the last one.
unsigned Run(Limiter& limiter, double& clock, double seconds, double fps, unsigned hz, bool fit,
             double present_share = 0.5) {
    unsigned limit = 0;
    const double frame = 1.0 / fps;
    for (double t = 0.0; t < seconds; t += frame) {
        clock += frame;
        limit = limiter.OnGameFrame(clock, hz, fit, 1000.0 / fps, 1000.0 / fps * present_share);
    }
    return limit;
}

} // namespace

int main() {
    // Generated frames per game frame: FSR 3.1 makes one whatever the multiplier.
    assert(WantedCount(ProviderFsr3, 4, 3) == 1);
    assert(WantedCount(ProviderFsr3, 2, 0) == 1);
    // DLSS: the multiplier less one, within what the GPU can (RTX 40: one, RTX 50: three).
    assert(WantedCount(ProviderDlss, 2, 3) == 1);
    assert(WantedCount(ProviderDlss, 3, 3) == 2);
    assert(WantedCount(ProviderDlss, 4, 3) == 3);
    assert(WantedCount(ProviderDlss, 4, 1) == 1);
    assert(WantedCount(ProviderDlss, 9, 3) == 3);
    // Without DLSS Frame Generation on the GPU, FSR 3.1's one (the provider falls back).
    assert(WantedCount(ProviderDlss, 4, 0) == 1);
    assert(WantedCount(ProviderOff, 4, 3) == 0);

    // Not fitted: every frame wanted from the start, on any display.
    {
        Limiter limiter;
        double clock = 0.0;
        assert(Run(limiter, clock, 3.0, 60.0, 60, false) == MaxGenerated);
    }
    // Fitted: 60 FPS on 144 Hz has room for one generated frame (2x = 120), on 240 Hz for three.
    {
        Limiter limiter;
        double clock = 0.0;
        assert(Run(limiter, clock, 0.5, 60.0, 144, true) == 0); // starts without, decides per second
        assert(Run(limiter, clock, 4.0, 60.0, 144, true) == 1);
        assert(Run(limiter, clock, 6.0, 60.0, 144, true) == 1); // stays
    }
    {
        Limiter limiter;
        double clock = 0.0;
        assert(Run(limiter, clock, 10.0, 60.0, 240, true) == 3);
    }
    // Fitted: 100 FPS on 144 Hz has no room; when the game slows to 60 there is.
    {
        Limiter limiter;
        double clock = 0.0;
        assert(Run(limiter, clock, 5.0, 100.0, 144, true) == 0);
        assert(Run(limiter, clock, 5.0, 60.0, 144, true) == 1);
        // Back to 100: down at once (within the next second).
        assert(Run(limiter, clock, 2.5, 100.0, 144, true) == 0);
    }
    // Presenting that takes the whole frame interval (FIFO on a slower display) pauses it,
    // fitted or not, and it comes back after the pause.
    {
        Limiter limiter;
        double clock = 0.0;
        assert(Run(limiter, clock, 1.0, 60.0, 240, false) == MaxGenerated);
        assert(Run(limiter, clock, 1.0, 60.0, 240, false, 1.0) == 0);
        assert(limiter.Paused(clock));
        assert(Run(limiter, clock, 6.0, 60.0, 240, false) == MaxGenerated);
        assert(!limiter.Paused(clock));
    }
    // One long hitch (a load, a shader compile: 98 ms of a 16.7 ms interval) is no display that
    // cannot keep up: no pause (Supermedo b382856). Ten overloaded frames in a row still pause.
    {
        Limiter limiter;
        double clock = 0.0;
        assert(Run(limiter, clock, 1.0, 60.0, 240, false) == MaxGenerated);
        clock += 1.0 / 60.0;
        assert(limiter.OnGameFrame(clock, 240, false, 1000.0 / 60.0, 98.0) == MaxGenerated);
        assert(!limiter.Paused(clock));
        assert(Run(limiter, clock, 1.0, 60.0, 240, false) == MaxGenerated);
        assert(Run(limiter, clock, 0.5, 60.0, 240, false, 1.2) == 0);
        assert(limiter.Paused(clock));
    }
    // The refresh rate unknown (0): no room is assumed to be missing.
    {
        Limiter limiter;
        double clock = 0.0;
        assert(Run(limiter, clock, 3.0, 60.0, 0, true) == MaxGenerated);
    }

    // FPS counter: the total, and with frames generated the game's own rate and the multiplier.
    char text[96];
    FpsText(text, sizeof(text), 120.0f, 60.0f, true, 8.3f);
    assert(std::strcmp(text, "120 FPS (game 60, frame gen 2x)") == 0);
    FpsText(text, sizeof(text), 238.0f, 60.0f, true, 4.2f);
    assert(std::strcmp(text, "238 FPS (game 60, frame gen 4x)") == 0);
    FpsText(text, sizeof(text), 60.0f, 60.0f, false, 16.7f);
    assert(std::strcmp(text, "60 FPS  16.7 ms") == 0);
    FpsText(text, sizeof(text), 60.0f, 0.0f, true, 16.7f); // game rate not known yet
    assert(std::strcmp(text, "60 FPS  16.7 ms") == 0);

    // Releasing DLSS Frame Generation (switched off): under the present's lock, after the queue
    // has finished presenting (Kaniel's playtest 2026-10-22: the driver crashed in the swap
    // thread's present right after the release).
    {
        struct PresentLock {
            bool held = false;
            int locks = 0;
            void lock() {
                assert(!held);
                held = true;
                ++locks;
            }
            void unlock() {
                assert(held);
                held = false;
            }
        } present;
        char steps[8] = {};
        int n = 0;
        ReleaseBesidePresent(
            present,
            [&] {
                assert(present.held);
                steps[n++] = 'w';
            },
            [&] {
                assert(present.held);
                steps[n++] = 'r';
            });
        assert(std::strcmp(steps, "wr") == 0);
        assert(present.locks == 1 && !present.held);
    }

    std::printf("frame-gen-pace-test: all checks passed\n");
    return 0;
}
