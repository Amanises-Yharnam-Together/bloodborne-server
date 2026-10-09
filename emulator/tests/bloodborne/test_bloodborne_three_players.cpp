// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

// A party of three: the host and two guests. Every rule that used to think of "the other
// player" has to hold with two others, who can be on different maps.

#include <optional>
#include <set>
#include <string>
#include <unordered_set>
#include <vector>

#include <gtest/gtest.h>

#include "core/bloodborne_party.h"

using namespace Core::Bloodborne;

namespace {

PartyObservation HostingWithOneGuest() {
    return PartyObservation{
        .player_valid = true,
        .map_usable = true,
        .stage_transition = false,
        .matching_idle = false, // hosting
        .search_effect_active = false,
        .room_for_more = true, // a third member is online and not in the session
    };
}

const PartyController::Clock::time_point T0{};

PartyAction TickUntilDecision(PartyController& controller, const PartyObservation& observation) {
    for (int i = 0; i < 100; ++i) {
        if (controller.Tick(observation, T0) == PartyAction::StartSearch) {
            return PartyAction::StartSearch;
        }
    }
    return PartyAction::Wait;
}

std::vector<u8> Bytes(std::string_view text) {
    return {text.begin(), text.end()};
}

} // namespace

// The Beckoning Bell's effect ends when a cooperator arrives. With a second guest still out of
// the session the host rings again; a guest in the session, or a full session, does not.
TEST(BloodborneThreePlayers, TheHostRingsAgainForTheSecondGuest) {
    PartyController host(PartyRole::Host);
    EXPECT_EQ(TickUntilDecision(host, HostingWithOneGuest()), PartyAction::StartSearch);

    PartyController full(PartyRole::Host);
    PartyObservation everyone_in = HostingWithOneGuest();
    everyone_in.room_for_more = false;
    EXPECT_EQ(TickUntilDecision(full, everyone_in), PartyAction::Wait);

    PartyController ringing(PartyRole::Host);
    PartyObservation already = HostingWithOneGuest();
    already.search_effect_active = true;
    EXPECT_EQ(TickUntilDecision(ringing, already), PartyAction::Wait);

    PartyController guest(PartyRole::Guest);
    EXPECT_EQ(TickUntilDecision(guest, HostingWithOneGuest()), PartyAction::Wait)
        << "a guest in a session never searches";
}

// On the port the bell's effect stayed on after the first guest arrived while the game had
// stopped searching (rig join3 2026-10-09: the second guest's sign was never taken). A host with
// room whose bell has been on for RetryDelay rings again.
TEST(BloodborneThreePlayers, TheHostRingsAgainWhenItsBellOutlastsTheSearch) {
    PartyController host(PartyRole::Host);
    PartyObservation ringing = HostingWithOneGuest();
    ringing.search_effect_active = true;
    EXPECT_EQ(TickUntilDecision(host, ringing), PartyAction::Wait) << "just rung";
    EXPECT_EQ(host.Tick(ringing, T0 + std::chrono::seconds{10}), PartyAction::Wait);
    EXPECT_EQ(host.Tick(ringing, T0 + PartyController::RetryDelay), PartyAction::StartSearch);
    EXPECT_EQ(host.Tick(ringing, T0 + PartyController::RetryDelay + std::chrono::seconds{1}),
              PartyAction::Wait)
        << "then again only after another RetryDelay";

    PartyController full(PartyRole::Host);
    PartyObservation everyone_in = ringing;
    everyone_in.room_for_more = false;
    EXPECT_EQ(full.Tick(everyone_in, T0), PartyAction::Wait);
    EXPECT_EQ(full.Tick(everyone_in, T0 + PartyController::RetryDelay * 2), PartyAction::Wait)
        << "a full session never rings";
}

// Session members are game objects; the boards know members by online id. The member's identity
// object holds its online id, NUL-terminated.
TEST(BloodborneThreePlayers, ASessionMemberIsFoundByItsOnlineId) {
    const std::vector<std::string> candidates{"Lisara", "Kaniel", "Kan"};
    std::vector<u8> identity(64, 0);
    const auto name = Bytes("Kaniel");
    std::copy(name.begin(), name.end(), identity.begin() + 0x10);
    EXPECT_EQ(FindOnlineIdIn(identity, candidates), std::optional<std::string>{"Kaniel"})
        << "not its prefix Kan";
    const std::vector<u8> none(64, 0x41);
    EXPECT_EQ(FindOnlineIdIn(none, candidates), std::nullopt);
    std::vector<u8> both(96, 0);
    std::copy(name.begin(), name.end(), both.begin() + 0x10);
    const auto other = Bytes("Lisara");
    std::copy(other.begin(), other.end(), both.begin() + 0x40);
    EXPECT_EQ(FindOnlineIdIn(both, candidates), std::nullopt) << "ambiguous";
    std::vector<u8> glued(64, 0);
    const auto longer = Bytes("xKaniel");
    std::copy(longer.begin(), longer.end(), glued.begin() + 0x10);
    EXPECT_EQ(FindOnlineIdIn(glued, candidates), std::nullopt) << "part of a longer name";
    EXPECT_EQ(OnlineIdOfBoardMember(BoardMemberOfOnlineId("Kaniel")), "Kaniel");
    EXPECT_EQ(BoardMemberOfOnlineId("Kaniel"), "4b616e69656c");
    EXPECT_EQ(OnlineIdOfBoardMember("zz"), "");
}

// After a trip only the members on this map are re-created; creating one into a map it is not
// on makes its game leave the session. The others follow when they arrive here.
TEST(BloodborneThreePlayers, OnlyTheMembersOnThisMapAreReCreated) {
    const std::vector<MemberMap> board{
        {"me", 0x18010000}, {"guestA", 0x18010000}, {"guestB", 0x15000000}};
    const std::vector<std::optional<std::string>> members{"guestA", "guestB"};
    std::set<std::string> handled;
    auto plan = PlanRecreate(members, board, "me", 0x18010000, handled);
    EXPECT_EQ(plan.now, (std::vector<size_t>{0}));
    EXPECT_FALSE(plan.done) << "guestB still to come";

    handled.insert("guestA");
    const std::vector<MemberMap> later{
        {"me", 0x18010000}, {"guestA", 0x18010000}, {"guestB", 0x18010000}};
    plan = PlanRecreate(members, later, "me", 0x18010000, handled);
    EXPECT_EQ(plan.now, (std::vector<size_t>{1}));
    EXPECT_TRUE(plan.done);

    // A member that cannot be told apart: back to waiting for everyone on this map.
    const std::vector<std::optional<std::string>> unknown{"guestA", std::nullopt};
    plan = PlanRecreate(unknown, board, "me", 0x18010000, {});
    EXPECT_TRUE(plan.now.empty());
    EXPECT_FALSE(plan.done);
    plan = PlanRecreate(unknown, later, "me", 0x18010000, {});
    EXPECT_EQ(plan.now, (std::vector<size_t>{0, 1}));
    EXPECT_TRUE(plan.done);
}

// A rebuilt join task takes its member's slot from before the trip, by identity. Only when one
// task of that kind was saved may it go to a member whose identity changed.
TEST(BloodborneThreePlayers, EachRebuiltTaskGetsItsOwnSlot) {
    const std::vector<SavedJoinTask> saved{
        {RemoteGuestTask, 0xA0, 1, 1}, {RemoteGuestTask, 0xB0, 2, 1}, {RemoteHostTask, 0xC0, 0, 1}};
    EXPECT_EQ(PickSavedJoinTask(saved, RemoteGuestTask, 0xB0), std::optional<size_t>{1});
    EXPECT_EQ(PickSavedJoinTask(saved, RemoteGuestTask, 0xEE), std::nullopt)
        << "two guests saved: no guessing";
    EXPECT_EQ(PickSavedJoinTask(saved, RemoteHostTask, 0xEE), std::optional<size_t>{2})
        << "the one host there was";
}

// "The host is on my map" comes from the host's own map state post, not from "everyone else is
// here": with two guests, the other guest may be elsewhere.
TEST(BloodborneThreePlayers, TheHostIsHereWhateverTheOtherGuestDoes) {
    const std::vector<MemberMap> board{
        {"me", 0x18010000}, {"host", 0x18010000}, {"guestB", 0x15000000}};
    EXPECT_TRUE(HostIsHere(0x18010000, board, "me", 0x18010000));
    EXPECT_FALSE(HostIsHere(0x15000000, board, "me", 0x18010000));
    EXPECT_FALSE(HostIsHere(std::nullopt, board, "me", 0x18010000));
    const std::vector<MemberMap> gone{{"me", 0x18010000}};
    EXPECT_FALSE(HostIsHere(0x18010000, gone, "me", 0x18010000)) << "a stale post";
}

// The game shares a kill's echoes with the members on the killer's map; the board covers the
// rest. A kill's echoes go to the board whenever someone is elsewhere, with the kill's map, and
// only members on another map add them.
TEST(BloodborneThreePlayers, KillEchoesReachOnlyTheMembersElsewhere) {
    EXPECT_TRUE(SharesKillEchoes(0x018FC99B, true)) << "someone elsewhere";
    EXPECT_FALSE(SharesKillEchoes(0x018FC99B, false)) << "everyone here: the game shares them";
    const std::vector<KillEchoes> kills{{120, 0x18010000}, {4000, 0}};
    EXPECT_EQ(EncodeEchoUpdate("guest", 3, kills), "guest 3 120:402718720 4000");
    EXPECT_EQ(ParseEchoBoard("5\n120 402718720\n4000\n"),
              (std::optional<EchoBoardAnswer>{EchoBoardAnswer{5, kills}}));
    EXPECT_EQ(ParseEchoBoard("5\n120 x\n"), std::nullopt);
    EXPECT_TRUE(GrantsKillEchoes(0x18010000, 0x15000000));
    EXPECT_FALSE(GrantsKillEchoes(0x18010000, 0x18010000)) << "the game gave them already";
    EXPECT_TRUE(GrantsKillEchoes(0, 0x18010000)) << "an old post without a map";
}

// The kill board names its posters: one member's visit ending takes back only its own kill.
TEST(BloodborneThreePlayers, AKillStandsUntilEveryKillerTakesItBack) {
    EXPECT_EQ(ParseKillBoard("4\n268517532 1 guestA\n268517532 1 guestB\n7 0\n"),
              (std::optional<KillBoardAnswer>{KillBoardAnswer{
                  4, {{268517532, true, "guestA"}, {268517532, true, "guestB"}, {7, false, ""}}}}));
    EXPECT_EQ(ParseKillBoard("x\n"), std::nullopt);
    PartyKills kills;
    kills.Apply(std::vector<KillChange>{{268517532, true, "guestA"}, {268517532, true, "guestB"}});
    kills.Apply(std::vector<KillChange>{{268517532, false, "guestA"}});
    EXPECT_TRUE(kills.Contains(268517532)) << "guestB's kill stands";
    kills.Apply(std::vector<KillChange>{{268517532, false, "guestB"}});
    EXPECT_FALSE(kills.Contains(268517532));
}

// More of the same rules at their edges.
TEST(BloodborneThreePlayers, ReCreationWaitsForMembersNotOnTheBoard) {
    const std::vector<MemberMap> board{{"me", 0x18010000}, {"guestA", 0x18010000}};
    // guestB is in the session but posts no map (loading, or its game is gone).
    const std::vector<std::optional<std::string>> members{"guestA", "guestB"};
    const auto plan = PlanRecreate(members, board, "me", 0x18010000, {"guestA"});
    EXPECT_TRUE(plan.now.empty()) << "guestA is done already";
    EXPECT_FALSE(plan.done);
    const auto alone = PlanRecreate({}, board, "me", 0x18010000, {});
    EXPECT_TRUE(alone.now.empty());
    EXPECT_TRUE(alone.done) << "nobody else in the session";
}

TEST(BloodborneThreePlayers, OnlineIdsAtTheEdgesOfTheObject) {
    const std::vector<std::string> candidates{"Kaniel"};
    std::vector<u8> at_start(16, 0);
    const std::string name = "Kaniel";
    std::copy(name.begin(), name.end(), at_start.begin());
    EXPECT_EQ(FindOnlineIdIn(at_start, candidates), std::optional<std::string>{"Kaniel"});
    std::vector<u8> cut(name.begin(), name.end()); // no NUL after it: not known to end there
    EXPECT_EQ(FindOnlineIdIn(cut, candidates), std::nullopt);
    EXPECT_EQ(FindOnlineIdIn(at_start, std::vector<std::string>{}), std::nullopt);
    std::vector<u8> twice(48, 0);
    std::copy(name.begin(), name.end(), twice.begin() + 2);
    std::copy(name.begin(), name.end(), twice.begin() + 24);
    EXPECT_EQ(FindOnlineIdIn(twice, candidates), std::optional<std::string>{"Kaniel"})
        << "the same member twice";
    EXPECT_EQ(OnlineIdOfBoardMember("4b6"), "") << "odd length";
    EXPECT_EQ(OnlineIdOfBoardMember("4B"), "") << "the boards use lowercase";
    EXPECT_EQ(OnlineIdOfBoardMember("20"), "") << "a space is not an online id";
    EXPECT_EQ(OnlineIdOfBoardMember("a1b2c3d4e5f60718"), "") << "an old random member";
    EXPECT_EQ(OnlineIdOfBoardMember(BoardMemberOfOnlineId("my_name-1")), "my_name-1");
}

TEST(BloodborneThreePlayers, SavedTasksOfAnotherKindAreNeverTaken) {
    EXPECT_EQ(PickSavedJoinTask(std::vector<SavedJoinTask>{}, RemoteGuestTask, 0xA0), std::nullopt);
    const std::vector<SavedJoinTask> host_only{{RemoteHostTask, 0xA0, 0, 1}};
    EXPECT_EQ(PickSavedJoinTask(host_only, RemoteGuestTask, 0xA0), std::nullopt)
        << "same identity, other kind";
}

TEST(BloodborneThreePlayers, KillBoardEdges) {
    EXPECT_EQ(ParseKillBoard("5\n7 2\n"), std::nullopt);
    EXPECT_EQ(ParseKillBoard("5\n7 1x\n"), std::nullopt);
    EXPECT_EQ(ParseKillBoard("\n7 1\n"), std::nullopt);
    PartyKills kills;
    kills.Apply(std::vector<KillChange>{{9, false, "nobody"}}); // nothing to take back
    kills.Apply(std::vector<KillChange>{{7, true, "guestA"}, {8, true, "guestB"}});
    EXPECT_EQ(kills.All(), (std::unordered_set<u32>{7, 8}));
    EXPECT_FALSE(kills.Contains(9));
}

TEST(BloodborneThreePlayers, EchoAndHpBoardEdges) {
    EXPECT_EQ(ParseEchoBoard("1\n120 0\n"),
              (std::optional<EchoBoardAnswer>{EchoBoardAnswer{1, {{120, 0}}}}));
    EXPECT_EQ(SharedEnemyHp(500, 2031, 3000, 2031), std::nullopt) << "a report above max";
    EXPECT_EQ(SharedEnemyHp(2'000'000'000, 2'000'000'000, 1, 2'000'000'000), 1) << "no overflow";
    const std::vector<EnemyHp> dead{{0x10014085, -1, 300}};
    EXPECT_EQ(DecodeEnemyHp(EncodeEnemyHp(1, dead)), (EnemyHpReport{1, dead}));
}

// The decisions the game code makes from the boards.
TEST(BloodborneThreePlayers, WhoRunsTheEnemiesAndWhoWaits) {
    const std::vector<MemberMap> two_guests{
        {"me", 0x18010000}, {"guestB", 0x18010000}, {"host", 0x15000000}};
    EXPECT_TRUE(HostAway(0x15000000, two_guests, "me", 0x18010000))
        << "two guests without the host: both run the enemies";
    const std::vector<MemberMap> with_host{
        {"me", 0x18010000}, {"host", 0x18010000}, {"guestB", 0x15000000}};
    EXPECT_FALSE(HostAway(0x18010000, with_host, "me", 0x18010000))
        << "the other guest being elsewhere does not matter";
    // No host post yet: the two-player rules.
    EXPECT_FALSE(HostAway(std::nullopt, with_host, "me", 0x18010000)) << "someone is here";
    const std::vector<MemberMap> alone{{"me", 0x18010000}, {"host", 0x15000000}};
    EXPECT_TRUE(HostAway(std::nullopt, alone, "me", 0x18010000));

    EXPECT_TRUE(SomeoneElsewhere(with_host, "me", 0x18010000));
    const std::vector<MemberMap> together{{"me", 0x18010000}, {"host", 0x18010000}};
    EXPECT_FALSE(SomeoneElsewhere(together, "me", 0x18010000));
    EXPECT_FALSE(SomeoneElsewhere(std::vector<MemberMap>{{"me", 0x18010000}}, "me", 0x18010000));
}

TEST(BloodborneThreePlayers, TheHostRingsOnlyWhileTheSessionHasRoom) {
    EXPECT_TRUE(RoomForMore(true, 2, 3)) << "one guest in, one outside";
    EXPECT_TRUE(RoomForMore(true, 1, 2)) << "a guest dropped out";
    EXPECT_FALSE(RoomForMore(true, 2, 2)) << "everyone is in";
    EXPECT_FALSE(RoomForMore(true, 3, 4)) << "three is the game's limit";
    EXPECT_FALSE(RoomForMore(false, 1, 3)) << "not hosting: the idle search does it";
}

TEST(BloodborneThreePlayers, EachCopyTakesTheLowestHpOnItsMap) {
    const u32 here = 0x18010000;
    const std::vector<MemberMapState> others{
        {here, EncodeEnemyHp(1, std::vector<EnemyHp>{{0x1001409c, 1500, 2031}})},
        {here, EncodeEnemyHp(2, std::vector<EnemyHp>{{0x1001409c, 900, 2031}})},
        {0x15000000, EncodeEnemyHp(3, std::vector<EnemyHp>{{0x1001409c, 10, 2031}})},
        {here, std::vector<u8>{1, 2, 3}}, // a cut post
    };
    EXPECT_EQ(LowestSharedHp(0x1001409c, 2031, 2031, here, others), 900)
        << "the lowest on this map, not the copy on another map";
    EXPECT_EQ(LowestSharedHp(0x1001409c, 800, 2031, here, others), std::nullopt) << "lower here";
    EXPECT_EQ(LowestSharedHp(0x10014085, 300, 300, here, others), std::nullopt) << "not hurt";
}

TEST(BloodborneThreePlayers, AGuestRebuildsTheOtherGuestAsAGuest) {
    // Kaniel 2026-10-09: after a trip the two guests did not see each other. A guest rebuilt
    // every other member as the remote host (kind 0); the other guest is a remote guest (kind 1),
    // as the game made it at the join.
    const std::vector<SavedJoinTask> saved{
        {RemoteHostTask, 0xA0, 0, 0x4},
        {RemoteGuestTask, 0xB0, 1, 0xC},
        {LocalTaskKind(false), 0xC0, 2, 0xC},
    };
    EXPECT_EQ(RebuiltTaskKind(false, saved, 0xA0), RemoteHostTask) << "the host";
    EXPECT_EQ(RebuiltTaskKind(false, saved, 0xB0), RemoteGuestTask) << "the other guest";
    EXPECT_EQ(RebuiltTaskKind(true, saved, 0xB0), RemoteGuestTask) << "the host's guests";
    // Not saved (it joined during the trip): a guest when the host is someone else.
    EXPECT_EQ(RebuiltTaskKind(false, saved, 0xD0), RemoteGuestTask);
    EXPECT_EQ(RebuiltTaskKind(false, std::vector<SavedJoinTask>{}, 0xD0), RemoteHostTask)
        << "nothing known: the one other member of a two-player party is the host";
}

TEST(BloodborneThreePlayers, AnotherBlockOfTheSameAreaSharesItsEnemyHp) {
    // Friend run 2026-10-09: the guest joined in place in Central Yharnam (0x18010000) while the
    // host stood in Cathedral Ward (0x18000000), and no HP was shared for the rest of the game.
    // The blocks of an area (m24_00, m24_01) give their enemies distinct handles.
    const u32 here = 0x18010000;
    const std::vector<MemberMapState> others{
        {0x18000000, EncodeEnemyHp(1, std::vector<EnemyHp>{{0x1001400b, 100, 484}})},
        {0x15000000, EncodeEnemyHp(2, std::vector<EnemyHp>{{0x1001400b, 10, 484}})},
    };
    EXPECT_EQ(LowestSharedHp(0x1001400b, 484, 484, here, others), 100)
        << "the Ward's report, not the other area's";
}

TEST(BloodborneThreePlayers, OnlyAWorldReloadInAPartySessionStartsATrip) {
    EXPECT_TRUE(StartsTripOnWorldReload(0x0193C65C, false, true));
    EXPECT_FALSE(StartsTripOnWorldReload(0x0193C65C, true, true)) << "a trip already";
    EXPECT_FALSE(StartsTripOnWorldReload(0x0193C65C, false, false)) << "no party session";
    EXPECT_FALSE(StartsTripOnWorldReload(0x013CDEFB, false, true)) << "a lamp trip's own call";
}
