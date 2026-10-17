// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace Core::Bloodborne {

struct PartyPanel;
struct CompassView;

std::optional<std::string> GetSeamlessHostPlacementHeader();
bool SetSeamlessHostPlacementHeader(std::string_view value);
void ClearSeamlessHostPlacementHeader();
// The server handed this game a summon (its claim on our sign). Called by the HTTP worker.
void NoteSummonDelivered();
// The game posted a summon search (/summon_messenger/get). Called by the HTTP layer as it sends.
void NoteSummonSearchPosted();
// Logs the eboot addresses on the current (guest) stack: who in the game called an HLE function.
void LogGameCallChain(const char* what);
void TraceMatching2LeaveRoom(std::uintptr_t return_address, std::uint64_t room_id);
void InstallSeamlessCoopPatches();
void InstallReverseEngineeringTrace();
// This build's revision and its event files' hash (EventFilesHash, 0 unknown), for the hello the
// game sends its room peers (PeerHelloType). Before it is called no hello goes out.
void SetBuildIdentity(std::string_view revision, std::uint64_t event_files);
// Tells the party code which character save slot the game opened last.
void NoteSaveFileOpened(std::string_view guest_path);
// Autoplay (SHADPS4_BLOODBORNE_AUTOPLAY): the buttons and sticks (lx, ly, rx, ry) it holds now;
// false when it is off. Called by the pad reader.
bool AutoplayPad(std::uint32_t& buttons, std::array<std::uint8_t, 4>& sticks);
// Party death (SHADPS4_BLOODBORNE_PARTY_DEATH): sees the pad (holding Circle to give up, attacks
// that revive a downed member) and, while the player is down, lets go of all but Options and the
// camera stick. Called by the pad reader on what the game will read (autoplay's buttons in).
void FilterPartyDeathPad(std::uint32_t& buttons, std::array<std::uint8_t, 4>& sticks,
                         std::uint8_t& l2, std::uint8_t& r2);
// Party death's panel (bloodborne_party.h) as of the last party tick, for the port's overlay:
// false when there is nothing to draw (no party session, nobody else on the board, not down).
bool ReadPartyPanel(PartyPanel& panel);
// The party HUD's own party list drew members within the last half second (the overlay leaves its
// list out then).
bool NativePartyListShown();
// The party HUD's prompt (CoopPrompt) showed party death's prompt within the last half second (the
// overlay then draws only its button glyph).
bool NativePartyPromptShown();
// The game's HUD ticked within the last half second (the port's party HUD): not in a load or a
// cutscene. Always true without the party HUD.
bool GameHudUp();
// Kaniel 2026-10-17: the teammate compass as of the last party tick (bloodborne_compass.h); false
// with `view.why` when it shows nothing.
bool ReadCompass(CompassView& view);
// Party portraits (bloodborne_portrait.h). The status page's model (its render target's RGBA
// rows) as the port's texture cache read it back: this character's portrait once the page closes.
void NoteStatusModel(std::span<const std::uint8_t> rgba, int width, int height);
// The off-screen body renderer has drawn this character (TickPortraits): the port reads its
// 560x768 target back when it is drawn, not only when the status page samples it.
bool OffscreenPortraitWanted();
// The party list slot's picture (PortraitBytes into `rgba`; false when it shows none) and its
// version, which changes whenever the picture does.
bool ReadPortraitSlot(int slot, std::span<std::uint8_t> rgba, std::uint32_t& version);
std::uint32_t PortraitSlotVersion(int slot);

} // namespace Core::Bloodborne
