// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

// A party of three: the host and two guests. Every rule that used to think of "the other
// player" has to hold with two others, who can be on different maps.

#include <optional>
#include <set>
#include <string>
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
