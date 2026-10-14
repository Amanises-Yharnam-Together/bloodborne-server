// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

// Phase 5: the title's load held for the team (TeamGate) and a party controller that waits for
// the team to say its role.
#include <algorithm>

#include <gtest/gtest.h>

#include "core/bloodborne_party.h"
#include "core/bloodborne_team.h"

using namespace Core::Bloodborne;

namespace {

bbcoop::LobbyState InTeam(const std::string& leader, const std::string& id = "t1") {
    bbcoop::LobbyState state;
    state.mutable_team()->set_id(id);
    state.mutable_team()->set_leader(leader);
    return state;
}

bbcoop::TeamStart StartOf(const std::string& host, u32 slot) {
    bbcoop::TeamStart start;
    start.set_team("t1");
    start.set_party("team:t1");
    start.set_host(host);
    start.set_slot(slot);
    return start;
}

// The team screen decides: nothing loads until its player plays alone or the team starts.
TEST(TeamGate, NothingLoadsUntilTheScreenSaysSo) {
    TeamGate gate;
    EXPECT_EQ(gate.Tick("me", 0, 1000).step, TeamGate::Step::Hold); // no word from the lobby
    gate.TakeLobby({});
    const TeamGate::Decision decision = gate.Tick("me", 0, 600'000);
    EXPECT_EQ(decision.step, TeamGate::Step::Hold); // not in a team: create, accept or alone
    EXPECT_TRUE(gate.Holding());
}

TEST(TeamGate, PlayAloneLoadsAlone) {
    TeamGate gate;
    gate.TakeLobby(InTeam("leader"));
    gate.Tick("me", 1, 1000);
    EXPECT_TRUE(gate.Choose(TeamGate::Choice::Alone, "me").empty());
    const TeamGate::Decision decision = gate.Tick("me", 1, 1100);
    EXPECT_EQ(decision.step, TeamGate::Step::Load);
    EXPECT_FALSE(decision.start);
}

TEST(TeamGate, ReadyAsksWithTheSlotAndWaitsForTheStart) {
    TeamGate gate;
    gate.TakeLobby(InTeam("leader"));
    EXPECT_EQ(gate.Tick("me", 2, 1000).step, TeamGate::Step::Hold);
    const auto requests = gate.Choose(TeamGate::Choice::Ready, "me");
    ASSERT_EQ(requests.size(), 1u);
    EXPECT_EQ(requests[0].action(), bbcoop::TeamRequest::READY);
    EXPECT_EQ(requests[0].slot(), 2u);
    EXPECT_EQ(gate.Tick("me", 2, 60'000).step, TeamGate::Step::Hold);
    gate.TakeStart(StartOf("leader", 2));
    const TeamGate::Decision decision = gate.Tick("me", 2, 61'000);
    EXPECT_EQ(decision.step, TeamGate::Step::Load);
    ASSERT_TRUE(decision.start);
    EXPECT_EQ(decision.start->party(), "team:t1");
}

TEST(TeamGate, TheLeadersStartIsItsReadyAndTheTeamsStart) {
    TeamGate gate;
    gate.TakeLobby(InTeam("me"));
    gate.Tick("me", 0, 1000);
    const auto requests = gate.Choose(TeamGate::Choice::Start, "me");
    ASSERT_EQ(requests.size(), 2u);
    EXPECT_EQ(requests[0].action(), bbcoop::TeamRequest::READY);
    EXPECT_EQ(requests[1].action(), bbcoop::TeamRequest::START);
    // A member's Start is only its Ready (the leader starts).
    TeamGate member;
    member.TakeLobby(InTeam("leader"));
    member.Tick("me", 0, 1000);
    EXPECT_EQ(member.Choose(TeamGate::Choice::Start, "me").size(), 1u);
    EXPECT_EQ(member.Choose(TeamGate::Choice::Unready, "me")[0].action(),
              bbcoop::TeamRequest::UNREADY);
}

TEST(TeamGate, AnEarlierLoadsChoicesDoNotCount) {
    TeamGate gate;
    gate.TakeLobby(InTeam("leader"));
    gate.Tick("me", 0, 1000);
    gate.Choose(TeamGate::Choice::Alone, "me");
    gate.TakeStart(StartOf("leader", 0));
    // Back at the title: the next load asks again.
    gate.Reset();
    EXPECT_FALSE(gate.Holding());
    EXPECT_EQ(gate.Tick("me", 0, 2000).step, TeamGate::Step::Hold);
}

TEST(PartyControllerTeam, AWaitingControllerRingsNothingUntilItsRoleComes) {
    PartyController controller{PartyRole::Host, true};
    EXPECT_TRUE(controller.Waiting());
    PartyObservation idle{.player_valid = true,
                          .map_usable = true,
                          .stage_transition = false,
                          .matching_idle = true,
                          .search_effect_active = false};
    auto now = PartyController::Clock::now();
    for (int i = 0; i < 100; ++i) {
        EXPECT_EQ(controller.Tick(idle, now), PartyAction::Wait);
        now += std::chrono::milliseconds{33};
    }
    controller.Assign(PartyRole::Guest);
    EXPECT_FALSE(controller.Waiting());
    EXPECT_EQ(controller.SearchEffectId(), SmallResonantBellEffect);
    PartyAction action = PartyAction::Wait;
    for (int i = 0; i < 100 && action == PartyAction::Wait; ++i) {
        action = controller.Tick(idle, now);
        now += std::chrono::milliseconds{33};
    }
    EXPECT_EQ(action, PartyAction::StartSearch);
    // A save alone after the team's: nothing rings again.
    controller.Wait();
    EXPECT_TRUE(controller.Waiting());
    for (int i = 0; i < 100; ++i) {
        EXPECT_EQ(controller.Tick(idle, now), PartyAction::Wait);
        now += std::chrono::milliseconds{33};
    }
}

} // namespace

namespace {

// A store of three blocks of 1000 flags (125 bytes): 1 (1000-1999, the world's), 4 (4000-4999,
// the session's bookkeeping) and 9 (9000-9999: 9300-9307 are each player's own).
struct Store {
    std::vector<std::vector<u8>> bytes{3, std::vector<u8>(125)};
    std::vector<u32> ids{1, 4, 9};
    void Set(s32 flag, bool on = true) {
        const size_t block = std::find(ids.begin(), ids.end(), u32(flag / 1000)) - ids.begin();
        const u32 within = u32(flag % 1000);
        const u8 mask = u8(0x80 >> (within % 8));
        auto& byte = bytes[block][within / 8];
        byte = on ? u8(byte | mask) : u8(byte & ~mask);
    }
    std::vector<FlagBlockBits> Blocks() const {
        std::vector<FlagBlockBits> out;
        for (size_t i = 0; i < ids.size(); ++i) {
            out.push_back({ids[i], bytes[i]});
        }
        return out;
    }
};

TEST(TeamWorld, TheWorldsFlagsOnlyAreCaptured) {
    EXPECT_TRUE(IsWorldFlag(1500));
    EXPECT_FALSE(IsWorldFlag(4000)); // the session's
    EXPECT_FALSE(IsWorldFlag(9300)); // a player's own
    Store store;
    store.Set(1000);
    store.Set(1999);
    store.Set(4000);
    store.Set(9300);
    store.Set(9500);
    const WorldFlags world = CaptureWorldFlags(1000, store.Blocks());
    EXPECT_EQ(world.on, (std::vector<s32>{1000, 1999, 9500}));
    EXPECT_EQ(world.blocks, (std::vector<u32>{1, 4, 9}));
}

TEST(TeamWorld, ChangesMakeTheWorldTheTargetAndLeaveTheRest) {
    Store store;
    store.Set(1000);
    store.Set(1001);
    store.Set(4000); // the session's: left alone
    store.Set(9300); // a player's own: left alone
    WorldFlags target;
    target.blocks = {1, 9, 12};
    target.on = {1001, 1002, 9301, 9600, 12000};
    const auto changes = WorldFlagChanges(1000, store.Blocks(), target);
    // 1000 off, 1002 on, 9600 on; 9301 is a player's own (never set by a world); block 12 is
    // not in this store; block 4 is not in the target.
    EXPECT_EQ(changes,
              (std::vector<std::pair<s32, bool>>{{1000, false}, {1002, true}, {9600, true}}));
    for (const auto& [flag, on] : changes) {
        store.Set(flag, on);
    }
    EXPECT_TRUE(WorldFlagChanges(1000, store.Blocks(), target).empty());
}

TEST(TeamWorld, ThePersonalWorldFileRoundTripsAndACutOneIsNone) {
    WorldFlags world;
    world.blocks = {1, 9};
    world.on = {1000, 9500};
    const std::string text = FormatWorldFlags(world);
    EXPECT_EQ(ParseWorldFlags(text), world);
    EXPECT_FALSE(ParseWorldFlags(text.substr(0, text.size() - 4))); // no "end"
    EXPECT_FALSE(ParseWorldFlags(""));
    EXPECT_FALSE(ParseWorldFlags("blocks\n1\non\nx\nend\n"));
}

// Phase 6: the team join without signs or a bell. The host opens its session once it stands in
// its world and says so once (the server keeps it for its guests); a guest joins the session the
// server sent when it stands alone in its world.
TeamJoin::Frame HostFrame(s32 matching_state, bool ready) {
    return {.host = true, .matching_state = matching_state, .ready = ready};
}
TeamJoin::Frame GuestFrame(s32 matching_state, bool ready, bool session_known) {
    return {.host = false,
            .matching_state = matching_state,
            .ready = ready,
            .session_known = session_known};
}

TEST(TeamJoin, TheHostOpensItsSessionWhenReadyAndAlone) {
    TeamJoin join;
    EXPECT_EQ(join.Tick(HostFrame(0, false)), TeamJoin::Action::None);
    EXPECT_EQ(join.Tick(HostFrame(0, true)), TeamJoin::Action::CreateSession);
    // Opening (the game's own states on the way): nothing more until it hosts.
    EXPECT_EQ(join.Tick(HostFrame(1, true)), TeamJoin::Action::None);
}

TEST(TeamJoin, TheHostSaysItsSessionOnceAndThatItClosed) {
    TeamJoin join;
    EXPECT_EQ(join.Tick(HostFrame(MatchingStateHosting, false)), TeamJoin::Action::Publish);
    EXPECT_EQ(join.Tick(HostFrame(MatchingStateHosting, false)), TeamJoin::Action::None);
    // A member still out of the session (the controller's room for more): the server has it.
    EXPECT_EQ(join.Tick(HostFrame(MatchingStateHosting, true)), TeamJoin::Action::None);
    EXPECT_EQ(join.Tick(HostFrame(0, false)), TeamJoin::Action::PublishClosed);
    EXPECT_EQ(join.Tick(HostFrame(0, false)), TeamJoin::Action::None);
    EXPECT_EQ(join.Tick(HostFrame(0, true)), TeamJoin::Action::CreateSession);
    EXPECT_EQ(join.Tick(HostFrame(MatchingStateHosting, false)), TeamJoin::Action::Publish);
}

TEST(TeamJoin, AGuestJoinsTheHostsSessionWhenReadyAndAlone) {
    TeamJoin join;
    EXPECT_EQ(join.Tick(GuestFrame(0, true, false)), TeamJoin::Action::None);
    EXPECT_EQ(join.Tick(GuestFrame(0, false, true)), TeamJoin::Action::None);
    EXPECT_EQ(join.Tick(GuestFrame(0, true, true)), TeamJoin::Action::Join);
    // Joining or in the session: nothing.
    EXPECT_EQ(join.Tick(GuestFrame(MatchingStateSummonedGuest, true, true)),
              TeamJoin::Action::None);
    // A guest never opens a session or says one.
    EXPECT_EQ(join.Tick(GuestFrame(MatchingStateHosting, true, true)), TeamJoin::Action::None);
}

} // namespace
