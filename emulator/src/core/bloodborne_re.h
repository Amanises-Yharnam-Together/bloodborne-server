// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

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

} // namespace Core::Bloodborne
