// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <vector>

#include <gtest/gtest.h>

#include "core/bloodborne_party.h"

using namespace Core::Bloodborne;

namespace {

const std::vector<u8> Nop5{0x0F, 0x1F, 0x44, 0x00, 0x00};

// A boss kill runs BlockClear2 (event 4050) on every member. In a session it continues with
// BlockClear2_2: the white phantom broadcasts 4063 (the host's OnGameLeave drops it), and every
// member schedules BlockClear2_3Leave, which leaves the session. The two calls go.
TEST(BloodborneBossClear, NobodyLeavesTheSessionAfterABossKill) {
    const auto& patches = BossClearKeepSessionPatches();
    ASSERT_EQ(patches.size(), 6u);
    for (const auto& patch : patches) {
        EXPECT_EQ(patch.original.size(), patch.patched.size()) << std::hex << patch.offset;
    }
    EXPECT_EQ(patches[0].offset, 0x013857ACu);
    EXPECT_EQ(DecodeNearCallTarget(patches[0].offset, patches[0].original),
              PartyGhostCustomLuaCallStartOffset);
    EXPECT_EQ(patches[0].patched, Nop5);

    EXPECT_EQ(patches[1].offset, 0x01385877u);
    EXPECT_EQ(DecodeNearCallTarget(patches[1].offset, patches[1].original), OnKeyTime2Offset);
    EXPECT_EQ(patches[1].patched, Nop5);
}

// BlockClear2 marks every living member as leaving (flags 4047 and 4000). BlockClear2_3 clears
// them again for live and grey players, while a white phantom gets the reset and warp into its
// own world instead. Seamless: a white phantom clears them like everyone else and stays.
TEST(BloodborneBossClear, TheGuestClearsItsLeavingFlagsAndStaysInsteadOfWarpingHome) {
    const auto& patches = BossClearKeepSessionPatches();
    ASSERT_EQ(patches.size(), 6u);
    // `cmp dword ptr [rcx + 0x78], 8; jne <skip the flags>` falls through for everyone.
    EXPECT_EQ(patches[2].offset, 0x0138596Du);
    EXPECT_EQ(patches[2].original, (std::vector<u8>{0x75, 0x1B}));
    EXPECT_EQ(patches[2].patched, (std::vector<u8>{0x66, 0x90}));
    // `cmp dword ptr [rax + 0x78], 1; jne <return>` always returns.
    EXPECT_EQ(patches[3].offset, 0x0138599Cu);
    EXPECT_EQ(patches[3].original, (std::vector<u8>{0x75, 0x46}));
    EXPECT_EQ(patches[3].patched, (std::vector<u8>{0xEB, 0x46}));
}

// BlockClear2's white phantom branch calls SetLoadWait (event state +0x8D = 1), which the event
// update copies to SprjActionButtonMan +0x52 (0x0133A114): no action button until the next
// load, the trip home the guest no longer takes. Seamless: the guest never waits for it.
TEST(BloodborneBossClear, TheGuestKeepsItsActionButtonAfterABossKill) {
    const auto& patches = BossClearKeepSessionPatches();
    ASSERT_EQ(patches.size(), 6u);
    // `mov byte ptr [r14 + 0x8D], 1` -> an 8-byte nop.
    EXPECT_EQ(patches[4].offset, 0x01384D03u);
    EXPECT_EQ(patches[4].original,
              (std::vector<u8>{0x41, 0xC6, 0x86, 0x8D, 0x00, 0x00, 0x00, 0x01}));
    EXPECT_EQ(patches[4].patched,
              (std::vector<u8>{0x0F, 0x1F, 0x84, 0x00, 0x00, 0x00, 0x00, 0x00}));
}

// Outside a friends party BlockClear2_1 takes the solo route instead: a live player (the host,
// or a guest alone in its own world) runs SoloBlockClear 3 s later, which ends with StopRequest
// and a tail jump to LeaveSession. Friend runs (2026-10-08): the host left the room after the
// Cleric Beast and went to the title; a lone guest lost the session after Gascoigne.
// Seamless: SoloBlockClear returns once it has cleared the boss flags.
TEST(BloodborneBossClear, ALivePlayerOutsideAFriendsPartyStaysInTheSession) {
    const auto& patches = BossClearKeepSessionPatches();
    ASSERT_EQ(patches.size(), 6u);
    EXPECT_EQ(patches[5].offset, 0x01385A4Au);
    // `mov rax, [rip + 0x41D0C27]; mov rbx, [rax + 0x16F8]`: the start of LeaveSession.
    EXPECT_EQ(patches[5].original, (std::vector<u8>{0x48, 0x8B, 0x05, 0x27, 0x0C, 0x1D, 0x04, 0x48,
                                                    0x8B, 0x98, 0xF8, 0x16, 0x00, 0x00}));
    // The function's own epilogue: `add rsp, 8; pop rbx; pop r14; pop r15; pop rbp; ret`.
    EXPECT_EQ(patches[5].patched, (std::vector<u8>{0x48, 0x83, 0xC4, 0x08, 0x5B, 0x41, 0x5E, 0x41,
                                                   0x5F, 0x5D, 0xC3, 0x0F, 0x1F, 0x00}));
}

// A co-op area stays closed until the player's own world opened it (Cathedral Ward: its gate,
// 12400160). Friend run 2026-10-09: the host rang the bell in Cathedral Ward, the guest's world
// had not opened the gate, so its bell showed effect 9009 and it never posted a sign. In a party
// the area check's flag read is `xor eax, eax`: the area is always open.
TEST(BloodborneJoinAnywhere, APartyJoinsInAnAreaItsOwnWorldHasNotOpened) {
    const auto& patches = JoinAnywherePatches();
    ASSERT_EQ(patches.size(), 1u);
    EXPECT_EQ(patches[0].offset, 0x0191A8B5u);
    EXPECT_EQ(DecodeNearCallTarget(patches[0].offset, patches[0].original), EventFlagIsSetOffset);
    EXPECT_EQ(patches[0].patched, (std::vector<u8>{0x31, 0xC0, 0x0F, 0x1F, 0x00}));
}

// Friend run 2026-10-09: after Gascoigne the Cathedral Ward door's cutscene (2002:6, evening)
// reloads the world. That load called StopRequest from 0x0193C657 and LeaveSession: the host left
// the room and went to the title, the guest lost the session. In a party it starts a trip.
TEST(BloodborneTimeOfDay, ATimeOfDayReloadIsATrip) {
    EXPECT_TRUE(IsWorldReloadStopRequest(0x0193C65C));
    EXPECT_FALSE(IsWorldReloadStopRequest(0x013CDEFB)) << "a lamp trip's own call";
    EXPECT_FALSE(IsWorldReloadStopRequest(0x019471B6)) << "the world teardown";
}

// Rig 2026-10-13 (border-tomb, as Kaniel's walk into Cathedral Ward after Gascoigne): a guest's
// game called only the world load's LeaveSession (returning to 0x0193C68E), 14 times; the same
// world reload as the host's StopRequest, so the same trip once it loads.
TEST(BloodborneTimeOfDay, AGuestsWorldReloadLeaveIsTheSameReload) {
    EXPECT_TRUE(IsWorldReloadLeaveSession(0x0193C68E));
    EXPECT_FALSE(IsWorldReloadLeaveSession(0x0193C65C)) << "the StopRequest's return";
    EXPECT_FALSE(IsWorldReloadLeaveSession(0x01385AC6)) << "the boss clear's tail jump";
}

} // namespace
