// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

// Party portraits (Kaniel 2026-10-13): the party list shows each member's head and shoulders, as
// its own game renders them (outfit and all), with its state icon as a small badge. Each game
// takes its character's portrait from the status page's model (see StatusModelWidth; the port's
// texture cache hands it over as the page samples it), keeps it on disk by the character's face
// data, and sends it to the party's games in
// PortraitPartType frames. The port shows a member's in the party HUD's CoopPartyFace slot
// (bbport/scripts/hud_patch.py), whose placeholder texture its texture cache fills.

#pragma once

#include <array>
#include <filesystem>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "common/types.h"
#include "core/bloodborne_party.h"

namespace Core::Bloodborne {

// A portrait: PortraitSide square, RGBA rows, opaque.
constexpr int PortraitSide = 64;
constexpr size_t PortraitBytes = size_t{PortraitSide} * PortraitSide * 4;
using PortraitPixels = std::vector<u8>;

// The status page's model target (SYSTEX_MENU_MDL, StatusModelWidth x StatusModelHeight: the whole
// character in its outfit, lit, on a transparent ground) and the square of it a portrait shows:
// the head and shoulders, StatusModelCropSide wide, from StatusModelCropAbove rows above the top
// of the head, the head (its first StatusModelHeadRows rows) in the middle. Measured on the rig's
// 2026-10-13 capture: the hood's top at y 4, its middle at x 274, the collar at about 150.
// (The page's face target, SYSTEX_Menu_StatusFace, is the bare head: no hat, no collar.)
constexpr int StatusModelWidth = 560;
constexpr int StatusModelHeight = 768;
constexpr int StatusModelCropSide = 152;
constexpr int StatusModelCropAbove = 4;
constexpr int StatusModelHeadRows = 60;
struct PortraitCrop {
    int x, y, side;
};
// What shows where the model page is transparent (a dark ground, as the save list's faces have).
constexpr std::array<u8, 3> PortraitBackdrop{0x16, 0x13, 0x11};
// The square of a model target's pixels (RGBA rows) the portrait shows; nothing when no figure
// is on it.
std::optional<PortraitCrop> HeadAndShoulders(std::span<const u8> rgba, int width, int height);
// The portrait of a model target's pixels (RGBA rows, `width` x `height`); nothing for another
// target or one with too little of a character on it yet.
std::optional<PortraitPixels> PortraitFromModel(std::span<const u8> rgba, int width, int height);

// A portrait on the co-op link: RGB565 in PortraitPartCount parts of PortraitPartBytes, each
// naming the member (its board name) and the picture (PortraitId). CoopFrameMax is the link's
// frame size (P2PCoopSendTo, the receive buffer in TakeDirectDamage).
constexpr u8 PortraitPartType = 0x3E;
constexpr size_t PortraitPartBytes = 1024;
constexpr size_t PortraitPartCount = size_t{PortraitSide} * PortraitSide * 2 / PortraitPartBytes;
constexpr size_t CoopFrameMax = 1400;
struct PortraitPart {
    std::string member;
    u32 id = 0;
    u8 part = 0;
    u8 parts = 0;
    std::vector<u8> bytes;
    bool operator==(const PortraitPart&) const = default;
};
u32 PortraitId(std::span<const u8> pixels);
std::vector<PortraitPart> SplitPortrait(std::string_view member, const PortraitPixels& pixels);
// The payload: member (u8 length first), id (u32), part, parts, then PortraitPartBytes.
std::vector<u8> EncodePortraitPart(const PortraitPart& part);
std::optional<PortraitPart> DecodePortraitPart(std::span<const u8> payload);
// A member's parts until its picture is whole. The parts of a member's PicturesKept latest
// pictures are kept apart (a late part of the one before is no loss); an older one's go.
class PortraitAssembler {
public:
    static constexpr size_t PicturesKept = 2;
    // The member's picture, once this part made it whole (once per picture).
    std::optional<PortraitPixels> Take(const PortraitPart& part);

private:
    struct Pending {
        u32 id = 0;
        std::vector<u8> rgb565;
        u32 have = 0; // a bit per part
        bool made = false;
        u64 touched = 0;
    };
    std::map<std::string, std::vector<Pending>, std::less<>> by_member;
    u64 touches = 0;
};

// The members' portraits and what the party list's slots show (CoopPartyFace0-2): a slot's version
// changes whenever its picture does, so the port refreshes its texture then only.
constexpr int PortraitSlotCount = 3;
class PartyPortraits {
public:
    void Take(std::string member, PortraitPixels pixels);
    bool Has(std::string_view member) const;
    // The slot shows `member` ("" for nobody): whether it shows a portrait.
    bool Show(int slot, std::string_view member);
    u32 Version(int slot) const;
    std::optional<PortraitPixels> Pixels(int slot) const;
    // The members still in the party: the others' pictures go.
    void KeepOnly(std::span<const std::string> members);

private:
    struct Picture {
        u32 id = 0;
        PortraitPixels pixels;
    };
    std::map<std::string, Picture, std::less<>> by_member;
    std::array<std::string, PortraitSlotCount> shown{};
    std::array<u32, PortraitSlotCount> shown_id{};
    std::array<u32, PortraitSlotCount> versions{};
};

// The captures of the status page's model while it shows (one now and then): the last one becomes
// the portrait once none came for SettleMs (the page closed).
class FaceCapture {
public:
    static constexpr s64 SettleMs = 1'500;
    void Take(PortraitPixels pixels, s64 now_ms);
    std::optional<PortraitPixels> Settled(s64 now_ms);

private:
    std::optional<PortraitPixels> last;
    s64 at = 0;
};

// When this game sends its portrait (`id`, 0 for none) to its room peers (sorted): at once when it
// changed or a peer came, and again every ResendMs (a lost part).
class PortraitSender {
public:
    static constexpr s64 ResendMs = 15'000;
    bool Due(u32 id, std::span<const u64> peers, s64 now_ms);

private:
    u32 sent_id = 0;
    std::vector<u64> sent_peers;
    s64 sent_at = 0;
};

// The character's portrait on disk (user/portraits/<face fingerprint>.rgba, PortraitBytes).
std::filesystem::path PortraitCachePath(const std::filesystem::path& dir, u64 fingerprint);
bool SavePortrait(const std::filesystem::path& path, const PortraitPixels& pixels);
std::optional<PortraitPixels> LoadPortrait(const std::filesystem::path& path);

// A list slot's art: the icon beside the bar (CoopPartyIcon), the badge on the portrait
// (CoopPartyBadge) and the portrait itself (CoopPartyFace, PortraitFrame...), frames as
// GotoAndStop takes them.
constexpr u32 PortraitFrameNone = 1;
constexpr u32 PortraitFrameShown = 2;
struct PartyListArt {
    u32 icon = PartyListIconNone;
    u32 badge = PartyListIconNone;
    u32 face = PortraitFrameNone;
    bool operator==(const PartyListArt&) const = default;
};
PartyListArt PartyListArtFor(const PartyListEntry& entry, bool portrait);

} // namespace Core::Bloodborne
