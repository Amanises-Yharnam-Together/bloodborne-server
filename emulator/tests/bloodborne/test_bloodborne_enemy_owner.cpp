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
    // Positions, HP, catch-up and join animations: the owner's alone (phase 8: the owner runs
    // every enemy on its map, wherever the others stand; no guest runs one).
    for (const u32 channel : {0x04u, 0x05u, 0x29u, 0x36u}) {
        EXPECT_EQ(EnemyPacketOut(channel, EnemyRole::Owner), EnemyPacketFate::Keep) << channel;
        EXPECT_EQ(EnemyPacketOut(channel, EnemyRole::Follower), EnemyPacketFate::Drop) << channel;
        EXPECT_EQ(EnemyPacketOut(channel, EnemyRole::Unknown), EnemyPacketFate::Keep) << channel;
    }
    // A hit (0x15): the owner applied its own already, a follower's goes to the owner.
    EXPECT_EQ(EnemyPacketOut(0x15, EnemyRole::Owner), EnemyPacketFate::Drop);
    EXPECT_EQ(EnemyPacketOut(0x15, EnemyRole::Follower), EnemyPacketFate::ToOwner);
    EXPECT_EQ(EnemyPacketOut(0x15, EnemyRole::Unknown), EnemyPacketFate::Keep);
    // The game's own hand-out of enemies (the host's answer 0x17): never, the server's owners
    // decide. A follower's near list (0x16) goes to the owner: its game sends a member the state
    // of the enemies on its list, attacks too (rig apart-hurt 2026-10-15: a far guest's enemies
    // came for it but never struck).
    EXPECT_EQ(EnemyPacketOut(0x16, EnemyRole::Follower), EnemyPacketFate::ToOwner);
    EXPECT_EQ(EnemyPacketOut(0x16, EnemyRole::Owner), EnemyPacketFate::Drop);
    EXPECT_EQ(EnemyPacketOut(0x16, EnemyRole::Unknown), EnemyPacketFate::Drop);
    for (const EnemyRole role : {EnemyRole::Owner, EnemyRole::Follower, EnemyRole::Unknown}) {
        EXPECT_EQ(EnemyPacketOut(0x17, role), EnemyPacketFate::Drop);
    }
    // The AI records, the hand-over block, per-hit records and everything else: as they are.
    for (const u32 channel : {0x3Du, 0x41u, 0x34u, 0x06u, 0x14u, 0x24u, 0x26u}) {
        EXPECT_EQ(EnemyPacketOut(channel, EnemyRole::Follower), EnemyPacketFate::Keep) << channel;
        EXPECT_EQ(EnemyPacketOut(channel, EnemyRole::Owner), EnemyPacketFate::Keep) << channel;
    }
}

TEST(EnemyOwner, AFollowerTakesTheEnemiesFromTheOwnerOnly) {
    // Positions too (krolebord 2026-10-15: a guest that had just joined crashed in the game's
    // character update while it took the other guests' positions).
    for (const u32 channel : {0x04u, 0x05u, 0x29u, 0x36u}) {
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
    EXPECT_TRUE(KeepsEnemyPacketIn(0x16, EnemyRole::Owner, false));
    EXPECT_FALSE(KeepsEnemyPacketIn(0x16, EnemyRole::Follower, true));
    EXPECT_FALSE(KeepsEnemyPacketIn(0x17, EnemyRole::Follower, true));
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

// Phase 8: the owner's game wakes the groups around every other player as around its own (the
// sets from the rig run 2026-10-15: the host by the lamp, the guest by Iosefka's clinic).
WakeGroups Groups(u32 wakes, u32 counted, u32 half) {
    WakeGroups groups;
    groups.wakes[0] = wakes;
    groups.counted[0] = counted;
    groups.half[0] = half;
    return groups;
}

TEST(EnemyOwner, TheOwnerWakesTheGroupsAroundTheOtherPlayers) {
    const WakeGroups record = Groups(0x60000, 0x20000, 0x60000);
    const WakeGroups own = Groups(0x60400, 0x20000, 0x60400);
    const std::vector<WakeGroups> far{Groups(0x2810000, 0x2000000, 0x2810000)};
    EXPECT_EQ(WithOthersGroups(record, own, far), Groups(0x2870000, 0x2020000, 0x2870000));
    // Nobody else: the game's own.
    EXPECT_EQ(WithOthersGroups(record, own, {}), record);
    // Beside the owner: nothing new.
    const std::vector<WakeGroups> near{Groups(0x60400, 0x20000, 0x60400)};
    EXPECT_EQ(WithOthersGroups(record, own, near), Groups(0x60400, 0x20000, 0x60400));
}

// Rig 2026-10-15: awake by the far guest's groups, the host's game still let the enemy go.
TEST(EnemyOwner, TheOwnerRunsEveryAwakeEnemyTheGameLetGo) {
    EXPECT_EQ(AwakeEnemyWord(EnemyRole::Owner, 4, 4, 0xFFFF, false),
              std::optional<u16>{OwnedEnemyWord});
    // The game's own already: unchanged.
    EXPECT_EQ(AwakeEnemyWord(EnemyRole::Owner, 4, 4, 1, false), std::nullopt);
    EXPECT_EQ(AwakeEnemyWord(EnemyRole::Owner, 4, 4, 0, false), std::nullopt);
    // Not awake (yet), or kept out by the game (a boss before its fight, Iosefka): the game's.
    EXPECT_EQ(AwakeEnemyWord(EnemyRole::Owner, 2, 2, 0xFFFF, false), std::nullopt);
    EXPECT_EQ(AwakeEnemyWord(EnemyRole::Owner, 4, 2, 0xFFFF, false), std::nullopt);
    EXPECT_EQ(AwakeEnemyWord(EnemyRole::Owner, 4, 4, 0xFFFF, true), std::nullopt);
    // Only the owner runs them.
    EXPECT_EQ(AwakeEnemyWord(EnemyRole::Follower, 4, 4, 0xFFFF, false), std::nullopt);
    EXPECT_EQ(AwakeEnemyWord(EnemyRole::Unknown, 4, 4, 0xFFFF, false), std::nullopt);
}

TEST(EnemyOwner, OnlyThisPlayersBlockTakesTheOthersGroups) {
    // Another block's record (Cathedral Ward's, by the border): its groups are its own.
    WakeGroups other_block;
    other_block.wakes[6] = 0x10000000;
    other_block.wakes[7] = 2;
    const WakeGroups own = Groups(0x60400, 0x20000, 0x60400);
    const std::vector<WakeGroups> far{Groups(0x2810000, 0x2000000, 0x2810000)};
    EXPECT_EQ(WithOthersGroups(other_block, own, far), other_block);
    // A block with no record yet (all zero) stays asleep.
    EXPECT_EQ(WithOthersGroups(WakeGroups{}, own, far), WakeGroups{});
}

TEST(EnemyOwner, APlayerWithoutAPlaceYetWakesNothing) {
    const WakeGroups record = Groups(0x60000, 0x20000, 0x60000);
    const WakeGroups own = Groups(0x60400, 0x20000, 0x60400);
    WakeGroups joining;
    joining.wakes.fill(0xFFFFFFFF);
    joining.half.fill(0xFFFFFFFF);
    EXPECT_EQ(WithOthersGroups(record, own, std::vector<WakeGroups>{joining}), record);
    EXPECT_EQ(WithOthersGroups(record, own, std::vector<WakeGroups>{WakeGroups{}}), record);
}

// Phase 8 (one HP path): another game's hit shows on a follower's copy at once.
TEST(EnemyOwner, AFollowersCopyTakesAnotherGamesHitAtOnce) {
    EXPECT_EQ(HitNewsHp(EnemyRole::Follower, 484, 484, 300, 484), std::optional<s32>{300});
    // Scaled by each copy's max (the co-op HP bonus may differ), rounded up.
    EXPECT_EQ(HitNewsHp(EnemyRole::Follower, 968, 968, 300, 484), std::optional<s32>{600});
    // A killing hit: dead here too, at once.
    EXPECT_EQ(HitNewsHp(EnemyRole::Follower, 120, 484, 0, 484), std::optional<s32>{0});
    // Already as low (its own hit, or the owner's HP came first): nothing.
    EXPECT_EQ(HitNewsHp(EnemyRole::Follower, 300, 484, 300, 484), std::nullopt);
    EXPECT_EQ(HitNewsHp(EnemyRole::Follower, 250, 484, 300, 484), std::nullopt);
    // Dead already, or nonsense: nothing.
    EXPECT_EQ(HitNewsHp(EnemyRole::Follower, 0, 484, 0, 484), std::nullopt);
    EXPECT_EQ(HitNewsHp(EnemyRole::Follower, 484, 484, 300, 0), std::nullopt);
    // The owner applies the hit itself (the game's own 0x15): never twice.
    EXPECT_EQ(HitNewsHp(EnemyRole::Owner, 484, 484, 300, 484), std::nullopt);
    EXPECT_EQ(HitNewsHp(EnemyRole::Unknown, 484, 484, 300, 484), std::nullopt);
}

// Phase 8: no guest runs an enemy on a map that has an owner, at any distance; the owner wakes the
// groups around every member instead. A follower's hits always go to the owner.
TEST(EnemyOwner, AFollowerNeverRunsAnEnemy) {
    constexpr s32 Hosting = MatchingStateHosting;
    constexpr s32 Guest = MatchingStateSummonedGuest;
    EXPECT_EQ(EnemyHitState(EnemyRole::Follower, Guest), std::nullopt);
    EXPECT_EQ(EnemyHitState(EnemyRole::Follower, Hosting), std::optional<s32>{Guest});
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
