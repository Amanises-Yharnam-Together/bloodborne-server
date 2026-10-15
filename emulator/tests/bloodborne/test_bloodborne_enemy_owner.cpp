// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

// Phase 4 (docs/SERVER_COOP_PLAN.md): the server names one game per occupied area that runs its
// enemies; the others follow it. What each game does with that: its role, the state the game's
// host-only checks see, the ownership words and what happens to the enemy packets.

#include <optional>
#include <vector>

#include <gtest/gtest.h>

#include "core/bloodborne_party.h"

using namespace Core::Bloodborne;

namespace {

constexpr u32 CentralYharnam = 0x18010000;
constexpr u32 CathedralWard = 0x18000000;
constexpr u32 ForbiddenWoods = 0x15000000;
constexpr s32 Hosting = 3;
constexpr s32 Guest = 6;

const std::vector<EnemyOwnerEntry> Owners{{CentralYharnam, "aa01", 4}, {ForbiddenWoods, "bb02", 7}};

} // namespace

TEST(EnemyOwner, AnOwnerRunsOneMap) {
    // Not an area: the game wakes a block's enemies only near a player in that world.
    EXPECT_EQ(EnemyOwnerArea(CentralYharnam), CentralYharnam);
    EXPECT_EQ(EnemyOwnerArea(CathedralWard), CathedralWard);
    EXPECT_EQ(EnemyOwnerArea(0), 0u);
}

TEST(EnemyOwner, TheServersOwnerOfTheAreaDecidesTheRole) {
    EXPECT_EQ(EnemyRoleOn(Owners, "aa01", CentralYharnam), EnemyRole::Owner);
    EXPECT_EQ(EnemyRoleOn(Owners, "bb02", CentralYharnam), EnemyRole::Follower);
    EXPECT_EQ(EnemyRoleOn(Owners, "aa01", ForbiddenWoods), EnemyRole::Follower);
    // The other block of the area is another map: nobody runs it here.
    EXPECT_EQ(EnemyRoleOn(Owners, "aa01", CathedralWard), EnemyRole::Unknown);
    // Not in the world, an area nobody runs, no name yet, no owners heard: the game's own way.
    EXPECT_EQ(EnemyRoleOn(Owners, "aa01", 0), EnemyRole::Unknown);
    EXPECT_EQ(EnemyRoleOn(Owners, "aa01", 0x21000000), EnemyRole::Unknown);
    EXPECT_EQ(EnemyRoleOn(Owners, "", CentralYharnam), EnemyRole::Unknown);
    EXPECT_EQ(EnemyRoleOn({}, "aa01", CentralYharnam), EnemyRole::Unknown);
    const EnemyOwnerEntry* owner = EnemyOwnerOf(Owners, CentralYharnam);
    ASSERT_NE(owner, nullptr);
    EXPECT_EQ(owner->member, "aa01");
    EXPECT_EQ(owner->epoch, 4u);
    EXPECT_EQ(EnemyOwnerOf(Owners, 0x21000000), nullptr);
}

TEST(EnemyOwner, TheOwnerSendsTheEnemiesHpAsAHostAFollowerTakesItAsAGuest) {
    // The host-only check of the HP sends (+0x124 == 3) and the guest-only one of the receive.
    EXPECT_EQ(EnemySyncState(EnemyRole::Owner, Guest), std::optional<s32>{Hosting});
    EXPECT_EQ(EnemySyncState(EnemyRole::Follower, Hosting), std::optional<s32>{Guest});
    // Already so, or no role: unchanged.
    EXPECT_EQ(EnemySyncState(EnemyRole::Owner, Hosting), std::nullopt);
    EXPECT_EQ(EnemySyncState(EnemyRole::Follower, Guest), std::nullopt);
    EXPECT_EQ(EnemySyncState(EnemyRole::Unknown, Guest), std::nullopt);
    EXPECT_EQ(EnemySyncState(EnemyRole::Unknown, Hosting), std::nullopt);
    // Out of a session (state 0) nothing changes.
    EXPECT_EQ(EnemySyncState(EnemyRole::Owner, 0), std::nullopt);
    EXPECT_EQ(EnemySyncState(EnemyRole::Follower, 0), std::nullopt);
}

TEST(EnemyOwner, AHostThatFollowsSendsItsHitsAsAGuestDoes) {
    // The hit on an enemy (0x01A29E40) sends it (ch 0x15) when the state reads 6.
    EXPECT_EQ(EnemyHitState(EnemyRole::Follower, Hosting), std::optional<s32>{Guest});
    EXPECT_EQ(EnemyHitState(EnemyRole::Owner, Hosting), std::nullopt);
    EXPECT_EQ(EnemyHitState(EnemyRole::Owner, Guest), std::nullopt); // its 0x15 is dropped
    EXPECT_EQ(EnemyHitState(EnemyRole::Follower, Guest), std::nullopt);
    EXPECT_EQ(EnemyHitState(EnemyRole::Unknown, Hosting), std::nullopt);
}

TEST(EnemyOwner, TheOwnerRunsTheAiAFollowerTheNetworkController) {
    EXPECT_EQ(EnemyOwnershipWord(EnemyRole::Owner), std::optional<u16>{0});
    EXPECT_EQ(EnemyOwnershipWord(EnemyRole::Follower), std::optional<u16>{0xFFFF});
    EXPECT_EQ(EnemyOwnershipWord(EnemyRole::Unknown), std::nullopt);
}

TEST(EnemyOwner, OnlyTheOwnerSendsTheEnemiesAndAFollowersHitGoesToIt) {
    // HP, catch-up and join animations: the owner's alone.
    for (const u32 channel : {0x05u, 0x29u, 0x36u}) {
        EXPECT_EQ(EnemyPacketOut(channel, EnemyRole::Owner), EnemyPacketFate::Keep) << channel;
        EXPECT_EQ(EnemyPacketOut(channel, EnemyRole::Follower), EnemyPacketFate::Drop) << channel;
        EXPECT_EQ(EnemyPacketOut(channel, EnemyRole::Unknown), EnemyPacketFate::Keep) << channel;
    }
    // A hit (0x15): the owner applied its own already, a follower's goes to the owner.
    EXPECT_EQ(EnemyPacketOut(0x15, EnemyRole::Owner), EnemyPacketFate::Drop);
    EXPECT_EQ(EnemyPacketOut(0x15, EnemyRole::Follower), EnemyPacketFate::ToOwner);
    EXPECT_EQ(EnemyPacketOut(0x15, EnemyRole::Unknown), EnemyPacketFate::Keep);
    // The game's own hand-out of enemies (near lists 0x16, the host's answer 0x17): never, the
    // server's owners decide.
    for (const EnemyRole role : {EnemyRole::Owner, EnemyRole::Follower, EnemyRole::Unknown}) {
        EXPECT_EQ(EnemyPacketOut(0x16, role), EnemyPacketFate::Drop);
        EXPECT_EQ(EnemyPacketOut(0x17, role), EnemyPacketFate::Drop);
    }
    // Positions (each game sends those of the enemies it runs: a follower runs those by it while
    // the owner is far, see FollowerRunsEnemy), the AI records, the hand-over block, per-hit
    // records and everything else: as they are.
    for (const u32 channel : {0x04u, 0x3Du, 0x41u, 0x34u, 0x06u, 0x14u, 0x24u, 0x26u}) {
        EXPECT_EQ(EnemyPacketOut(channel, EnemyRole::Follower), EnemyPacketFate::Keep) << channel;
        EXPECT_EQ(EnemyPacketOut(channel, EnemyRole::Owner), EnemyPacketFate::Keep) << channel;
    }
}

TEST(EnemyOwner, AFollowerTakesTheEnemiesFromTheOwnerOnly) {
    for (const u32 channel : {0x05u, 0x29u, 0x36u}) {
        EXPECT_TRUE(KeepsEnemyPacketIn(channel, EnemyRole::Follower, true)) << channel;
        EXPECT_FALSE(KeepsEnemyPacketIn(channel, EnemyRole::Follower, false)) << channel;
        // The owner runs them itself.
        EXPECT_FALSE(KeepsEnemyPacketIn(channel, EnemyRole::Owner, false)) << channel;
        EXPECT_TRUE(KeepsEnemyPacketIn(channel, EnemyRole::Unknown, false)) << channel;
    }
    // A hit is the owner's to apply; a follower's copy would die alone.
    EXPECT_TRUE(KeepsEnemyPacketIn(0x15, EnemyRole::Owner, false));
    EXPECT_FALSE(KeepsEnemyPacketIn(0x15, EnemyRole::Follower, true));
    EXPECT_TRUE(KeepsEnemyPacketIn(0x15, EnemyRole::Unknown, false));
    EXPECT_FALSE(KeepsEnemyPacketIn(0x16, EnemyRole::Owner, false));
    EXPECT_FALSE(KeepsEnemyPacketIn(0x17, EnemyRole::Follower, true));
    // Positions: the owner takes them from anyone (the guest that runs an enemy it let go); a
    // follower from the owner only (krolebord 2026-10-15: a guest that had just joined crashed in
    // the game's character update while it took the other guests' positions too).
    EXPECT_TRUE(KeepsEnemyPacketIn(0x04, EnemyRole::Owner, false));
    EXPECT_TRUE(KeepsEnemyPacketIn(0x04, EnemyRole::Follower, true));
    EXPECT_FALSE(KeepsEnemyPacketIn(0x04, EnemyRole::Follower, false));
    for (const u32 channel : {0x3Du, 0x41u, 0x34u, 0x06u}) {
        EXPECT_TRUE(KeepsEnemyPacketIn(channel, EnemyRole::Follower, false)) << channel;
        EXPECT_TRUE(KeepsEnemyPacketIn(channel, EnemyRole::Owner, false)) << channel;
    }
}

TEST(EnemyOwner, ARoleChangeIsLoggedOnce) {
    EnemyRoleLog log;
    EXPECT_EQ(log.Change(EnemyRole::Unknown, CentralYharnam, nullptr), std::nullopt);
    const EnemyOwnerEntry mine{CentralYharnam, "aa01", 4};
    const auto first = log.Change(EnemyRole::Owner, CentralYharnam, &mine);
    ASSERT_TRUE(first.has_value());
    EXPECT_EQ(*first, "enemy owner: this game runs map 0x18010000's enemies (epoch 4)");
    EXPECT_EQ(log.Change(EnemyRole::Owner, CentralYharnam, &mine), std::nullopt);
    const EnemyOwnerEntry other{CentralYharnam, "bb02", 5};
    EXPECT_EQ(log.Change(EnemyRole::Follower, CentralYharnam, &other),
              std::optional<std::string>{
                  "enemy owner: this game follows bb02 on map 0x18010000 (epoch 5)"});
    EXPECT_EQ(log.Change(EnemyRole::Unknown, 0, nullptr),
              std::optional<std::string>{"enemy owner: none (the game's own way)"});
}

// Kaniel 2026-10-14: the host far away, an enemy by a guest stood still and woke only when the
// host came near; and never two games running one enemy (nor none: two guests about as near both
// left it, rig guests-pair-mobs). The owner names the one guest that runs each enemy it let go.
MemberVitals GuestAt(const std::string& member, float x, Vital vital = Vital::Up,
                     u32 map = CentralYharnam) {
    return MemberVitals{.member = member, .map = map, .vital = vital, .at = {x, 0.0f, 0.0f}};
}

TEST(EnemyOwner, TheOwnerNamesTheNearestGuestToRunAnEnemyItLetGo) {
    const LampPosition enemy{0.0f, 0.0f, 0.0f};
    const std::vector<MemberVitals> a_near{GuestAt("a", 5.0f), GuestAt("b", 20.0f)};
    EXPECT_EQ(RunnerOf("", enemy, a_near, CentralYharnam), "a");
    // Kept until another is clearly nearer, then handed on.
    const std::vector<MemberVitals> b_slightly{GuestAt("a", 6.0f), GuestAt("b", 4.0f)};
    EXPECT_EQ(RunnerOf("a", enemy, b_slightly, CentralYharnam), "a");
    const std::vector<MemberVitals> b_clearly{GuestAt("a", 9.0f), GuestAt("b", 4.0f)};
    EXPECT_EQ(RunnerOf("a", enemy, b_clearly, CentralYharnam), "b");
    // As near as each other, with nobody running it yet: still one of them.
    const std::vector<MemberVitals> tie{GuestAt("a", 5.0f), GuestAt("b", 5.0f)};
    EXPECT_FALSE(RunnerOf("", enemy, tie, CentralYharnam).empty());
    // Only a guest up, on the map, within reach.
    EXPECT_EQ(RunnerOf("", enemy, std::vector<MemberVitals>{GuestAt("a", 5.0f, Vital::Down)},
                       CentralYharnam),
              "");
    EXPECT_EQ(RunnerOf("", enemy,
                       std::vector<MemberVitals>{GuestAt("a", 5.0f, Vital::Up, 0x18000000)},
                       CentralYharnam),
              "");
    EXPECT_EQ(RunnerOf("a", enemy,
                       std::vector<MemberVitals>{GuestAt("a", RunnerReachMetres + 1.0f)},
                       CentralYharnam),
              "");
}

// Kaniel 2026-10-15: a guest's hits on an enemy it runs went to the host's sleeping copy and came
// back late or not at all. On an enemy this follower runs, its own hits land at once, as the
// host's.
TEST(EnemyOwner, AGuestsHitOnAnEnemyItRunsLandsAtOnce) {
    constexpr s32 Hosting = MatchingStateHosting;
    constexpr s32 Guest = MatchingStateSummonedGuest;
    EXPECT_EQ(EnemyHitState(EnemyRole::Follower, Guest, true), std::optional<s32>{Hosting});
    EXPECT_EQ(EnemyHitState(EnemyRole::Follower, Hosting, true), std::nullopt);
    EXPECT_EQ(EnemyHitState(EnemyRole::Follower, Guest, false), std::nullopt);
    EXPECT_EQ(EnemyHitState(EnemyRole::Follower, Hosting, false), std::optional<s32>{Guest});
    EXPECT_EQ(EnemyHitState(EnemyRole::Owner, Hosting, false), std::nullopt);
}

// Kaniel 2026-10-15: a guest's hit did not count at once: its copy took the hit, then the host's
// older HP (from before the hit reached it) put it back until the host's answer came. For a while
// after its own hit, a guest's copy keeps what its hit left, unless the host's is lower.
TEST(EnemyOwner, AGuestsOwnHitStaysWhileTheHostCatchesUp) {
    EXPECT_EQ(HeldOwnHitHp(484, 382, 100), std::optional<s32>{382}) << "the host's HP from before";
    EXPECT_EQ(HeldOwnHitHp(382, 382, 100), std::nullopt) << "the host has it";
    EXPECT_EQ(HeldOwnHitHp(300, 382, 100), std::nullopt) << "another's hit on top";
    EXPECT_EQ(HeldOwnHitHp(484, 382, OwnHitHoldMs), std::nullopt) << "too long: the host's stands";
    EXPECT_EQ(HeldOwnHitHp(0, 382, 100), std::nullopt) << "dead";
}

// Kaniel 2026-10-15: the dog died as the team joined: the host, the map's first owner, took HP a
// guest had reported from its own world before it joined (1093 -> 413). Only an owner that takes
// a map over from another takes the log's HP; the first owner's world is the enemy.
TEST(EnemyOwner, OnlyAnOwnerThatTakesOverTakesTheLogsHp) {
    EXPECT_FALSE(TakesOverEnemyHp(EnemyOwnerEntry{CentralYharnam, "host", 1}));
    EXPECT_TRUE(TakesOverEnemyHp(EnemyOwnerEntry{CentralYharnam, "guest", 2}));
}
