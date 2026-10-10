// SPDX-License-Identifier: GPL-2.0-or-later
// bbport co-op: party death's HUD keeps clear of the game's own HUD (bbport_party_layout.h): at
// 1080p and 4K, in a small window (where the overlay's scale stops at 0.75) and with the game's
// frame letterboxed or pillarboxed.
#include <cassert>
#include <cstdio>
#include "gpu/shim/bbport_party_layout.h"

using namespace BbPartyLayout;

namespace {

/// The bottom of the downed player's own screen: the give-up bar's lower edge.
float DownBottom(const Rect& game, float scale) {
    return DownRingY(game, scale) + (DownBarBelow + DownBarHeight) * scale;
}

void ClearOfTheGamesHud(const Rect& game, float scale) {
    // The list starts below the game's quick items and inside the game's frame, not in a bar.
    assert(ListTop(game) >= game.y + game.h * GameQuickItemsBottom + 5.0f * scale);
    assert(ListLeft(game, scale) >= game.x);
    // The downed screen ends above the game's notices, with its ring below the list's top.
    assert(DownBottom(game, scale) <= game.y + game.h * GameNoticeTop);
    assert(DownRingY(game, scale) - DownRingRadius * scale >= ListTop(game));
    // The revive prompt (a band from 10 px above its line) stays below the notices.
    assert(RevivePromptY(game) - 10.0f * scale >= game.y + game.h * GameNoticeBottom);
}

} // namespace

int main() {
    ClearOfTheGamesHud({0, 0, 1920, 1080}, Scale(1080));
    ClearOfTheGamesHud({0, 0, 3840, 2160}, Scale(2160));
    ClearOfTheGamesHud({0, 0, 1280, 720}, Scale(720));
    ClearOfTheGamesHud({0, 0, 960, 540}, Scale(540));
    // 16:10: 60 px bars above and below the frame; everything moves down with the game's HUD.
    ClearOfTheGamesHud({0, 60, 1920, 1080}, Scale(1200));
    assert(ListTop({0, 60, 1920, 1080}) == 60.0f + ListTop({0, 0, 1920, 1080}));
    assert(DownRingY({0, 60, 1920, 1080}, 1.0f) == 60.0f + DownRingY({0, 0, 1920, 1080}, 1.0f));
    // 21:9: bars left and right; the list moves in with the game's HUD.
    ClearOfTheGamesHud({440, 0, 2560, 1440}, Scale(1440));
    assert(ListLeft({440, 0, 2560, 1440}, 1.0f) == 440.0f + ListLeft({0, 0, 2560, 1440}, 1.0f));
    std::puts("Party layout: PASS (1080p, 4K, 720p, small window, letterboxed, pillarboxed)");
}
