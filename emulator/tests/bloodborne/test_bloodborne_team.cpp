// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

// Phase 5: the title's load held for the team (TeamGate) and a party controller that waits for
// the team to say its role.
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

TEST(TeamMode, Setting) {
    EXPECT_TRUE(TeamModeEnabled("1"));
    EXPECT_TRUE(TeamModeEnabled("on"));
    EXPECT_FALSE(TeamModeEnabled(nullptr));
    EXPECT_FALSE(TeamModeEnabled("0"));
    EXPECT_FALSE(TeamModeEnabled(""));
}

TEST(TeamGate, NotInATeamLoadsAlone) {
    TeamGate gate;
    gate.TakeLobby({});
    const TeamGate::Decision decision = gate.Tick("me", 0, "Hunter", 1000);
    EXPECT_EQ(decision.step, TeamGate::Step::Load);
    EXPECT_TRUE(decision.requests.empty());
    EXPECT_FALSE(decision.start);
}

TEST(TeamGate, NoWordFromTheLobbyWaitsThenLoadsAlone) {
    TeamGate gate;
    EXPECT_EQ(gate.Tick("me", 0, "Hunter", 1000).step, TeamGate::Step::Hold);
    EXPECT_EQ(gate.Tick("me", 0, "Hunter", 1000 + TeamGate::NoLobbyLoadsAfterMs - 1).step,
              TeamGate::Step::Hold);
    EXPECT_EQ(gate.Tick("me", 0, "Hunter", 1000 + TeamGate::NoLobbyLoadsAfterMs).step,
              TeamGate::Step::Load);
}

TEST(TeamGate, AMembersLoadIsItsReadyAndWaitsForTheStart) {
    TeamGate gate;
    gate.TakeLobby(InTeam("leader"));
    TeamGate::Decision decision = gate.Tick("me", 2, "Hunter", 1000);
    EXPECT_EQ(decision.step, TeamGate::Step::Hold);
    ASSERT_EQ(decision.requests.size(), 1u);
    EXPECT_EQ(decision.requests[0].action(), bbcoop::TeamRequest::READY);
    EXPECT_EQ(decision.requests[0].slot(), 2u);
    EXPECT_EQ(decision.requests[0].character(), "Hunter");
    // Asked once; it waits as long as it takes (the leader decides when).
    decision = gate.Tick("me", 2, "Hunter", 60'000);
    EXPECT_EQ(decision.step, TeamGate::Step::Hold);
    EXPECT_TRUE(decision.requests.empty());
    gate.TakeStart(StartOf("leader", 2));
    decision = gate.Tick("me", 2, "Hunter", 61'000);
    EXPECT_EQ(decision.step, TeamGate::Step::Load);
    ASSERT_TRUE(decision.start);
    EXPECT_EQ(decision.start->party(), "team:t1");
}

TEST(TeamGate, TheLeadersLoadStartsTheTeam) {
    TeamGate gate;
    gate.TakeLobby(InTeam("me"));
    const TeamGate::Decision decision = gate.Tick("me", 0, "Hunter", 1000);
    EXPECT_EQ(decision.step, TeamGate::Step::Hold);
    ASSERT_EQ(decision.requests.size(), 2u);
    EXPECT_EQ(decision.requests[0].action(), bbcoop::TeamRequest::READY);
    EXPECT_EQ(decision.requests[1].action(), bbcoop::TeamRequest::START);
}

TEST(TeamGate, AStartFromAnEarlierLoadDoesNotCount) {
    TeamGate gate;
    gate.TakeLobby(InTeam("leader"));
    gate.TakeStart(StartOf("leader", 0));
    // Back at the title: the next load asks again.
    gate.Reset();
    EXPECT_FALSE(gate.Holding());
    const TeamGate::Decision decision = gate.Tick("me", 0, "Hunter", 1000);
    EXPECT_EQ(decision.step, TeamGate::Step::Hold);
    EXPECT_EQ(decision.requests.size(), 1u);
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
}

} // namespace
