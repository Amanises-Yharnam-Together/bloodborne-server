// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

// Checks the patch tables against the game itself: every patch's original bytes, and the calls
// the comments name, in the 01.09 eboot. The game is not in the repository, so these run only
// where BLOODBORNE_EBOOT names an eboot.bin (CUSA03173, 01.09); elsewhere they are skipped.

#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "core/bloodborne_party.h"

using namespace Core::Bloodborne;

namespace {

template <typename T>
T ReadAt(const std::vector<u8>& data, size_t at) {
    T value{};
    std::memcpy(&value, data.data() + at, sizeof(T));
    return value;
}

// The executable segment of a fake-signed SELF: shadPS4 loads it at the image base, so the
// patch offsets are offsets into it.
std::vector<u8> TextSegment(const std::vector<u8>& self) {
    constexpr u32 SelfMagic = 0x1D3D154F;
    if (self.size() < 0x20 || ReadAt<u32>(self, 0) != SelfMagic) {
        return {};
    }
    const u16 segments = ReadAt<u16>(self, 0x18);
    const size_t elf = 0x20 + size_t{segments} * 0x20;
    if (elf + 0x40 > self.size() || std::memcmp(self.data() + elf,
                                                "\x7F"
                                                "ELF",
                                                4) != 0) {
        return {};
    }
    const u64 phoff = ReadAt<u64>(self, elf + 0x20);
    const u16 phnum = ReadAt<u16>(self, elf + 0x38);
    for (u16 segment = 0; segment < segments; ++segment) {
        const size_t entry = 0x20 + size_t{segment} * 0x20;
        const u64 flags = ReadAt<u64>(self, entry);
        const u64 offset = ReadAt<u64>(self, entry + 0x08);
        const u64 size = ReadAt<u64>(self, entry + 0x10);
        const u32 index = static_cast<u32>((flags >> 20) & 0xFFF);
        const size_t header = elf + phoff + size_t{index} * 0x38;
        if (index >= phnum || header + 0x38 > self.size()) {
            continue;
        }
        constexpr u32 Loadable = 1;
        constexpr u32 Executable = 1;
        if (ReadAt<u32>(self, header) == Loadable && (ReadAt<u32>(self, header + 4) & Executable) &&
            ReadAt<u64>(self, header + 0x10) == 0 && offset + size <= self.size()) {
            return {self.begin() + offset, self.begin() + offset + size};
        }
    }
    return {};
}

class BloodborneEboot : public ::testing::Test {
protected:
    static void SetUpTestSuite() {
        const char* path = std::getenv("BLOODBORNE_EBOOT");
        if (path == nullptr || *path == '\0') {
            return;
        }
        std::ifstream file(path, std::ios::binary);
        const std::vector<u8> self{std::istreambuf_iterator<char>{file}, {}};
        text = new std::vector<u8>(TextSegment(self));
    }
    static void TearDownTestSuite() {
        delete text;
        text = nullptr;
    }
    void SetUp() override {
        if (text == nullptr) {
            GTEST_SKIP() << "BLOODBORNE_EBOOT does not name the game's eboot.bin";
        }
        ASSERT_FALSE(text->empty()) << "not a fake-signed SELF with a text segment";
    }

    static std::vector<u8> Bytes(u64 offset, size_t size) {
        if (offset + size > text->size()) {
            return {};
        }
        return {text->begin() + offset, text->begin() + offset + size};
    }
    static u64 CallTarget(u64 offset) {
        return DecodeNearCallTarget(offset, Bytes(offset, 5));
    }
    static u64 JumpTarget(u64 offset) {
        return DecodeNearJumpTarget(offset, Bytes(offset, 5));
    }

    static inline std::vector<u8>* text = nullptr;
};

constexpr u64 MultiPlayStopRequest = 0x01ED07A0; // CSMultiPlayMan StopRequest
constexpr u64 LeaveSession = 0x015BED10;
const std::vector<u8> FunctionPrologue{0x55, 0x48, 0x89, 0xE5}; // push rbp; mov rbp, rsp

} // namespace

TEST_F(BloodborneEboot, EveryPatchFindsTheGamesOwnBytes) {
    const std::vector<std::pair<const char*, const std::vector<CodePatch>*>> tables{
        {"ghost death", &GhostDeathKeepSessionPatches()},
        {"guest bloodstain", &GuestBloodstainPatches()},
        {"host death", &HostDeathKeepGuestsPatches()},
        {"boss clear", &BossClearKeepSessionPatches()},
        {"join anywhere", &JoinAnywherePatches()},
        {"quick arrival", &QuickArrivalPatches()},
    };
    for (const auto& [name, patches] : tables) {
        for (const CodePatch& patch : *patches) {
            EXPECT_EQ(Bytes(patch.offset, patch.original.size()), patch.original)
                << name << " patch at " << std::hex << patch.offset;
        }
    }
}

TEST_F(BloodborneEboot, TheSignTimersAreTheGamesStores) {
    for (const SignTimerSite& site : SignTimerSites) {
        const auto expected = EncodeSignTimerStore(site.modrm, site.original_seconds);
        EXPECT_EQ(Bytes(site.offset, expected.size()),
                  std::vector<u8>(expected.begin(), expected.end()))
            << "sign timer at " << std::hex << site.offset;
    }
}

TEST_F(BloodborneEboot, TheNamedGameFunctionsStartThere) {
    for (const u64 offset :
         {EventFlagIsSetOffset, PartyGhostSetEventFlagOffset, PartyGhostCustomLuaCallStartOffset,
          OnKeyTime2Offset, MultiPlayStopRequest, LeaveSession, KillCharacterOffset}) {
        EXPECT_EQ(Bytes(offset, FunctionPrologue.size()), FunctionPrologue) << std::hex << offset;
    }
}

// The calls the patches and hooks are about are where the comments say.
TEST_F(BloodborneEboot, ThePatchedCallsGoWhereTheCommentsSay) {
    // Ghost death: SetEventFlag(4047) twice, then CustomLuaCallStart(4063).
    EXPECT_EQ(CallTarget(0x01381FE7), PartyGhostSetEventFlagOffset);
    EXPECT_EQ(CallTarget(0x01381FF6), PartyGhostSetEventFlagOffset);
    EXPECT_EQ(CallTarget(0x0138201B), PartyGhostCustomLuaCallStartOffset);
    // SoloBlockClear returns at 0x01385A4A, before its StopRequest and LeaveSession tail jump.
    EXPECT_EQ(CallTarget(0x01385A95), MultiPlayStopRequest);
    EXPECT_EQ(JumpTarget(0x01385AC6), LeaveSession);
    EXPECT_LT(BossClearKeepSessionPatches().back().offset, 0x01385A95u);
    // The world load's StopRequest returns to the address IsWorldReloadStopRequest knows, and its
    // LeaveSession follows.
    EXPECT_EQ(CallTarget(0x0193C657), MultiPlayStopRequest);
    EXPECT_TRUE(IsWorldReloadStopRequest(0x0193C657 + 5));
    EXPECT_EQ(CallTarget(0x0193C689), LeaveSession);
    // The co-op area check: the flag read, then `xor al, 1`.
    EXPECT_EQ(CallTarget(0x0191A8B5), EventFlagIsSetOffset);
    EXPECT_EQ(Bytes(0x0191A8BA, 2), (std::vector<u8>{0x34, 0x01}));
    // The kill autoplay uses: the dead flag (`or byte ptr [r15 + 0x1E3], 1`) on its way out.
    EXPECT_EQ(Bytes(0x018BFAD3, 8),
              (std::vector<u8>{0x41, 0x80, 0x8F, 0xE3, 0x01, 0x00, 0x00, 0x01}));
    // Host death: the jump lands on the epilogue it names.
    const CodePatch& host_dead = HostDeathKeepGuestsPatches().front();
    EXPECT_EQ(DecodeNearJumpTarget(host_dead.offset, host_dead.patched), HostDeadEpilogueOffset);
}
