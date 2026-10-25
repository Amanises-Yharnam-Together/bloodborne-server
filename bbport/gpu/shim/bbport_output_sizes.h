// SPDX-License-Identifier: GPL-2.0-or-later
// bbport: the output resolutions the menus offer and the size the upscaler makes (Kaniel
// 2026-10-24: every resolution the monitor has, ultrawide 21:9 and 32:9, 16:10, 4:3; the game
// shows a wider or taller view, not stretched). The game keeps its own 1920x1080 frame; for
// another aspect ratio its camera, HUD markers and UI follow the output (bbport_guest_hooks, the
// UI composition), after deadinside28's bbport 0.6 ("any aspect ratio, live, through hooks").
// Pure functions: the settings keep the state (bbport_settings.h), tests/test_output_sizes.cpp.

#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace BbOutputSizes {

struct Size {
    int width = 0, height = 0;
    bool operator==(const Size&) const = default;
    bool Valid() const { return width > 0 && height > 0; }
};

/// The game's own size, the default output.
inline constexpr Size Game{1920, 1080};
/// The outputs before the list came from the monitor (BB_PRESET_FILE's output numbers), and the
/// 16:9 sizes kept for windowed play.
inline constexpr Size Standard[] = {{1280, 720}, {1920, 1080}, {2560, 1440}, {3840, 2160}};
/// The largest output the upscalers and the UI targets take (8K; 32:9 at 2160 lines).
inline constexpr Size Max{7680, 4320};
/// The smallest mode of a display the menus offer (no 640x480 and 800x600 lists).
inline constexpr Size Min{1024, 576};

constexpr std::uint32_t Pack(Size s) {
    return std::uint32_t(s.width & 0xffff) | std::uint32_t(s.height & 0xffff) << 16;
}
constexpr Size Unpack(std::uint32_t v) {
    return {int(v & 0xffff), int(v >> 16)};
}

/// "2560x1080" (bbport.ini output_res; "2560 x 1080" and "2560×1080" too), nullopt when not a
/// size within Max.
inline std::optional<Size> Parse(std::string_view text) {
    std::string t;
    for (std::size_t i = 0; i < text.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        if (c == ' ' || c == '\t') continue;
        // U+00D7 (the launcher's multiplication sign) in UTF-8.
        if (c == 0xc3 && i + 1 < text.size() && static_cast<unsigned char>(text[i + 1]) == 0x97) {
            t += 'x';
            ++i;
            continue;
        }
        t += char(c == 'X' ? 'x' : c);
    }
    int w = 0, h = 0;
    char rest = 0;
    if (std::sscanf(t.c_str(), "%dx%d%c", &w, &h, &rest) != 2) return std::nullopt;
    if (w < 320 || h < 200 || w > Max.width || h > Max.height) return std::nullopt;
    return Size{w, h};
}

inline std::string Text(Size s) {
    return std::to_string(s.width) + "x" + std::to_string(s.height);
}

/// Rounded to an even number (the game has half-size buffers), at least 2.
inline int Even(double v) {
    return std::max(2, int(std::lround(v / 2.0)) * 2);
}

/// 16:9 inside `s`, as large as it fits: the old picture with bars on a wider (taller) output.
inline Size Fit16x9(Size s) {
    if (!s.Valid() || std::int64_t(s.width) * 9 == std::int64_t(s.height) * 16) return s;
    if (std::int64_t(s.width) * 9 > std::int64_t(s.height) * 16) {
        return {Even(s.height * 16.0 / 9.0), s.height};
    }
    return {s.width, Even(s.width * 9.0 / 16.0)};
}

/// `s` with the shape of `window` (borderless: the window is the desktop): the height kept on a
/// wider window, the width on a taller one.
inline Size FitWindow(Size s, Size window) {
    if (!s.Valid() || !window.Valid() ||
        std::int64_t(s.width) * window.height == std::int64_t(s.height) * window.width) {
        return s;
    }
    const double ratio = double(window.width) / double(window.height);
    if (double(s.width) / double(s.height) < ratio) {
        return {std::min(Even(s.height * ratio), Max.width), s.height};
    }
    return {s.width, std::min(Even(s.width / ratio), Max.height)};
}

/// The output before one is chosen: the game's 1080 lines in the desktop's shape (2560x1080 on a
/// 21:9 desktop, 1728x1080 on 16:10), as heavy to draw as the game's own size.
inline Size DefaultOutput(Size desktop) {
    if (!desktop.Valid()) return Game;
    return {std::min(Even(Game.height * double(desktop.width) / double(desktop.height)), Max.width),
            Game.height};
}

/// The sizes the menus offer: the display's modes (each size once, Min..Max), its desktop size,
/// and the standard 16:9 sizes that fit inside the desktop (windowed play); all four of these
/// while the display is unknown. Fewer pixels first.
inline std::vector<Size> Choices(std::span<const Size> modes, Size desktop) {
    std::vector<Size> list;
    const auto add = [&](Size s) {
        if (s.width < Min.width || s.height < Min.height || s.width > Max.width ||
            s.height > Max.height || std::find(list.begin(), list.end(), s) != list.end()) {
            return;
        }
        list.push_back(s);
    };
    for (const Size& m : modes) add(m);
    if (desktop.Valid()) add(desktop);
    for (const Size& s : Standard) {
        if (!desktop.Valid() || (s.width <= desktop.width && s.height <= desktop.height)) add(s);
    }
    std::sort(list.begin(), list.end(), [](Size a, Size b) {
        const std::int64_t pa = std::int64_t(a.width) * a.height, pb = std::int64_t(b.width) * b.height;
        return pa != pb ? pa < pb : a.width < b.width;
    });
    return list;
}

/// The choice in `list` for `s`: itself, else the one nearest in height, then width (-1: empty).
inline int Nearest(std::span<const Size> list, Size s) {
    int best = -1;
    for (int i = 0; i < int(list.size()); ++i) {
        if (list[i] == s) return i;
        const auto cost = [&](Size c) {
            return std::int64_t(std::abs(c.height - s.height)) * 100000 + std::abs(c.width - s.width);
        };
        if (best < 0 || cost(list[i]) < cost(list[best])) best = i;
    }
    return best;
}

/// The aspect ratio's name in the menus: "21:9" for 2560x1080, 3440x1440 and 3840x1600 alike.
inline const char* AspectName(Size s) {
    if (!s.Valid()) return "";
    struct Named {
        double ratio;
        const char* name;
    };
    static constexpr Named names[] = {{4.0 / 3.0, "4:3"},   {5.0 / 4.0, "5:4"},    {16.0 / 10.0, "16:10"},
                                      {16.0 / 9.0, "16:9"}, {64.0 / 27.0, "21:9"}, {32.0 / 9.0, "32:9"}};
    const double ratio = double(s.width) / double(s.height);
    const Named* best = &names[0];
    for (const Named& n : names) {
        if (std::abs(std::log(ratio / n.ratio)) < std::abs(std::log(ratio / best->ratio))) best = &n;
    }
    // 3840x1600 (12:5) and 3440x1440 (43:18) are 21:9 monitors too; far from all of these: none.
    return std::abs(std::log(ratio / best->ratio)) < 0.06 ? best->name : "";
}

} // namespace BbOutputSizes
