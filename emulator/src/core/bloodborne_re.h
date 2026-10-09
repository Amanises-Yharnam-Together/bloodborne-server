// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace Core::Bloodborne {

std::optional<std::string> GetSeamlessHostPlacementHeader();
bool SetSeamlessHostPlacementHeader(std::string_view value);
void ClearSeamlessHostPlacementHeader();
// Logs the eboot addresses on the current (guest) stack: who in the game called an HLE function.
void LogGameCallChain(const char* what);
void TraceMatching2LeaveRoom(std::uintptr_t return_address, std::uint64_t room_id);
void InstallSeamlessCoopPatches();
void InstallReverseEngineeringTrace();
// Tells the party code which character save slot the game opened last.
void NoteSaveFileOpened(std::string_view guest_path);
// Autoplay (SHADPS4_BLOODBORNE_AUTOPLAY): the buttons and sticks (lx, ly, rx, ry) it holds now;
// false when it is off. Called by the pad reader.
bool AutoplayPad(std::uint32_t& buttons, std::array<std::uint8_t, 4>& sticks);

} // namespace Core::Bloodborne
