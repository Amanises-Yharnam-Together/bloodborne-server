// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

// Every board message and saved file the emulator reads comes from another program or another
// run: a server of another version, a cut answer, a file an older build wrote. Random input must
// never crash a parser, and what the emulator writes must read back the same.

#include <map>
#include <random>
#include <string>
#include <vector>

#include <fmt/format.h>
#include <gtest/gtest.h>

#include "core/bloodborne_party.h"

using namespace Core::Bloodborne;

namespace {

constexpr int Rounds = 20000;

// Text made of the characters the formats are built from, so it gets past the first checks, or
// of whole tokens (numbers, members, flags, maps) on lines, so it reaches the checks after them.
std::string RandomText(std::mt19937& random) {
    static constexpr std::string_view Alphabet = "0123456789abcdefx -:\n\n\t\r.e+";
    static const std::vector<std::string> Tokens{"",
                                                 "0",
                                                 "1",
                                                 "7",
                                                 "-5",
                                                 "4294967296",
                                                 "99999999999",
                                                 "402718720",
                                                 "18010000",
                                                 "4b616e69656c",
                                                 "zz",
                                                 "1:1",
                                                 "120:402718720",
                                                 "00ff10",
                                                 "0g",
                                                 "-12.5",
                                                 "nan",
                                                 "1e40",
                                                 ":",
                                                 "268517532",
                                                 "0x10",
                                                 "up",
                                                 "down",
                                                 "wipe"};
    std::uniform_int_distribution<int> coin(0, 1);
    std::uniform_int_distribution<int> any(0, 255);
    std::string text;
    if (coin(random)) {
        std::uniform_int_distribution<size_t> length(0, 48);
        std::uniform_int_distribution<size_t> pick(0, Alphabet.size() - 1);
        for (size_t index = length(random); index > 0; --index) {
            text += (any(random) < 8) ? static_cast<char>(any(random)) : Alphabet[pick(random)];
        }
        return text;
    }
    // Most answers start with a number line (the board's latest sequence number).
    std::uniform_int_distribution<size_t> lines(0, 5);
    std::uniform_int_distribution<size_t> tokens(1, 3);
    std::uniform_int_distribution<size_t> pick(0, Tokens.size() - 1);
    std::uniform_int_distribution<int> percent(0, 99);
    if (percent(random) < 70) {
        text += std::to_string(any(random));
        text += '\n';
    }
    for (size_t line = lines(random); line > 0; --line) {
        for (size_t token = tokens(random); token > 0; --token) {
            text += Tokens[pick(random)];
            if (token > 1) {
                text += coin(random) ? " " : "  ";
            }
        }
        if (line > 1 || coin(random)) {
            text += '\n';
        }
    }
    return text;
}

std::vector<u8> RandomBytes(std::mt19937& random, size_t max_size) {
    std::uniform_int_distribution<size_t> length(0, max_size);
    std::uniform_int_distribution<int> any(0, 255);
    std::vector<u8> bytes(length(random));
    for (u8& byte : bytes) {
        byte = static_cast<u8>(any(random));
    }
    return bytes;
}

} // namespace

TEST(BloodborneFuzz, NoParserCrashesOnRandomInput) {
    std::mt19937 random{2026};
    const std::vector<std::string> candidates{"Kaniel", "Ada", "a"};
    for (int round = 0; round < Rounds; ++round) {
        const std::string text = RandomText(random);
        // Only that they return; what they return is checked by the round trips below.
        (void)ParseMapBoard(text);
        (void)ParseFlagBoard(text);
        (void)ParseKillBoard(text);
        (void)ParseEchoBoard(text);
        (void)ParseMapStateBoard(text);
        (void)ParseTrophyBoard(text);
        (void)ParseLampRestBoard(text);
        (void)ParseVitalsBoard(text);
        (void)ParseResumePoint(text);
        (void)ParseWorldChanges(text);
        (void)ParsePartySetting(text);
        (void)OnlineIdOfBoardMember(text);
        const std::vector<u8> bytes = RandomBytes(random, 64);
        (void)DecodeEnemyHp(bytes);
        (void)FindOnlineIdIn(bytes, candidates);
    }
}

TEST(BloodborneFuzz, WhatTheEmulatorWritesReadsBackTheSame) {
    std::mt19937 random{7};
    std::uniform_int_distribution<s32> any_s32;
    std::uniform_int_distribution<u32> any_u32;
    std::uniform_int_distribution<int> count(0, 6);
    std::uniform_real_distribution<float> coordinate(-2000.0f, 2000.0f);
    for (int round = 0; round < Rounds; ++round) {
        std::vector<EnemyHp> enemies(count(random));
        for (EnemyHp& enemy : enemies) {
            enemy = {any_u32(random), any_s32(random), any_s32(random)};
        }
        const u64 visit = (u64{any_u32(random)} << 32) | any_u32(random);
        EXPECT_EQ(DecodeEnemyHp(EncodeEnemyHp(visit, enemies)), (EnemyHpReport{visit, enemies}));

        const ResumePoint point{any_u32(random),
                                {coordinate(random), coordinate(random), coordinate(random)},
                                any_s32(random) & 0x7FFFFFFF};
        EXPECT_EQ(ParseResumePoint(EncodeResumePoint(point)), point);

        std::map<s32, bool> world;
        for (int index = count(random); index > 0; --index) {
            // Map flags (1MMMxxxx and up) carry to a guest's world, but for a map's load-reset
            // 4xxx and 5xxx ones (FlagResetsOnLoad).
            const s32 flag = 12400000 + (any_s32(random) & 0xFFFFF);
            if (CarriesToGuestWorld(flag)) {
                world[flag] = (any_u32(random) & 1) != 0;
            }
        }
        EXPECT_EQ(ParseWorldChanges(FormatWorldChanges(world)), world);

        const std::string online_id = fmt::format("p{}_{}", any_u32(random) % 100000, round % 10);
        EXPECT_EQ(OnlineIdOfBoardMember(BoardMemberOfOnlineId(online_id)), online_id);
    }
}

// Answers in shadNet's formats (pinned by test_bloodborne_contracts.cpp) read back to what was
// formatted, for any values.
TEST(BloodborneFuzz, ServerAnswersReadBackTheSame) {
    std::mt19937 random{11};
    std::uniform_int_distribution<s32> positive(1, 0x7FFFFFFF);
    std::uniform_int_distribution<u32> any_u32;
    std::uniform_int_distribution<int> count(0, 6);
    std::uniform_int_distribution<int> byte(0, 255);
    for (int round = 0; round < Rounds; ++round) {
        const s64 latest = positive(random);

        KillBoardAnswer kills{latest, {}};
        std::string kill_text = fmt::format("{}\n", latest);
        EchoBoardAnswer echoes{latest, {}};
        std::string echo_text = fmt::format("{}\n", latest);
        std::vector<MemberMap> maps;
        std::string map_text;
        std::vector<MemberMapState> states;
        std::string state_text;
        for (int index = count(random); index > 0; --index) {
            const KillChange kill{positive(random), (any_u32(random) & 1) != 0,
                                  BoardMemberOfOnlineId(fmt::format("m{}", index))};
            kills.changes.push_back(kill);
            kill_text += fmt::format("{} {} {}\n", kill.handle, kill.killed ? 1 : 0, kill.member);

            const KillEchoes echo{positive(random), (any_u32(random) & 1) ? any_u32(random) : 0};
            echoes.echoes.push_back(echo);
            echo_text += echo.map != 0 ? fmt::format("{} {}\n", echo.amount, echo.map)
                                       : fmt::format("{}\n", echo.amount);

            const MemberMap entry{BoardMemberOfOnlineId(fmt::format("n{}", index)),
                                  any_u32(random)};
            maps.push_back(entry);
            map_text += fmt::format("{} {:x}\n", entry.member, entry.map);

            MemberMapState state{any_u32(random), {}};
            std::string hex;
            for (int bits = count(random); bits > 0; --bits) {
                state.bits.push_back(static_cast<u8>(byte(random)));
                hex += fmt::format("{:02x}", state.bits.back());
            }
            if (!state.bits.empty()) { // shadNet only keeps a state with bits
                states.push_back(state);
                state_text += fmt::format("{} {}\n", state.map, hex);
            }
        }
        EXPECT_EQ(ParseKillBoard(kill_text), kills);
        EXPECT_EQ(ParseEchoBoard(echo_text), echoes);
        const auto map_board = ParseMapBoard(map_text);
        ASSERT_TRUE(map_board);
        ASSERT_EQ(map_board->size(), maps.size());
        for (size_t index = 0; index < maps.size(); ++index) {
            EXPECT_EQ((*map_board)[index].member, maps[index].member);
            EXPECT_EQ((*map_board)[index].map, maps[index].map);
        }
        EXPECT_EQ(ParseMapStateBoard(state_text), states);

        VitalsBoard vitals;
        std::string vitals_text;
        for (int index = count(random); index > 0; --index) {
            MemberVitals member{.member = BoardMemberOfOnlineId(fmt::format("v{}", index)),
                                .map = any_u32(random),
                                .vital = static_cast<Vital>(any_u32(random) % 4),
                                .hp = positive(random),
                                .max = positive(random),
                                .down = any_u32(random),
                                .landed = any_u32(random) % 13,
                                .needed = any_u32(random) % 13,
                                .at = {static_cast<float>(byte(random)) - 128.0f, 0.5f,
                                       static_cast<float>(byte(random)) * 0.25f},
                                .revive_damage = any_u32(random)};
            vitals_text +=
                EncodeVitalsUpdate(member, {}) + fmt::format(" {}\n", member.revive_damage);
            vitals.members.push_back(std::move(member));
        }
        if (count(random) > 3) {
            const PartyWipe wipe{any_u32(random),
                                 any_u32(random),
                                 {BoardMemberOfOnlineId("v1"), BoardMemberOfOnlineId("v2")}};
            vitals_text += fmt::format("wipe {} {:x} {} {}\n", wipe.id, wipe.map, wipe.members[0],
                                       wipe.members[1]);
            vitals.wipes.push_back(wipe);
        }
        EXPECT_EQ(ParseVitalsBoard(vitals_text), vitals);
    }
}
