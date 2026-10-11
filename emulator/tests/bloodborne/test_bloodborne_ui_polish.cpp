// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <cstring>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include "core/bloodborne_party.h"

using namespace Core::Bloodborne;
using namespace std::string_view_literals;

namespace {

// A loaded PARAM file as the game keeps it (see test_bloodborne_harder_enemies.cpp).
std::vector<u8> MakeParam(std::string_view type, std::size_t row_size,
                          const std::vector<s32>& ids) {
    const std::size_t data = 0x40 + ids.size() * 24;
    std::vector<u8> param(data + ids.size() * row_size);
    const u16 rows = static_cast<u16>(ids.size());
    std::memcpy(param.data() + 0x0A, &rows, sizeof(rows));
    std::memcpy(param.data() + 0x0C, type.data(), type.size());
    for (std::size_t row = 0; row < ids.size(); ++row) {
        const u64 offset = data + row * row_size;
        std::memcpy(param.data() + 0x40 + row * 24, &ids[row], sizeof(s32));
        std::memcpy(param.data() + 0x40 + row * 24 + 8, &offset, sizeof(offset));
    }
    return param;
}

s32 IconOf(const std::vector<u8>& param, std::size_t row) {
    constexpr std::size_t RowSize = 0x210;
    const std::size_t data = 0x40 + (param.size() - 0x40) / (24 + RowSize) * 24;
    s32 icon = 0;
    std::memcpy(&icon, param.data() + data + row * RowSize + SpEffectIconOffset, sizeof(icon));
    return icon;
}

void SetIcon(std::vector<u8>& param, std::size_t row, s32 icon) {
    constexpr std::size_t RowSize = 0x210;
    const std::size_t data = 0x40 + (param.size() - 0x40) / (24 + RowSize) * 24;
    std::memcpy(param.data() + data + row * RowSize + SpEffectIconOffset, &icon, sizeof(icon));
}

} // namespace

// The name plates say who is who in the party: Host and Guest instead of the bell's roles.
TEST(BloodborneUiPolish, NamePlatesSayHostAndGuest) {
    EXPECT_EQ(PolishedText(EventTextCategory, 107000, u"Beckoner"), u"Host"sv);
    EXPECT_EQ(PolishedText(EventTextCategory, 107100, u"Cooperator"), u"Guest"sv);
}

TEST(BloodborneUiPolish, JoinBannersAreShort) {
    EXPECT_EQ(
        PolishedText(EventTextCategory, 102001, u"The bell's resonance has beckoned <?joinName?>."),
        u"<?joinName?> joined."sv);
    EXPECT_EQ(PolishedText(EventTextCategory, 102100,
                           u"Entered the world of <?hostName?>,\nwho beckoned you here."),
              u"Joined <?hostName?>."sv);
}

// Text the game holds in another language, or already changed, stays as it is.
TEST(BloodborneUiPolish, OnlyTheEnglishOriginalChanges) {
    EXPECT_EQ(PolishedText(EventTextCategory, 107000, u"Rufer"), std::nullopt);
    EXPECT_EQ(PolishedText(EventTextCategory, 107000, u"Host"), std::nullopt);
    EXPECT_EQ(PolishedText(EventTextCategory, 107001, u"Beckoner"), std::nullopt);
    EXPECT_EQ(PolishedText(InGameMenuCategory, 107000, u"Beckoner"), std::nullopt);
}

// Each replacement is written over the original in the loaded message file, so it must fit, and
// a message's tags (the names the game fills in) must survive.
TEST(BloodborneUiPolish, EveryReplacementFitsAndKeepsItsTags) {
    ASSERT_FALSE(PolishedMessages().empty());
    for (const PolishedMessage& message : PolishedMessages()) {
        EXPECT_LE(message.replacement.size(), message.original.size())
            << message.category << ":" << message.id;
        EXPECT_FALSE(message.replacement.empty()) << message.category << ":" << message.id;
        for (const std::u16string_view tag :
             {u"<?joinName?>"sv, u"<?hostName?>"sv, u"<?deadName?>"sv, u"<?leaveName?>"sv,
              u"<?npcNameId@500000?>"sv}) {
            EXPECT_EQ(message.original.find(tag) != std::u16string_view::npos,
                      message.replacement.find(tag) != std::u16string_view::npos)
                << message.category << ":" << message.id;
        }
        EXPECT_EQ(PolishedText(message.category, message.id, message.original),
                  message.replacement);
    }
}

// No "beckoner", "cooperator" or "phantom" is left in a changed message.
TEST(BloodborneUiPolish, NoBellRoleWordsAreLeft) {
    for (const PolishedMessage& message : PolishedMessages()) {
        std::string lower;
        for (const char16_t c : message.replacement) {
            lower.push_back(static_cast<char>(c >= u'A' && c <= u'Z' ? c - u'A' + u'a' : c));
        }
        for (const std::string_view word : {"beckon"sv, "cooperator"sv, "phantom"sv}) {
            EXPECT_EQ(lower.find(word), std::string::npos) << message.category << ":" << message.id;
        }
    }
}

// The bell indicators: the co-op bells' status icons are gone, the effects stay.
TEST(BloodborneUiPolish, RemovesTheCoopBellIcons) {
    auto param = MakeParam("SP_EFFECT_PARAM_ST", 0x210, {9000, 9003, 9004, 9006, 9008, 9009, 9028});
    const std::vector<s32> icons{-1, 8801, 8813, -1, 8802, 8814, 8803};
    for (std::size_t row = 0; row < icons.size(); ++row) {
        SetIcon(param, row, icons[row]);
    }
    EXPECT_EQ(RemoveBellIcons(param), 4u);
    for (const std::size_t row : {1u, 2u, 4u, 5u}) {
        EXPECT_EQ(IconOf(param, row), -1) << row;
    }
    EXPECT_EQ(IconOf(param, 6), 8803) << "the Sinister Bell is not a party bell";
    EXPECT_EQ(RemoveBellIcons(param), 0u) << "already done";
    auto npc = MakeParam("NPC_PARAM_ST", 0x210, {9003});
    SetIcon(npc, 0, 8801);
    EXPECT_EQ(RemoveBellIcons(npc), 0u);
    EXPECT_EQ(IconOf(npc, 0), 8801);
}

// "Discovered the chime of a ..." comes right before the join banner; both of its calls go.
TEST(BloodborneUiPolish, TheChimeBannerCallsBecomeNops) {
    const auto& patches = QuietSummonBannerPatches();
    ASSERT_EQ(patches.size(), 2u);
    for (const CodePatch& patch : patches) {
        EXPECT_EQ(DecodeNearCallTarget(patch.offset, patch.original), ShowNetworkBannerOffset)
            << std::hex << patch.offset;
        EXPECT_EQ(patch.patched, (std::vector<u8>{0x0F, 0x1F, 0x44, 0x00, 0x00}));
    }
}

// The boss-death tracer (Kaniel 2026-10-11): every place the game sets a character's dead flag,
// each with the register that holds the character there.
TEST(BloodborneDeathTracer, SitesSetTheDeadFlag) {
    const auto& sites = DeathFlagSites();
    ASSERT_EQ(sites.size(), 3u);
    for (const DeathFlagSite& site : sites) {
        // `or byte ptr [reg + 0x1E3], 1`, with or without a REX prefix.
        const std::size_t rex = site.instruction.size() == 8 ? 1 : 0;
        ASSERT_EQ(site.instruction.size(), 7 + rex) << std::hex << site.offset;
        EXPECT_EQ(site.instruction[rex], 0x80);
        EXPECT_EQ(site.instruction[rex + 2], 0xE3);
        EXPECT_EQ(site.instruction[rex + 3], 0x01);
        EXPECT_EQ(site.instruction.back(), 0x01);
    }
}

// Big characters only (bosses and the like): an ordinary kill would flood the log.
TEST(BloodborneDeathTracer, TracesBigCharactersOnly) {
    EXPECT_TRUE(TracesDeathOf(2031));
    EXPECT_TRUE(TracesDeathOf(DeathTraceMinMaxHp));
    EXPECT_FALSE(TracesDeathOf(DeathTraceMinMaxHp - 1));
    EXPECT_FALSE(TracesDeathOf(200));
}
