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

// Kaniel 2026-10-14: "hold the loading screen until it joins". A guest's screen stays dark from its
// team load until it is in the host's session (or a while: a host that never comes).
TEST(JoinScreen, StaysUpFromTheLoadUntilTheSession) {
    JoinScreen screen;
    EXPECT_FALSE(screen.Up(false, 1000)) << "no team load yet";
    screen.Loading(1000);
    EXPECT_TRUE(screen.Up(false, 1000));
    EXPECT_TRUE(screen.Up(false, 20'000));
    EXPECT_FALSE(screen.Up(true, 21'000)) << "in the session";
    EXPECT_FALSE(screen.Up(false, 22'000)) << "once over, it stays over";
}

TEST(JoinScreen, GivesUpOnAHostThatNeverComes) {
    JoinScreen screen;
    screen.Loading(0);
    EXPECT_TRUE(screen.Up(false, JoinScreen::LimitMs - 1));
    EXPECT_FALSE(screen.Up(false, JoinScreen::LimitMs));
    EXPECT_FALSE(screen.Up(false, JoinScreen::LimitMs + 1));
}

// Kaniel 2026-10-14: no bell items in the game (the team joins without them). The four co-op bells
// (item names from the game's item.msgbnd: 200, 205, 225, 4312), never the Choir Bell (a hunter
// tool that heals, 2050).
TEST(BellItems, TheCoopBellsAndNothingElse) {
    for (const u32 bell : {200u, 205u, 225u, 4312u}) {
        EXPECT_TRUE(IsBellGoods(bell)) << bell;
    }
    for (const u32 other : {2050u, 111u, 201u, 1000u}) {
        EXPECT_FALSE(IsBellGoods(other)) << other;
    }
    EXPECT_EQ(BellGoods.size(), 4u);
}

// Rig 2026-10-14 (host-crash-apart, team-world): the host's game gone, the guest's game set 4047
// ("a player on the way out") and, with no summon reload to clear it, never joined again.
TEST(LeavingFlag, ClearedOnceAGuestStandsAloneAWhile) {
    LeavingFlag leaving;
    EXPECT_FALSE(leaving.Clear(true, true, 0)) << "in the session: the game's own";
    EXPECT_FALSE(leaving.Clear(true, false, 1000));
    EXPECT_FALSE(leaving.Clear(true, false, 1000 + LeavingFlag::ClearAfterMs - 1));
    EXPECT_TRUE(leaving.Clear(true, false, 1000 + LeavingFlag::ClearAfterMs));
    EXPECT_FALSE(leaving.Clear(false, false, 9000)) << "off: nothing to clear";
    EXPECT_FALSE(leaving.Clear(true, false, 10'000)) << "on again: its own wait again";
}

TEST(PhantomHome, APhantomAloneAWhileIsAPlayerAgainNeverWhileItJoins) {
    PhantomHome home;
    EXPECT_FALSE(home.Due(true, false, 0)) << "in a session (or joining): the game's phantom";
    EXPECT_FALSE(home.Due(true, true, 1000));
    EXPECT_FALSE(home.Due(true, false, 2000)) << "a join: the wait starts over";
    EXPECT_FALSE(home.Due(true, true, 3000));
    EXPECT_FALSE(home.Due(true, true, 3000 + PhantomHome::ReturnAfterMs - 1));
    EXPECT_TRUE(home.Due(true, true, 3000 + PhantomHome::ReturnAfterMs));
    EXPECT_FALSE(home.Due(false, true, 20'000)) << "a player already";
}

} // namespace
