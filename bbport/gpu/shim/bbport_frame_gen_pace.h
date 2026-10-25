// SPDX-License-Identifier: GPL-2.0-or-later
// bbport: frame generation's pacing rules (vk_frame_generation.h, vk_presenter.cpp), apart so
// they can be tested without a GPU. From Supermedo's bbport (026f8ff, 389124d, aabd7f7):
//  - the frames made per game frame for the provider (FSR 3.1: one; DLSS: the multiplier less
//    one, up to three on a GeForce RTX 50);
//  - "fit to display refresh": at most the frames the display shows between two game frames,
//    decided once a second; always so with V-Sync (FIFO), where each frame more waits for the
//    display (upstream caps the game instead, which would slow co-op's fixed 60 FPS);
//  - a pause when presenting takes the whole frame interval (a GPU or display too slow);
//  - the FPS counter: the total and, with frames generated, the game's own rate;
//  - DLSS Frame Generation's release apart from the swap thread's present.
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <mutex>

namespace BbFrameGenPace {

/// BbSettings::frame_gen_provider values (0 when frame generation is off).
enum Provider { ProviderOff = 0, ProviderFsr3 = 1, ProviderDlss = 2 };

/// Frames generated per game frame at most (DLSS 4x).
constexpr unsigned MaxGenerated = 3;

/// Generated frames per game frame the settings ask for. `dlss_max`: what DLSS Frame
/// Generation can make on this GPU (0 none: FSR 3.1 takes over, one).
inline unsigned WantedCount(int provider, int multiplier, int dlss_max) {
    if (provider == ProviderOff) {
        return 0;
    }
    if (provider == ProviderDlss && dlss_max > 0) {
        const int most = std::min(dlss_max, int(MaxGenerated));
        return unsigned(std::clamp(multiplier - 1, 1, most));
    }
    return 1;
}

/// How many generated frames go out per game frame now (present thread, once per game frame).
class Limiter {
public:
    /// A game frame was presented at `now_s` (seconds). `hz`: the display's refresh rate (0
    /// unknown); `fit`: no more frames than it shows (the setting, or V-Sync); `interval_ms`: the
    /// game's smoothed frame interval; `present_ms`: the time presenting its images took.
    unsigned OnGameFrame(double now_s, unsigned hz, bool fit, double interval_ms, double present_ms) {
        // The images the display shows per game frame: 60 FPS on 144 Hz has room for one
        // generated frame, on 240 Hz for three. Decided once a second from that second's game
        // frame rate (single slow frames while an area loads do not count): down at once when
        // 10% short, up after two seconds with 5% to spare.
        if (window_start < 0.0) {
            window_start = now_s;
            frames = 0;
        }
        ++frames;
        const double window_s = now_s - window_start;
        if (window_s >= 1.0) {
            const double ratio = hz * window_s / frames;
            window_start = now_s;
            frames = 0;
            if (window_s < 2.0) { // not after a pause of the game
                if (limit > 0 && ratio < (limit + 1) * 0.9) {
                    while (limit > 0 && ratio < (limit + 1) * 0.9) {
                        --limit;
                    }
                    up_seconds = 0;
                } else if (limit < MaxGenerated && ratio >= (limit + 2) * 0.95) {
                    if (++up_seconds >= 2) {
                        ++limit;
                        up_seconds = 0;
                    }
                } else {
                    up_seconds = 0;
                }
            }
        }
        if (!fit || hz == 0) {
            // The multiplier as chosen: past the refresh rate the display shows the newest image.
            limit = MaxGenerated;
        }
        // The share of the frame interval spent presenting: paced, about count / (count + 1).
        // Near one presenting holds the game back: off for a while, longer each time.
        // Each frame counts at most 1.2: one long hitch (a load, a shader compile) is no display
        // that cannot keep up; about ten overloaded frames in a row are (Supermedo b382856).
        if (interval_ms > 0.0) {
            busy += (std::min(present_ms / interval_ms, 1.2) - busy) * 0.1;
        }
        if (shown_before && busy > 0.97) {
            hold_seconds = now_s - hold_until > 60.0 ? 5 : std::min(hold_seconds * 2, 120);
            hold_until = now_s + hold_seconds;
            busy = 0.0;
            std::printf("Frame generation: paused for %d s, presenting does not keep up (%.1f of "
                        "%.1f ms per game frame)\n",
                        hold_seconds, present_ms, interval_ms);
        }
        const unsigned now_limit = Paused(now_s) ? 0 : limit;
        shown_before = now_limit > 0;
        return now_limit;
    }

    [[nodiscard]] bool Paused(double now_s) const noexcept {
        return now_s < hold_until;
    }

    /// Frame generation off: the next start decides afresh.
    void Reset() noexcept {
        limit = 0;
        frames = up_seconds = 0;
        window_start = -1.0;
        busy = 0.0;
        shown_before = false;
    }

private:
    unsigned limit = 0;
    unsigned frames = 0, up_seconds = 0;
    double window_start = -1.0;
    double busy = 0.0;
    bool shown_before = false;
    int hold_seconds = 5;
    double hold_until = -1e9;
};

/// The FPS counter's text: "120 FPS (game 60, frame gen 2x)" while frames are generated (the
/// total follows the game's rate times the multiplier, the game's rate is what resolution and
/// upscaler change), else "60 FPS  16.7 ms".
inline void FpsText(char* text, std::size_t size, float shown_fps, float game_fps, bool generating,
                    float frame_ms) {
    if (generating && game_fps > 0.0f && shown_fps > game_fps * 1.25f) {
        std::snprintf(text, size, "%.0f FPS (game %.0f, frame gen %dx)", shown_fps, game_fps,
                      int(shown_fps / game_fps + 0.5f));
    } else {
        std::snprintf(text, size, "%.0f FPS  %.1f ms", shown_fps, frame_ms);
    }
}

/// DLSS Frame Generation released (switched off, or another provider): under the present's lock
/// (`present_lock`, which every present and queue submit takes), once the queues are idle
/// (`wait_idle`), so no present runs beside the release. Kaniel's playtest 2026-10-22 (v1.1.5):
/// NVIDIA's driver crashed in the swap thread's present right after frame generation went off.
template <class Lock, class WaitIdle, class Release>
void ReleaseBesidePresent(Lock& present_lock, WaitIdle&& wait_idle, Release&& release) {
    std::scoped_lock lock{present_lock};
    wait_idle();
    release();
}

} // namespace BbFrameGenPace
