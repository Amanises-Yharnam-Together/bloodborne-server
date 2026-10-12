// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

// The co-op site table (core/bloodborne_patch_sites.h): well formed, the source of the patch
// lists the installers write, the same as the copy the launcher reads
// (bbport/patches/coop-sites.json), and the checks and the summary line built on it.

#include <cstdlib>
#include <fstream>
#include <iterator>
#include <set>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "core/bloodborne_party.h"
#include "core/bloodborne_patch_sites.h"

using namespace Core::Bloodborne;

namespace {

std::vector<const CoopSite*> SitesOf(std::string_view feature) {
    std::vector<const CoopSite*> sites;
    for (const CoopSite& site : CoopSites) {
        if (site.feature == feature) {
            sites.push_back(&site);
        }
    }
    return sites;
}

// An eboot image holding every site's original bytes (zeros elsewhere).
std::vector<u8> ImageWithOriginalBytes() {
    u64 end = 0;
    for (const CoopSite& site : CoopSites) {
        end = std::max<u64>(end, site.offset + HexBytes(site.original).size());
    }
    std::vector<u8> image(end);
    for (const CoopSite& site : CoopSites) {
        const auto bytes = HexBytes(site.original);
        std::ranges::copy(bytes, image.begin() + site.offset);
    }
    return image;
}

const CoopSite& Site(std::string_view name) {
    const auto found =
        std::ranges::find_if(CoopSites, [name](const CoopSite& site) { return site.name == name; });
    EXPECT_NE(found, CoopSites.end()) << name;
    return *found;
}

} // namespace

TEST(CoopSites, HexBytesReadPairsAndAnyByte) {
    EXPECT_EQ(HexBytes("0F 87 90"), (std::vector<u8>{0x0F, 0x87, 0x90}));
    EXPECT_EQ(HexBytes(""), std::vector<u8>{});
    EXPECT_EQ(HexBytes("c7 ?? 28"), (std::vector<u8>{0xC7, 0x00, 0x28}));
    EXPECT_TRUE(MatchesHexPattern(std::vector<u8>{0xC7, 0x47, 0x28}, "C7 ?? 28"));
    EXPECT_FALSE(MatchesHexPattern(std::vector<u8>{0xC7, 0x47, 0x29}, "C7 ?? 28"));
    EXPECT_FALSE(MatchesHexPattern(std::vector<u8>{0xC7, 0x47}, "C7 ?? 28"));
}

TEST(CoopSites, EverySiteIsWellFormed) {
    std::set<std::string_view> names;
    for (const CoopSite& site : CoopSites) {
        SCOPED_TRACE(std::string{site.name});
        EXPECT_TRUE(names.insert(site.name).second) << "names are unique";
        EXPECT_NE(FindCoopFeature(site.feature), nullptr) << "a feature of CoopFeatures";
        const auto original = HexBytes(site.original);
        EXPECT_EQ(original.size() * 3 - 1, site.original.size()) << "pairs separated by spaces";
        if (site.kind == CoopSiteKind::Patch) {
            EXPECT_EQ(HexBytes(site.patched).size(), original.size());
            EXPECT_NE(HexBytes(site.patched), original);
        } else {
            EXPECT_TRUE(site.patched.empty());
        }
        if (site.kind == CoopSiteKind::Hook) {
            EXPECT_GE(original.size(), 5u) << "room for a jmp rel32";
        }
        for (const std::string_view variant : AcceptedVariants(site)) {
            EXPECT_EQ(variant.size(), site.original.size()) << variant;
            EXPECT_TRUE(MatchesHexPattern(original, variant)) << "the original is a variant too";
        }
    }
}

TEST(CoopSites, EveryFeatureHasSites) {
    std::set<std::string_view> names;
    for (const CoopFeature& feature : CoopFeatures) {
        EXPECT_TRUE(names.insert(feature.name).second) << feature.name;
        EXPECT_FALSE(SitesOf(feature.name).empty()) << feature.name;
    }
}

// Two writes over the same bytes would undo each other; a function we call may also be hooked.
TEST(CoopSites, NoTwoWritesOverlap) {
    for (const CoopSite& one : CoopSites) {
        for (const CoopSite& other : CoopSites) {
            if (&one == &other || one.kind == CoopSiteKind::Call ||
                other.kind == CoopSiteKind::Call) {
                continue;
            }
            const u64 one_end = one.offset + HexBytes(one.original).size();
            const u64 other_end = other.offset + HexBytes(other.original).size();
            EXPECT_FALSE(one.offset < other_end && other.offset < one_end)
                << one.name << " and " << other.name;
        }
    }
}

TEST(CoopSites, ThePatchListsComeFromTheTable) {
    const std::vector<std::pair<std::string_view, const std::vector<CodePatch>*>> lists{
        {"ghost death", &GhostDeathKeepSessionPatches()},
        {"guest bloodstain", &GuestBloodstainPatches()},
        {"no echo theft", &NoEchoTheftPatches()},
        {"host death", &HostDeathKeepGuestsPatches()},
        {"boss clear", &BossClearKeepSessionPatches()},
        {"join anywhere", &JoinAnywherePatches()},
        {"quiet summon banners", &QuietSummonBannerPatches()},
    };
    for (const auto& [feature, patches] : lists) {
        const auto expected = CoopPatchesOf(feature);
        ASSERT_EQ(patches->size(), expected.size()) << feature;
        ASSERT_FALSE(expected.empty()) << feature;
        for (size_t index = 0; index < expected.size(); ++index) {
            EXPECT_EQ((*patches)[index].offset, expected[index].offset) << feature;
            EXPECT_EQ((*patches)[index].original, expected[index].original) << feature;
            EXPECT_EQ((*patches)[index].patched, expected[index].patched) << feature;
        }
    }
    EXPECT_EQ(GhostDeathKeepSessionPatches().size(), 5u);
    EXPECT_EQ(BossClearKeepSessionPatches().size(), 6u);
    EXPECT_EQ(CoopPatchesOf("seamless core").size(), 13u);
}

// Kaniel 2026-10-13: New Game crashed on GXRenderThread (eboot+0x1F591B0) drawing a Scaleform
// movie player that was already freed: the game frees a released player after 5 frames
// (FD4DeleyDeleteManager, `mov ecx, 5` before 0x00F5D2B0), and the port's render thread fell
// further behind than that. 60 frames.
TEST(CoopSites, AReleasedMoviePlayerOutlivesItsQueuedDraws) {
    const auto patches = CoopPatchesOf("movie player delete delay");
    ASSERT_EQ(patches.size(), 1u);
    EXPECT_EQ(patches[0].offset, 0x01F60837u);
    EXPECT_EQ(patches[0].original, HexBytes("B9 05 00 00 00"));
    EXPECT_EQ(patches[0].patched, HexBytes("B9 3C 00 00 00"));
}

TEST(CoopSites, TheSignTimersAreTheTables) {
    const auto sites = SitesOf("sign timers");
    ASSERT_EQ(sites.size(), SignTimerSites.size());
    for (size_t index = 0; index < sites.size(); ++index) {
        const SignTimerSite& timer = SignTimerSites[index];
        const CoopSite& site = *sites[index];
        EXPECT_EQ(site.offset, timer.offset);
        const auto original = EncodeSignTimerStore(timer.modrm, timer.original_seconds);
        const auto patched = EncodeSignTimerStore(timer.modrm, timer.patched_seconds);
        EXPECT_EQ(HexBytes(site.original), std::vector<u8>(original.begin(), original.end()));
        EXPECT_EQ(HexBytes(site.patched), std::vector<u8>(patched.begin(), patched.end()));
        // "Uncap FPS++" writes 60 s: still the game's store, still ours to patch.
        const auto uncapped = EncodeSignTimerStore(timer.modrm, 60.0f);
        EXPECT_TRUE(MatchesCoopSite(uncapped, site));
        EXPECT_TRUE(IsSignTimerStore(uncapped, timer));
    }
}

TEST(CoopSites, TheEchoRatePatchesAreTheRetargetedLoads) {
    constexpr u64 FullEchoRate = 0x04927EBC;
    const auto sites = SitesOf("full echoes");
    ASSERT_EQ(sites.size(), 3u);
    for (const CoopSite* site : sites) {
        const auto original = HexBytes(site->original);
        ASSERT_EQ(original.size(), 8u);
        const auto patched =
            RetargetRipLoad(std::span<const u8, 8>{original.data(), 8}, site->offset, FullEchoRate);
        EXPECT_EQ(HexBytes(site->patched), std::vector<u8>(patched.begin(), patched.end()))
            << site->name;
    }
}

TEST(CoopSites, TheSummonSlotHoldsAreRetargetedCompares) {
    const auto sites = SitesOf("summon slot hold");
    ASSERT_EQ(sites.size(), 2u);
    for (const CoopSite* site : sites) {
        const auto original = HexBytes(site->original);
        ASSERT_EQ(original.size(), 8u);
        const auto patched = RetargetRipLoad(std::span<const u8, 8>{original.data(), 8},
                                             site->offset, PartySummonSlotHoldConstant);
        EXPECT_EQ(HexBytes(site->patched), std::vector<u8>(patched.begin(), patched.end()))
            << site->name;
    }
}

// The hook sites whose bytes live in bloodborne_party.h are the table's.
TEST(CoopSites, ThePartyHeaderHooksAreRegistered) {
    for (const DeathFlagSite& site : DeathFlagSites()) {
        EXPECT_TRUE(IsRegisteredCoopHook(site.offset, site.instruction)) << site.name;
    }
    EXPECT_TRUE(IsRegisteredCoopHook(HitOffset, HitPrologue));
    EXPECT_TRUE(IsRegisteredCoopHook(HitAppliedOffset, HitApplied));
    for (const u64 offset : EffectHpChangeOffsets) {
        EXPECT_TRUE(IsRegisteredCoopHook(offset, EffectHpChange));
    }
    EXPECT_TRUE(IsRegisteredCoopHook(IsNoDeadExitOffset, IsNoDeadExit));
    // The party HUD's and the downed player's hooks (Lisara's log 2026-10-12: three "not in the
    // co-op site table" errors, so the launcher could not warn about a game patch over them).
    EXPECT_TRUE(IsRegisteredCoopHook(HudTagUpdateExitOffset, HudTagUpdateExit));
    EXPECT_TRUE(IsRegisteredCoopHook(HudTicksTagsDoneOffset, HudTicksTagsDone));
    EXPECT_TRUE(IsRegisteredCoopHook(HitDamageStoredOffset, HitDamageStored));
    EXPECT_TRUE(IsRegisteredCoopHook(HitReactionOffset, HitReactionPrologue));
    EXPECT_TRUE(IsRegisteredCoopHook(PartyGhostDeathMenuBranchOffset - 5,
                                     std::vector<u8>{0x41, 0x80, 0x7E, 0x0C, 0x00}));
    // Other bytes, another length or an offset that is no hook are not.
    EXPECT_FALSE(IsRegisteredCoopHook(HitOffset, std::span{HitPrologue}.first(6)));
    EXPECT_FALSE(IsRegisteredCoopHook(HitOffset + 1, HitPrologue));
    EXPECT_FALSE(IsRegisteredCoopHook(0x0157F8C8, HexBytes("0F 87 90 00 00 00")));
}

TEST(CoopSites, AFeatureMatchesOnlyWithEverySiteInPlace) {
    auto image = ImageWithOriginalBytes();
    for (const CoopFeature& feature : CoopFeatures) {
        EXPECT_EQ(FirstMismatchedCoopSite(image, feature.name), nullptr) << feature.name;
    }
    // Another game patch's re-post time is accepted.
    const CoopSite& repost = Site("sign re-post time");
    image[repost.offset + 5] = 0x70; // 30.0f -> 60.0f
    EXPECT_EQ(FirstMismatchedCoopSite(image, "sign timers"), nullptr);
    // Another instruction there is not.
    image[repost.offset + 1] = 0x47;
    EXPECT_EQ(FirstMismatchedCoopSite(image, "sign timers"), &repost);
    // Nor are other bytes at a site without variants.
    const CoopSite& branch = Site("BlockClear2_3 warp home");
    image[branch.offset] = 0xEB;
    EXPECT_EQ(FirstMismatchedCoopSite(image, "boss clear"), &branch);
    // A site past the end of the image cannot match.
    image.resize(Site("chime banner for others").offset + 2);
    EXPECT_EQ(FirstMismatchedCoopSite(image, "quiet summon banners"),
              &Site("chime banner for others"));
}

TEST(CoopSites, TheSummonCoreIsRequired) {
    for (const std::string_view name :
         {"seamless core", "ghost death", "host death", "boss clear", "join anywhere",
          "host leaves", "seamless travel", "stop request", "sign timers"}) {
        ASSERT_NE(FindCoopFeature(name), nullptr) << name;
        EXPECT_TRUE(FindCoopFeature(name)->required) << name;
    }
    EXPECT_FALSE(FindCoopFeature("item button diagnostics")->required);
    EXPECT_EQ(FindCoopFeature("no such feature"), nullptr);
}

TEST(CoopFeatureReport, SummarizesInOneLine) {
    CoopFeatureReport report;
    EXPECT_EQ(report.Summary(), "Co-op features: 0/0 on");
    report.On("seamless core");
    report.On("ghost death");
    EXPECT_EQ(report.Summary(), "Co-op features: 2/2 on");
    report.Skipped("sign timers", "signature mismatch at 0x18a9757");
    report.Skipped("lamps", "hook failed at 0x204232e");
    EXPECT_EQ(report.Summary(), "Co-op features: 2/4 on (skipped: sign timers [signature "
                                "mismatch at 0x18a9757], lamps [hook failed at 0x204232e])");
    EXPECT_EQ(report.SkippedRequired(), std::vector<std::string>{"sign timers"});
}

TEST(CoopFeatureReport, TheLastWordOnAFeatureCounts) {
    CoopFeatureReport report;
    report.Skipped("shared loot", "hook failed at 0x17dc3cf");
    report.On("shared loot");
    report.On("seamless core");
    report.Skipped("seamless core", "hook failed at 0x12f836e; rolled back");
    EXPECT_EQ(report.Summary(), "Co-op features: 1/2 on (skipped: seamless core [hook failed at "
                                "0x12f836e; rolled back])");
}

TEST(CoopSites, MatchTheCommittedJson) {
    const std::string json = CoopSitesJson();
    const char* write = std::getenv("BB_WRITE_COOP_SITES");
    if (write != nullptr && std::string_view{write} == "1") {
        std::ofstream{BLOODBORNE_COOP_SITES, std::ios::binary} << json;
    }
    std::ifstream file(BLOODBORNE_COOP_SITES, std::ios::binary);
    ASSERT_TRUE(file) << BLOODBORNE_COOP_SITES;
    std::string committed{std::istreambuf_iterator<char>{file}, {}};
    // A Windows checkout may turn the file's line ends into CRLF (jj eol-conversion).
    std::erase(committed, '\r');
    EXPECT_EQ(committed, json) << "run this test with BB_WRITE_COOP_SITES=1 to update "
                               << BLOODBORNE_COOP_SITES;
}

TEST(CoopSites, TheJsonNamesEverySite) {
    const std::string json = CoopSitesJson();
    EXPECT_NE(json.find(R"({"name": "sign re-post time", "feature": "sign timers", )"
                        R"("kind": "patch", "offset": "0x018A9757", )"
                        R"("original": "C7 43 28 00 00 F0 41", )"
                        R"("patched": "C7 43 28 00 00 80 3F", )"
                        R"("accepted": ["C7 43 28 ?? ?? ?? ??"]})"),
              std::string::npos);
    EXPECT_NE(json.find(R"({"name": "sign timers", "required": true})"), std::string::npos);
    EXPECT_NE(json.find(R"("kind": "hook")"), std::string::npos);
    EXPECT_NE(json.find(R"("kind": "call")"), std::string::npos);
}
