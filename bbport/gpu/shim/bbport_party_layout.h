// SPDX-License-Identifier: GPL-2.0-or-later
// bbport co-op: where party death's HUD goes on screen (bbport_overlay.cpp Party::), apart from
// ImGui so a test can hold it clear of the game's own HUD. Places follow the game's frame inside
// the window (fitted whole and centred, as the presenter blits it), because the game's HUD moves
// with that frame when the window has another shape. `scale` is the overlay's (1 at 1080p).

#pragma once

#include <algorithm>
#include <cmath>

namespace BbPartyLayout {

/// The game's frame in display pixels.
struct Rect {
    float x, y, w, h;
};

/// The overlay's scale for a display this tall: 1 at 1080p, never under 0.75.
inline float Scale(float display_h) {
    return std::max(display_h / 1080.0f, 0.75f);
}

/// The game's own HUD, as fractions of its frame's height (measured on the rig's 1080p pictures):
/// the top-left quick items and their counts end about here...
inline constexpr float GameQuickItemsBottom = 225.0f / 1080.0f;
/// ...and the game's notices in the middle ("Entered the world of ...") take about these lines.
inline constexpr float GameNoticeTop = 734.0f / 1080.0f;
inline constexpr float GameNoticeBottom = 780.0f / 1080.0f;

/// The party list: its top left corner.
inline float ListLeft(const Rect& game, float scale) {
    return game.x + 48.0f * scale;
}
inline float ListTop(const Rect& game) {
    return std::round(game.y + game.h * 0.225f);
}

/// The downed player's own screen, from its revive ring's centre down to the give-up bar: the
/// ring's radius, then each line's offset below the centre (times `scale`).
inline constexpr float DownRingRadius = 40.0f;
inline constexpr float DownTitleBelow = 54.0f;
inline constexpr float DownHintBelow = 94.0f;
inline constexpr float DownGiveUpBelow = 128.0f;
inline constexpr float DownBarBelow = 152.0f;
inline constexpr float DownBarHeight = 4.0f;

/// The ring's centre, so that the give-up bar ends well above the game's notices.
inline float DownRingY(const Rect& game, float scale) {
    return std::round(game.y + game.h * 0.64f - (DownBarBelow + DownBarHeight) * scale);
}

/// The prompt to strike a downed member: low in the middle, as the game's own prompts.
inline float RevivePromptY(const Rect& game) {
    return std::round(game.y + game.h * 0.74f);
}

} // namespace BbPartyLayout
