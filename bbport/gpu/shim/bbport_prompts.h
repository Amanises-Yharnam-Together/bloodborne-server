// SPDX-License-Identifier: GPL-2.0-or-later
// bbport: button icons that follow the device in use (bbport.ini button_icons=auto). The game's
// KG_* glyphs and the buttons baked into its menu atlases are found in guest memory as the texture
// cache uploads them; whenever the device in use changes, the window thread writes that device's
// set over them from the prompt sets file run.py names (BB_PROMPT_SETS, bbport_prompt_sets.h;
// drawn by launcher/bbport_icons.py: PlayStation, Xbox, keyboard and mouse, Switch). From
// Supermedo's bbport (615841c, c804508), with our own icons.
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include "bbport_prompt_sets.h"

namespace BbPrompts {

/// True when the icons follow the device (a prompt sets file was loaded).
bool Active();

/// Window thread: an input from the keyboard or mouse (Keyboard), or from a controller of that set.
void OnInput(BbPromptSets::Set set);
/// Window thread: a controller of that set connected. Before any input, the icons are its set.
void OnControllerConnected(BbPromptSets::Set set);

/// Window thread, every event poll: writes the icons that should change (outside every renderer
/// lock: a write to a page the texture cache watches tells it).
void Apply();

/// GPU thread (TextureCache::RefreshImage), before a texture is read from guest memory: remembers
/// the game's button pictures. Never writes guest memory itself.
void OnUpload(std::uintptr_t address, std::size_t size, std::uint32_t width, std::uint32_t height,
              std::uint32_t levels, std::uint32_t bits);

/// The set the icons show now (the overlay draws its own buttons in it).
BbPromptSets::Set Shown();

/// A glyph's BC7 bytes (1024, the PS4's tiled layout) in a set, empty when unknown.
std::span<const std::uint8_t> Glyph(std::string_view name, BbPromptSets::Set set);

} // namespace BbPrompts
