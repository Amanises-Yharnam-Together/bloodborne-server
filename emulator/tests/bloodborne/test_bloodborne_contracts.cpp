// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

// The party boards' messages as shadNet parses and formats them
// (server/tests/contracts/bloodborne_party_boards.txt, which shadNet's tests read too): every
// update the emulator encodes is the text there, and every answer there parses to the values
// shadNet formatted. Skipped where the server's tests are not next to the emulator's.

#include <fstream>
#include <map>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "core/bloodborne_party.h"

using namespace Core::Bloodborne;

namespace {

const std::string Kaniel = "4b616e69656c";
const std::string Ada = "416461";
constexpr u32 CentralYharnam = 0x18010000;
constexpr u32 HuntersDream = 0x15000000;

std::map<std::string, std::string> ReadContracts() {
    std::map<std::string, std::string> messages;
    std::ifstream file(BLOODBORNE_CONTRACTS);
    std::string line;
    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#') {
            continue;
        }
        const auto tab = line.find('\t');
        if (tab == std::string::npos) {
            continue;
        }
        std::string text;
        for (size_t index = tab + 1; index < line.size(); ++index) {
            if (line[index] == '\\' && index + 1 < line.size() && line[index + 1] == 'n') {
                text += '\n';
                ++index;
            } else {
                text += line[index];
            }
        }
        messages[line.substr(0, tab)] = text;
    }
    return messages;
}

class BloodborneContracts : public ::testing::Test {
protected:
    void SetUp() override {
        messages = ReadContracts();
        if (messages.empty()) {
            GTEST_SKIP() << "no contracts file at " << BLOODBORNE_CONTRACTS;
        }
        ASSERT_EQ(messages.size(), 21u);
    }
    const std::string& Message(const std::string& name) const {
        return messages.at(name);
    }
    std::map<std::string, std::string> messages;
};

} // namespace

TEST_F(BloodborneContracts, MapBoard) {
    EXPECT_EQ(EncodeMapUpdate(Kaniel, CentralYharnam), Message("map.update"));
    const auto board = ParseMapBoard(Message("map.answer"));
    ASSERT_TRUE(board);
    ASSERT_EQ(board->size(), 2u);
    EXPECT_EQ((*board)[0].member, Kaniel);
    EXPECT_EQ((*board)[0].map, CentralYharnam);
    EXPECT_EQ((*board)[1].member, Ada);
    EXPECT_EQ((*board)[1].map, HuntersDream);
}

TEST_F(BloodborneContracts, RestBoard) {
    EXPECT_EQ(EncodeLampRestUpdate(Kaniel, 72410100, LampPosition{-12.5f, 3.0f, 40.25f}),
              Message("rest.update"));
    const auto board = ParseLampRestBoard(Message("rest.answer"));
    ASSERT_TRUE(board);
    ASSERT_EQ(board->size(), 2u);
    EXPECT_EQ((*board)[0].member, Kaniel);
    EXPECT_EQ((*board)[0].lamp, 72410100);
    EXPECT_EQ((*board)[0].at, (std::optional<LampPosition>{LampPosition{-12.5f, 3.0f, 40.25f}}));
    EXPECT_EQ((*board)[1].member, Ada);
    EXPECT_EQ((*board)[1].at, std::nullopt);
}

TEST_F(BloodborneContracts, TrophyBoard) {
    EXPECT_EQ(EncodeTrophyUpdate(Kaniel, std::vector<s32>{3, 12}), Message("trophy.update"));
    EXPECT_EQ(ParseTrophyBoard(Message("trophy.answer")),
              (std::optional<std::vector<s32>>{std::vector<s32>{3, 12}}));
}

TEST_F(BloodborneContracts, FlagBoard) {
    const std::vector<std::pair<s32, bool>> changes{{12417830, true}, {7116, false}};
    EXPECT_EQ(EncodeFlagUpdate(Kaniel, 0, changes), Message("flags.update"));
    EXPECT_EQ(ParseFlagBoard(Message("flags.answer")),
              (std::optional<FlagBoardAnswer>{FlagBoardAnswer{3, changes}}));
}

TEST_F(BloodborneContracts, KillBoard) {
    const std::vector<std::pair<s32, bool>> kill{{268517532, true}};
    EXPECT_EQ(EncodeFlagUpdate(Kaniel, 7, kill), Message("kills.update"));
    EXPECT_EQ(ParseKillBoard(Message("kills.answer")),
              (std::optional<KillBoardAnswer>{
                  KillBoardAnswer{9, {{268517532, true, Kaniel}, {268517532, false, Ada}}}}));
}

TEST_F(BloodborneContracts, WorldSnapshot) {
    const std::vector<u8> blob{0x01, 0xab};
    EXPECT_EQ(EncodeWorldSnapshotUpdate(Kaniel, blob), Message("world.update"));
    EXPECT_EQ(EncodeWorldSnapshotUpdate(Kaniel, {}), Message("world.read"));
    EXPECT_EQ(ParseWorldSnapshotAnswer(Message("world.answer")),
              (std::optional<WorldSnapshotAnswer>{WorldSnapshotAnswer{1500, blob}}));
}

TEST_F(BloodborneContracts, MapStateAndHpBoards) {
    const std::vector<u8> bits{0x00, 0xFF, 0x10};
    EXPECT_EQ(EncodeMapStateUpdate(Kaniel, CentralYharnam, bits), Message("mapstate.update"));
    EXPECT_EQ(ParseMapStateBoard(Message("mapstate.answer")),
              (std::optional<std::vector<MemberMapState>>{{MemberMapState{CentralYharnam, bits}}}));

    const std::vector<EnemyHp> hurt{{0x1001409c, 1595, 2031}};
    const auto hp = EncodeEnemyHp(3, hurt);
    EXPECT_EQ(EncodeMapStateUpdate(Kaniel, CentralYharnam, hp), Message("hp.update"));
    const auto board = ParseMapStateBoard(Message("hp.answer"));
    ASSERT_TRUE(board);
    ASSERT_EQ(board->size(), 1u);
    EXPECT_EQ((*board)[0].map, CentralYharnam);
    EXPECT_EQ(DecodeEnemyHp((*board)[0].bits), (EnemyHpReport{3, hurt}));
}

TEST_F(BloodborneContracts, EchoBoard) {
    const std::vector<KillEchoes> kills{{120, CentralYharnam}, {4000, 0}};
    EXPECT_EQ(EncodeEchoUpdate(Kaniel, 3, kills), Message("echoes.update"));
    EXPECT_EQ(ParseEchoBoard(Message("echoes.answer")),
              (std::optional<EchoBoardAnswer>{EchoBoardAnswer{5, kills}}));
}

TEST_F(BloodborneContracts, VitalsBoard) {
    const MemberVitals self{.member = Kaniel,
                            .map = CentralYharnam,
                            .vital = Vital::Up,
                            .hp = 1200,
                            .max = 1520,
                            .at = {-12.5f, 3.0f, 40.25f}};
    const std::vector<ReviveHitTotal> hits{{Ada, 4, 2}};
    EXPECT_EQ(EncodeVitalsUpdate(self, hits), Message("vitals.update"));
    const MemberVitals ada{.member = Ada,
                           .map = CentralYharnam,
                           .vital = Vital::Down,
                           .hp = 1,
                           .max = 1380,
                           .down = 4,
                           .landed = 2,
                           .needed = 3,
                           .at = {-11.0f, 3.0f, 41.0f},
                           .revive_damage = 2};
    EXPECT_EQ(ParseVitalsBoard(Message("vitals.answer")),
              (std::optional<VitalsBoard>{
                  VitalsBoard{{self, ada}, {PartyWipe{1, HuntersDream, {Kaniel, Ada}}}}}));
}
