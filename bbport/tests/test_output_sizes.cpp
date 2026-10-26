// SPDX-License-Identifier: GPL-2.0-or-later
// bbport_output_sizes.h: the resolutions the menus offer (the monitor's), and the output for
// borderless windows and the 16:9 aspect ratio.
#include <cassert>
#include <cstdio>
#include <vector>
#include "gpu/shim/bbport_output_sizes.h"

using BbOutputSizes::Size;

int main() {
    using namespace BbOutputSizes;
    // bbport.ini values: old and new ones, the launcher's "×".
    assert(Parse("1920x1080") == (Size{1920, 1080}));
    assert(Parse("2560 x 1080") == (Size{2560, 1080}));
    assert(Parse("3440\xc3\x97" "1440") == (Size{3440, 1440}));
    assert(Parse("5120X1440") == (Size{5120, 1440}));
    assert(!Parse("1") && !Parse("") && !Parse("1920x") && !Parse("16000x9000") && !Parse("1920x1080p"));
    assert(Text({2560, 1080}) == "2560x1080");
    assert(Unpack(Pack({7680, 2160})) == (Size{7680, 2160}));

    // 16:9 inside wider and taller outputs (even sizes); 16:9 itself is kept.
    assert(Fit16x9({2560, 1080}) == (Size{1920, 1080}));
    assert(Fit16x9({3440, 1440}) == (Size{2560, 1440}));
    assert(Fit16x9({5120, 1440}) == (Size{2560, 1440}));
    assert(Fit16x9({1920, 1200}) == (Size{1920, 1080}));
    assert(Fit16x9({1024, 768}) == (Size{1024, 576}));
    assert(Fit16x9({1280, 720}) == (Size{1280, 720}));
    assert(Fit16x9({1366, 768}) == (Size{1366, 768})); // 1365.3 lines up to an even 1366

    // Borderless: the desktop's shape; the height kept on a wider desktop, the width on a taller.
    assert(FitWindow({1920, 1080}, {2560, 1080}) == (Size{2560, 1080}));
    assert(FitWindow({1280, 720}, {3440, 1440}) == (Size{1720, 720}));
    assert(FitWindow({1920, 1080}, {5120, 1440}) == (Size{3840, 1080}));
    assert(FitWindow({1920, 1080}, {1920, 1200}) == (Size{1920, 1200}));
    assert(FitWindow({1920, 1080}, {1600, 1200}) == (Size{1920, 1440}));
    assert(FitWindow({2560, 1440}, {3840, 2160}) == (Size{2560, 1440}));
    assert(FitWindow({2560, 1080}, {}) == (Size{2560, 1080}));
    assert(FitWindow({3840, 2160}, {7680, 1080}).width == Max.width); // never past the largest

    // Before a choice: 1080 lines in the desktop's shape.
    assert(DefaultOutput({}) == Game);
    assert(DefaultOutput({3840, 2160}) == Game);
    assert(DefaultOutput({2560, 1080}) == (Size{2560, 1080}));
    assert(DefaultOutput({3440, 1440}) == (Size{2580, 1080}));
    assert(DefaultOutput({5120, 1440}) == (Size{3840, 1080}));
    assert(DefaultOutput({1920, 1200}) == (Size{1728, 1080}));
    assert(DefaultOutput({1024, 768}) == (Size{1440, 1080}));

    // The monitor's modes: each size once (refresh rates), none below Min or above Max, the
    // desktop size, and the 16:9 sizes that fit it for windowed play; fewer pixels first.
    const std::vector<Size> modes = {{3440, 1440}, {3440, 1440}, {2560, 1080}, {1920, 1080},
                                     {800, 600},   {640, 480},   {1024, 768},  {1720, 720},
                                     {1280, 720},  {1920, 1080}};
    const auto list = Choices(modes, {3440, 1440});
    const std::vector<Size> want = {{1024, 768},  {1280, 720},  {1720, 720},  {1920, 1080},
                                    {2560, 1080}, {2560, 1440}, {3440, 1440}};
    assert(list == want);
    // A 4K desktop without a mode list (Wayland): the desktop and every standard size.
    const auto four_k = Choices({}, {3840, 2160});
    assert(four_k.size() == 4 && four_k.back() == (Size{3840, 2160}));
    // A 1366x768 laptop: 720p fits, 1080p does not.
    const auto laptop = Choices(std::vector<Size>{{1366, 768}, {1280, 720}}, {1366, 768});
    assert((laptop == std::vector<Size>{{1280, 720}, {1366, 768}}));
    // Unknown display (tests, a failed query): the four standard sizes.
    assert(Choices({}, {}).size() == 4);
    // 32:9 and 8K.
    const auto wide = Choices(std::vector<Size>{{5120, 1440}, {7680, 2160}, {8192, 4320}}, {5120, 1440});
    assert(std::find(wide.begin(), wide.end(), Size{7680, 2160}) != wide.end());
    assert(std::find(wide.begin(), wide.end(), Size{8192, 4320}) == wide.end());

    // The menu row's choice for a size: itself, else the nearest height.
    assert(Nearest(want, {2560, 1080}) == 4);
    assert(Nearest(want, {2000, 1080}) == 3);
    assert(Nearest(want, {3840, 2160}) == 6);
    assert(Nearest(std::vector<Size>{}, {1, 1}) == -1);

    assert(std::string(AspectName({2560, 1080})) == "21:9");
    assert(std::string(AspectName({3440, 1440})) == "21:9");
    assert(std::string(AspectName({3840, 1600})) == "21:9");
    assert(std::string(AspectName({5120, 1440})) == "32:9");
    assert(std::string(AspectName({1920, 1200})) == "16:10");
    assert(std::string(AspectName({1024, 768})) == "4:3");
    assert(std::string(AspectName({1366, 768})) == "16:9");
    assert(std::string(AspectName({1920, 1080})) == "16:9");

    // Kaniel 2026-10-26: the old launcher's 16:9 sizes (written with no aspect_ratio key) on a desktop
    // of another shape are no choice: that shape at their height, at most the desktop's (patches.py,
    // the launcher's migrate_old_output). A key, a 16:9 desktop or another size: the player's choice.
    assert(OldLauncherOutput({1920, 1080}, false, {2560, 1080}));
    assert(OldLauncherOutput({3840, 2160}, false, {1920, 1200}));
    assert(!OldLauncherOutput({1920, 1080}, true, {2560, 1080}));
    assert(!OldLauncherOutput({1920, 1080}, false, {3840, 2160}));
    assert(!OldLauncherOutput({1920, 1080}, false, {1366, 768}));
    assert(!OldLauncherOutput({1920, 1080}, false, {}));
    assert(!OldLauncherOutput({1720, 720}, false, {2560, 1080}));
    assert(OldLauncherRefit({1920, 1080}, {2560, 1080}) == (Size{2560, 1080}));
    assert(OldLauncherRefit({1920, 1080}, {3440, 1440}) == (Size{2580, 1080}));
    assert(OldLauncherRefit({2560, 1440}, {3440, 1440}) == (Size{3440, 1440}));
    assert(OldLauncherRefit({3840, 2160}, {3440, 1440}) == (Size{3440, 1440}));
    assert(OldLauncherRefit({1920, 1080}, {5120, 1440}) == (Size{3840, 1080}));
    assert(OldLauncherRefit({1920, 1080}, {1920, 1200}) == (Size{1728, 1080}));

    // The game's start, one line in the log (players' reports answer it directly).
    assert(ResolutionText({1920, 1080}, true, "borderless", {2560, 1080}, {2560, 1080}, "2560x1080 (startup patch)") ==
           "Resolution: ini 1920x1080 (the old launcher's 16:9 size: the desktop's shape), window borderless, "
           "desktop 2560x1080 -> output 2560x1080, scene 2560x1080 (startup patch), aspect 2.370");
    assert(ResolutionText({}, false, "windowed", {}, {1920, 1080}, "1920x1080 (the game's)") ==
           "Resolution: ini unset, window windowed, desktop unknown -> output 1920x1080, scene 1920x1080 "
           "(the game's), aspect 1.778");
    std::printf("output sizes: all checks passed\n");
    return 0;
}
