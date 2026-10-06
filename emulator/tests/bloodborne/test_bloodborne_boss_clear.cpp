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
    ASSERT_EQ(patches.size(), 4u);
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
    ASSERT_EQ(patches.size(), 4u);
    // `cmp dword ptr [rcx + 0x78], 8; jne <skip the flags>` falls through for everyone.
    EXPECT_EQ(patches[2].offset, 0x0138596Du);
    EXPECT_EQ(patches[2].original, (std::vector<u8>{0x75, 0x1B}));
    EXPECT_EQ(patches[2].patched, (std::vector<u8>{0x66, 0x90}));
    // `cmp dword ptr [rax + 0x78], 1; jne <return>` always returns.
    EXPECT_EQ(patches[3].offset, 0x0138599Cu);
    EXPECT_EQ(patches[3].original, (std::vector<u8>{0x75, 0x46}));
    EXPECT_EQ(patches[3].patched, (std::vector<u8>{0xEB, 0x46}));
}

} // namespace
