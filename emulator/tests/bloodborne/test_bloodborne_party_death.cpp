// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

// Party death, after Elden Ring Nightreign: a lethal hit puts a party member down instead of
// killing it, the others revive it by hitting it, and a map where nobody is left standing is
// wiped. The rules here are plain functions; the hooks that use them are in the emulator.

#include <array>
#include <optional>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "core/bloodborne_party.h"

using namespace Core::Bloodborne;

namespace {

const std::string Kaniel = "4b616e69656c";
const std::string Lisara = "4c6973617261";
const std::string Third = "7468697264";
constexpr u32 CentralYharnam = 0x18010000;
constexpr u32 HuntersDream = 0x15000000;

MemberVitals Member(const std::string& member, u32 map, Vital vital, u32 down = 0,
                    LampPosition at = {}) {
    return MemberVitals{.member = member,
                        .map = map,
                        .vital = vital,
                        .hp = vital == Vital::Down ? 1 : 900,
                        .max = 1000,
                        .down = down,
                        .at = at};
}

} // namespace

TEST(BloodbornePartyDeath, IsOnlyOnWhenSwitchedOn) {
    EXPECT_TRUE(PartyDeathEnabled("1"));
    EXPECT_FALSE(PartyDeathEnabled(nullptr));
    EXPECT_FALSE(PartyDeathEnabled(""));
    EXPECT_FALSE(PartyDeathEnabled("0"));
}

// IsNoDead's callers by return address: the hit, special effect and fall damage checks may put
// the player down; the game's kills stay real deaths; anything else is left alone.
TEST(BloodbornePartyDeath, KnowsWhichLethalChecksPutThePlayerDown) {
    EXPECT_EQ(ClassifyNoDeadCheck(0x01A2BF7D), NoDeadCheck::Lethal);  // hit damage
    EXPECT_EQ(ClassifyNoDeadCheck(0x01A2FDD4), NoDeadCheck::Lethal);  // poison, frenzy
    EXPECT_EQ(ClassifyNoDeadCheck(0x01A35E10), NoDeadCheck::Lethal);  // a fall
    EXPECT_EQ(ClassifyNoDeadCheck(0x018BF9EE), NoDeadCheck::Instant); // an event's kill
    EXPECT_EQ(ClassifyNoDeadCheck(0x018BCD9C), NoDeadCheck::Instant); // HP set to 0
    EXPECT_EQ(ClassifyNoDeadCheck(0x01A2FCD8), NoDeadCheck::Other);   // asks, kills nobody
    EXPECT_EQ(ClassifyNoDeadCheck(0), NoDeadCheck::Other);
    for (const NoDeadCaller& caller : NoDeadCallers()) {
        EXPECT_EQ(ClassifyNoDeadCheck(caller.return_offset), caller.check);
    }
}

TEST(BloodbornePartyDeath, KeepsTheDownedOrArmedPlayerAliveOnlyAtLethalChecks) {
    // Armed (another member stands on the map): a lethal hit leaves 1 HP.
    EXPECT_TRUE(KeepsPlayerAlive(NoDeadCheck::Lethal, true, false, false));
    // Down: nothing lethal finishes the player off.
    EXPECT_TRUE(KeepsPlayerAlive(NoDeadCheck::Lethal, false, true, false));
    // The game's kills and other questions get the game's own answer.
    EXPECT_FALSE(KeepsPlayerAlive(NoDeadCheck::Instant, true, true, false));
    EXPECT_FALSE(KeepsPlayerAlive(NoDeadCheck::Other, true, true, false));
    EXPECT_TRUE(KeepsPlayerAlive(NoDeadCheck::Other, false, false, true));
    // Not armed and not down: today's death.
    EXPECT_FALSE(KeepsPlayerAlive(NoDeadCheck::Lethal, false, false, false));
    EXPECT_TRUE(KeepsPlayerAlive(NoDeadCheck::Lethal, false, false, true));
}

TEST(BloodbornePartyDeath, NeedsMoreHitsForEachDown) {
    EXPECT_EQ(ReviveHitsNeeded(1), 3u);
    EXPECT_EQ(ReviveHitsNeeded(2), 6u);
    EXPECT_EQ(ReviveHitsNeeded(3), 12u);
    EXPECT_EQ(ReviveHitsNeeded(9), 12u);
    EXPECT_EQ(ReviveHitsNeeded(0), 3u);
}

TEST(BloodbornePartyDeath, GetsUpWithHalfItsHp) {
    EXPECT_EQ(RevivedHp(1520), 760);
    EXPECT_EQ(RevivedHp(3), 1);
    EXPECT_EQ(RevivedHp(1), 1);
    EXPECT_EQ(RevivedHp(0), 1);
}

TEST(BloodbornePartyDeath, GoesDownWithANewDownEachTime) {
    DownedState state{100};
    EXPECT_EQ(state.Current(), Vital::Up);
    const u32 first = state.GoDown(0);
    EXPECT_EQ(state.Current(), Vital::Down);
    EXPECT_EQ(state.DownId(), first);
    EXPECT_EQ(state.HitsNeeded(), 3u);
    EXPECT_EQ(state.HitsLanded(), 0u);
    state.GetUp(1'000);
    EXPECT_EQ(state.Current(), Vital::Up);
    const u32 second = state.GoDown(2'000);
    EXPECT_NE(second, first);
    EXPECT_EQ(state.HitsNeeded(), 6u);
    state.GetUp(3'000);
    state.GoDown(4'000);
    EXPECT_EQ(state.HitsNeeded(), 12u);
}

TEST(BloodbornePartyDeath, IsRevivedByThePartysHits) {
    DownedState state{1};
    state.GoDown(0);
    EXPECT_FALSE(state.Hits(1, 100));
    EXPECT_EQ(state.HitsLanded(), 1u);
    // The board repeats its total: the same hit is not counted again.
    EXPECT_FALSE(state.Hits(1, 200));
    EXPECT_EQ(state.HitsLanded(), 1u);
    EXPECT_TRUE(state.Hits(3, 300));
    EXPECT_EQ(state.HitsLanded(), 3u);
}

TEST(BloodbornePartyDeath, LosesReviveProgressWhenNobodyHits) {
    DownedState state{1};
    state.GoDown(0);
    state.Hits(2, 1'000);
    state.Tick(4'400);
    EXPECT_EQ(state.HitsLanded(), 2u);
    state.Tick(4'500); // 3.5 s after the last hit
    EXPECT_EQ(state.HitsLanded(), 1u);
    state.Tick(5'400);
    EXPECT_EQ(state.HitsLanded(), 1u);
    state.Tick(5'500);
    EXPECT_EQ(state.HitsLanded(), 0u);
    state.Tick(9'000);
    EXPECT_EQ(state.HitsLanded(), 0u);
    // A new hit counts from there.
    EXPECT_FALSE(state.Hits(3, 9'100));
    EXPECT_EQ(state.HitsLanded(), 1u);
}

TEST(BloodbornePartyDeath, ForgetsOldDownsAfterThreeMinutesUpOrAtALamp) {
    DownedState state{1};
    state.GoDown(0);
    state.GetUp(1'000);
    state.GoDown(1'000 + DownedState::PenaltyResetMs);
    EXPECT_EQ(state.HitsNeeded(), 3u);
    state.GetUp(200'000);
    state.GoDown(201'000);
    EXPECT_EQ(state.HitsNeeded(), 6u);
    state.GetUp(202'000);
    state.ClearPenalty();
    state.GoDown(203'000);
    EXPECT_EQ(state.HitsNeeded(), 3u);
}

TEST(BloodbornePartyDeath, ARealDeathEndsTheDownAndRespawnsFresh) {
    DownedState state{1};
    state.GoDown(0);
    state.GetUp(1'000);
    state.GoDown(2'000);
    state.Died();
    EXPECT_EQ(state.Current(), Vital::Dead);
    EXPECT_FALSE(state.Hits(10, 2'500));
    state.Alive(10'000);
    EXPECT_EQ(state.Current(), Vital::Up);
    state.GoDown(11'000);
    EXPECT_EQ(state.HitsNeeded(), 3u);
}

TEST(BloodbornePartyDeath, IgnoresHitsWhileUp) {
    DownedState state{1};
    EXPECT_FALSE(state.Hits(5, 0));
    EXPECT_EQ(state.HitsLanded(), 0u);
}

TEST(BloodbornePartyDeath, GivesUpOnlyAfterAFullHold) {
    HoldToConfirm hold{3'000};
    EXPECT_FALSE(hold.Tick(true, 0));
    EXPECT_FALSE(hold.Tick(true, 2'999));
    EXPECT_FLOAT_EQ(hold.Progress(1'500), 0.5f);
    EXPECT_TRUE(hold.Tick(true, 3'000));
    // Once per hold.
    EXPECT_FALSE(hold.Tick(true, 6'000));
    EXPECT_FALSE(hold.Tick(false, 6'100));
    EXPECT_FLOAT_EQ(hold.Progress(6'100), 0.0f);
    // Letting go early starts over.
    EXPECT_FALSE(hold.Tick(true, 7'000));
    EXPECT_FALSE(hold.Tick(false, 9'000));
    EXPECT_FALSE(hold.Tick(true, 9'500));
    EXPECT_FALSE(hold.Tick(true, 12'000));
    EXPECT_TRUE(hold.Tick(true, 12'500));
}

TEST(BloodbornePartyDeath, CountsAttackPressesAsReviveHits) {
    ReviveHits hits;
    EXPECT_TRUE(hits.Press(Lisara, 4, 0));
    // Faster than an attack: one swing.
    EXPECT_FALSE(hits.Press(Lisara, 4, ReviveHits::GapMs - 1));
    EXPECT_TRUE(hits.Press(Lisara, 4, ReviveHits::GapMs));
    EXPECT_TRUE(hits.Press(Third, 2, ReviveHits::GapMs * 2));
    const std::vector<MemberVitals> board{Member(Lisara, CentralYharnam, Vital::Down, 4),
                                          Member(Third, CentralYharnam, Vital::Down, 2)};
    EXPECT_EQ(hits.Totals(board), (std::vector<ReviveHitTotal>{{Lisara, 4, 2}, {Third, 2, 1}}));
    // A down that ended (revived, or a new one) is dropped.
    const std::vector<MemberVitals> later{Member(Lisara, CentralYharnam, Vital::Up, 4),
                                          Member(Third, CentralYharnam, Vital::Down, 3)};
    EXPECT_TRUE(hits.Totals(later).empty());
    EXPECT_TRUE(hits.Press(Third, 3, ReviveHits::GapMs * 3));
    EXPECT_EQ(hits.Totals(later), (std::vector<ReviveHitTotal>{{Third, 3, 1}}));
}

TEST(BloodbornePartyDeath, RevivesTheNearestDownedMemberInReach) {
    const std::vector<MemberVitals> board{
        Member(Kaniel, CentralYharnam, Vital::Up, 0, {0.0f, 0.0f, 0.0f}),
        Member(Lisara, CentralYharnam, Vital::Down, 4, {2.0f, 0.0f, 0.0f}),
        Member(Third, CentralYharnam, Vital::Down, 2, {1.0f, 0.5f, 0.0f}),
    };
    const LampPosition me{0.0f, 0.0f, 0.0f};
    const auto target = ReviveTarget(board, Kaniel, CentralYharnam, me);
    ASSERT_TRUE(target);
    EXPECT_EQ(target->member, Third);
    EXPECT_EQ(target->down, 2u);
    // Out of reach, on another map, standing, or this player itself: nobody.
    EXPECT_FALSE(ReviveTarget(board, Kaniel, CentralYharnam, LampPosition{10.0f, 0.0f, 0.0f}));
    EXPECT_FALSE(ReviveTarget(board, Kaniel, HuntersDream, me));
    const std::vector<MemberVitals> standing{
        Member(Lisara, CentralYharnam, Vital::Up, 4, {1.0f, 0.0f, 0.0f})};
    EXPECT_FALSE(ReviveTarget(standing, Kaniel, CentralYharnam, me));
    const std::vector<MemberVitals> self{
        Member(Kaniel, CentralYharnam, Vital::Down, 4, {0.0f, 0.0f, 0.0f})};
    EXPECT_FALSE(ReviveTarget(self, Kaniel, CentralYharnam, me));
}

TEST(BloodbornePartyDeath, GoesDownOnlyWithSomeoneElseStandingOnTheMap) {
    EXPECT_TRUE(SomeoneElseStands(std::vector{Member(Lisara, CentralYharnam, Vital::Up)}, Kaniel,
                                  CentralYharnam));
    EXPECT_FALSE(SomeoneElseStands(std::vector{Member(Lisara, CentralYharnam, Vital::Down, 1)},
                                   Kaniel, CentralYharnam));
    EXPECT_FALSE(SomeoneElseStands(std::vector{Member(Lisara, HuntersDream, Vital::Up)}, Kaniel,
                                   CentralYharnam));
    EXPECT_FALSE(SomeoneElseStands(std::vector{Member(Kaniel, CentralYharnam, Vital::Up)}, Kaniel,
                                   CentralYharnam));
    EXPECT_FALSE(SomeoneElseStands(std::vector{Member(Lisara, CentralYharnam, Vital::Off)}, Kaniel,
                                   CentralYharnam));
    EXPECT_FALSE(SomeoneElseStands(std::vector{Member(Lisara, 0, Vital::Up)}, Kaniel, 0));
    EXPECT_FALSE(SomeoneElseStands({}, Kaniel, CentralYharnam));
}

TEST(BloodbornePartyDeath, EncodesTheVitalsUpdate) {
    MemberVitals self = Member(Kaniel, CentralYharnam, Vital::Down, 7, {-12.5f, 3.0f, 40.25f});
    self.hp = 1;
    self.max = 1520;
    self.landed = 1;
    self.needed = 3;
    EXPECT_EQ(EncodeVitalsUpdate(self, {}),
              "4b616e69656c 18010000 down 1 1520 7 1 3 -12.5 3 40.25");
    const std::vector<ReviveHitTotal> hits{{Lisara, 4, 2}, {Third, 9, 1}};
    EXPECT_EQ(EncodeVitalsUpdate(self, hits),
              "4b616e69656c 18010000 down 1 1520 7 1 3 -12.5 3 40.25 4c6973617261:4:2 "
              "7468697264:9:1");
}

TEST(BloodbornePartyDeath, ParsesTheVitalsBoard) {
    const auto board = ParseVitalsBoard("4b616e69656c 18010000 up 1200 1520 0 0 0 -12.5 3 40.25 0\n"
                                        "4c6973617261 18010000 down 1 1380 4 2 3 -11 3 41 2\n"
                                        "wipe 5 15000000 4b616e69656c 7468697264\n");
    ASSERT_TRUE(board);
    ASSERT_EQ(board->members.size(), 2u);
    EXPECT_EQ(board->members[0].member, Kaniel);
    EXPECT_EQ(board->members[0].vital, Vital::Up);
    EXPECT_EQ(board->members[0].hp, 1200);
    EXPECT_EQ(board->members[0].at, (LampPosition{-12.5f, 3.0f, 40.25f}));
    const MemberVitals& lisara = board->members[1];
    EXPECT_EQ(lisara.member, Lisara);
    EXPECT_EQ(lisara.map, CentralYharnam);
    EXPECT_EQ(lisara.vital, Vital::Down);
    EXPECT_EQ(lisara.max, 1380);
    EXPECT_EQ(lisara.down, 4u);
    EXPECT_EQ(lisara.landed, 2u);
    EXPECT_EQ(lisara.needed, 3u);
    EXPECT_EQ(lisara.revive_hits, 2u);
    ASSERT_EQ(board->wipes.size(), 1u);
    EXPECT_EQ(board->wipes[0].id, 5u);
    EXPECT_EQ(board->wipes[0].map, HuntersDream);
    EXPECT_EQ(board->wipes[0].members, (std::vector<std::string>{Kaniel, Third}));
    EXPECT_EQ(WipeFor(*board, Kaniel), std::optional<u32>{5});
    EXPECT_EQ(WipeFor(*board, Lisara), std::nullopt);

    EXPECT_TRUE(ParseVitalsBoard(""));
    EXPECT_FALSE(ParseVitalsBoard("4b616e69656c 18010000 sleeping 1 1 0 0 0 0 0 0 0\n"));
    EXPECT_FALSE(ParseVitalsBoard("4b616e69656c 18010000 up 1 1 0 0 0 0 0 0\n"));
    EXPECT_FALSE(ParseVitalsBoard("4b616e69656c zz up 1 1 0 0 0 0 0 0 0\n"));
    EXPECT_FALSE(ParseVitalsBoard("wipe x 15000000 4b616e69656c\n"));
    EXPECT_FALSE(ParseVitalsBoard("wipe 5 15000000\n"));
}

TEST(BloodbornePartyDeath, NamesTheVitals) {
    for (const Vital vital : {Vital::Off, Vital::Up, Vital::Down, Vital::Dead}) {
        EXPECT_EQ(ParseVital(VitalName(vital)), std::optional<Vital>{vital});
    }
    EXPECT_EQ(VitalName(Vital::Down), "down");
    EXPECT_EQ(ParseVital("Down"), std::nullopt);
}

// A wipe, or a real death with nobody else standing on the map (a wipe of one), resets the map:
// the respawn travels to the lamp even when it is in a loaded block, so the map reloads as it
// does after a solo death. Otherwise a death is today's party death.
TEST(BloodbornePartyDeath, AResetDeathReloadsAtTheLamp) {
    EXPECT_TRUE(DeathResetsMap(true, true));
    EXPECT_TRUE(DeathResetsMap(false, false));
    EXPECT_FALSE(DeathResetsMap(false, true));
    EXPECT_EQ(ChooseDeathRespawn(2412951, true, false, true), DeathRespawn::Travel);
    EXPECT_EQ(ChooseDeathRespawn(2102962, false, false, true), DeathRespawn::Travel);
    EXPECT_EQ(ChooseDeathRespawn(2412951, true, false, false), DeathRespawn::MoveToPoint);
    EXPECT_EQ(ChooseDeathRespawn(-1, true, false, true), DeathRespawn::StayPut);
    // The arrival after that trip puts the player at the lamp; it never travels again.
    EXPECT_EQ(ChooseDeathRespawn(2412951, true, true, true), DeathRespawn::MoveToPoint);
    EXPECT_EQ(ChooseDeathRespawn(2412951, false, true, true), DeathRespawn::StayPut);
}

// A downed player keeps the menu (Options) and the camera: it can't move, attack, roll, heal or
// switch items until it is up.
TEST(BloodbornePartyDeath, ADownedPlayerOnlyKeepsTheMenuAndTheCamera) {
    const PadInput pad{.buttons = PadOptions | PadR1 | PadCircle | AutoplayButton("up") |
                                  AutoplayButton("square"),
                       .sticks = {20, 240, 60, 200},
                       .l2 = 255,
                       .r2 = 128};
    const PadInput kept = DownedPad(pad);
    EXPECT_EQ(kept.buttons, PadOptions);
    EXPECT_EQ(kept.sticks, (std::array<u8, 4>{128, 128, 60, 200}));
    EXPECT_EQ(kept.l2, 0);
    EXPECT_EQ(kept.r2, 0);
}

TEST(BloodbornePartyDeath, AnAttackIsR1OrR2GoingDown) {
    EXPECT_EQ(PadR1, AutoplayButton("r1"));
    EXPECT_EQ(PadR2, AutoplayButton("r2"));
    EXPECT_EQ(PadCircle, AutoplayButton("circle"));
    EXPECT_EQ(PadOptions, AutoplayButton("options"));
    EXPECT_TRUE(AttackPressed(0, PadR1));
    EXPECT_TRUE(AttackPressed(PadCircle, PadCircle | PadR2));
    EXPECT_FALSE(AttackPressed(PadR1, PadR1)) << "held, not pressed";
    EXPECT_FALSE(AttackPressed(PadR1, 0));
    EXPECT_FALSE(AttackPressed(0, PadCircle));
}

// What ends a down, in this order: a wipe of the map, the give-up hold, the party's revive hits,
// and a vitals board not heard from for BoardLostMs (shadNet gone: nobody stays down for good).
TEST(BloodbornePartyDeath, EndsADownByWipeGiveUpReviveOrALostBoard) {
    EXPECT_EQ(EndOfDown(false, false, false, 0), DownEnd::None);
    EXPECT_EQ(EndOfDown(true, true, true, 0), DownEnd::Wipe);
    EXPECT_EQ(EndOfDown(false, true, true, 0), DownEnd::GiveUp);
    EXPECT_EQ(EndOfDown(false, false, true, BoardLostMs), DownEnd::Revive);
    EXPECT_EQ(EndOfDown(false, false, false, BoardLostMs - 1), DownEnd::None);
    EXPECT_EQ(EndOfDown(false, false, false, BoardLostMs), DownEnd::BoardLost);
}

// Circle held from before the down (a sprint) gives up only once it is let go and held again.
TEST(BloodbornePartyDeath, AHoldFromBeforeTheDownDoesNotGiveUp) {
    HoldToConfirm hold{GiveUpHoldMs};
    hold.Restart(true);
    EXPECT_FALSE(hold.Tick(true, 0));
    EXPECT_FALSE(hold.Tick(true, GiveUpHoldMs * 2));
    EXPECT_FLOAT_EQ(hold.Progress(GiveUpHoldMs * 2), 0.0f);
    EXPECT_FALSE(hold.Tick(false, GiveUpHoldMs * 2 + 10));
    EXPECT_FALSE(hold.Tick(true, GiveUpHoldMs * 3));
    EXPECT_TRUE(hold.Tick(true, GiveUpHoldMs * 4));

    HoldToConfirm fresh{GiveUpHoldMs};
    EXPECT_FALSE(fresh.Tick(true, 0));
    fresh.Restart(false);
    EXPECT_FALSE(fresh.Tick(true, 100)) << "the hold starts over";
    EXPECT_FALSE(fresh.Tick(true, GiveUpHoldMs));
    EXPECT_TRUE(fresh.Tick(true, GiveUpHoldMs + 100));
}

// The port's party panel lists the other members playing party death in the board's order, by
// their online ids, and points at the downed one an attack would revive now.
TEST(BloodbornePartyDeath, ListsTheOthersOnThePartyPanel) {
    MemberVitals lisara = Member(Lisara, CentralYharnam, Vital::Down, 4, {1.0f, 0.0f, 0.0f});
    lisara.landed = 2;
    lisara.needed = 3;
    const std::vector<MemberVitals> board{
        Member(Kaniel, CentralYharnam, Vital::Up),    lisara,
        Member(Third, HuntersDream, Vital::Up),       Member("00ff", CentralYharnam, Vital::Dead),
        Member("6f6666", CentralYharnam, Vital::Off),
    };
    const PartyPanel panel = BuildPartyPanel(board, Kaniel, CentralYharnam, LampPosition{});
    ASSERT_EQ(panel.members.size(), 3u);
    EXPECT_EQ(panel.members[0], (PanelMember{.member = Lisara,
                                             .name = "Lisara",
                                             .vital = Vital::Down,
                                             .here = true,
                                             .hp = 1,
                                             .max = 1000,
                                             .landed = 2,
                                             .needed = 3}));
    EXPECT_EQ(panel.members[1].name, "third");
    EXPECT_FALSE(panel.members[1].here);
    EXPECT_EQ(panel.members[2].name, ""); // its board name is not an online id
    EXPECT_EQ(panel.members[2].vital, Vital::Dead);
    EXPECT_EQ(panel.revive_target, 0);
    EXPECT_EQ(panel.vital, Vital::Off); // the local player's own down is the caller's to fill

    EXPECT_EQ(BuildPartyPanel(board, Kaniel, CentralYharnam, LampPosition{9.0f, 0.0f, 0.0f})
                  .revive_target,
              -1);
    EXPECT_EQ(BuildPartyPanel(board, Kaniel, CentralYharnam, std::nullopt).revive_target, -1);
    EXPECT_FALSE(BuildPartyPanel(board, Kaniel, 0, std::nullopt).members[0].here); // loading
    EXPECT_TRUE(BuildPartyPanel({}, Kaniel, CentralYharnam, std::nullopt).members.empty());
}
