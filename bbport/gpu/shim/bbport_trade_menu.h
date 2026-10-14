// SPDX-License-Identifier: GPL-2.0-or-later
// bbport: trading's menus, the game's own (the co-op code's bloodborne_trade.h does the trades):
// a "Give" row in the inventory's item commands for what may be traded, the game's quantity
// picker, a yes/no box for an offer to this player and a box for what became of an offer.
#pragma once

#include <cstdint>

namespace BbTradeMenu {
/// Hooks the inventory's item commands (bytes checked against the 1.09 eboot) and registers the
/// menus' tick with the co-op code. `stubs` is executable-to-be memory within 2 GiB of the image,
/// advanced past what it takes. Called by BbGameMenu::PatchImage once its text hook is in.
bool PatchImage(unsigned char* image, std::uint64_t size, std::uint8_t*& stubs);
} // namespace BbTradeMenu
