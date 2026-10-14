// SPDX-License-Identifier: GPL-2.0-or-later
// bbport: the port's settings as pages of the game's own System menu ("Display", "Game effects",
// "Game patches" after "Screen/Sound"), built and drawn by the game's menu code: rows are the
// game's list, toggle and slider rows, texts come from a hook on its message lookup, the values live
// in the port and are applied as the player changes them. BB_GAME_MENU=0 leaves the menu as it is.
#pragma once

#include <cstdint>

namespace BbGameMenu {
/// Installs the hooks (the loader calls it once, before any game code runs, while the image is
/// still writable). Each hooked function is checked byte for byte against the 1.09 eboot.
void PatchImage(unsigned char* image, std::uint64_t size);

/// Applies what the player changed on the port's pages (each presented frame).
void Poll();

// For the port's other game menus (bbport_trade_menu.cpp), on the game's thread.
std::uint64_t ImageBase();
/// A text the game looks up like its own messages (any category), both languages; before the game
/// runs (PatchImage).
std::uint32_t AddGameText(const char* english, const char* russian);
/// Slot `slot` (0-7) now says `utf8`: its message id.
std::uint32_t SetDynamicText(int slot, const char* utf8);
/// Frees a text entry (0x40 bytes, MakeText's) and a std::function (0x28 bytes) as the game does.
void DestroyGameText(unsigned char* text);
void DestroyGameFunction(unsigned char* function);
} // namespace BbGameMenu
