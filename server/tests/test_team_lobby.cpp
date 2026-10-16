// SPDX-FileCopyrightText: Copyright 2026 shadNet Project
// SPDX-License-Identifier: GPL-2.0-or-later
// The team lobby's rules (coop/team_lobby.h): teams, invites, Ready and Start,
// and the store.
#include <iostream>
#include <string>

#include "coop/team_lobby.h"

using namespace BloodborneCoop;
using Action = bbcoop::TeamRequest;

namespace {

bool Check(bool condition, const char *expression, int line) {
  if (!condition) {
    std::cerr << "check failed at line " << line << ": " << expression << '\n';
  }
  return condition;
}

#define CHECK(expression)                                                      \
  do {                                                                         \
    if (!Check((expression), #expression, __LINE__))                           \
      return 1;                                                                \
  } while (false)

TeamLobby NewLobby() {
  return TeamLobby{[n = 0]() mutable { return "t" + std::to_string(++n); }};
}

bbcoop::TeamRequest Request(bbcoop::TeamRequest::Action action,
                            std::string member = {}, std::string team = {},
                            uint32_t slot = 0) {
  bbcoop::TeamRequest request;
  request.set_action(action);
  request.set_member(member);
  request.set_team(team);
  request.set_slot(slot);
  request.set_character("char of slot " + std::to_string(slot));
  return request;
}

int TeamFormsByInvite() {
  TeamLobby lobby = NewLobby();
  lobby.Arrive("a", "Kaniel", false);
  lobby.Arrive("b", "Ada", false);
  // Others see each other signed in.
  bbcoop::LobbyState a = lobby.StateFor("a");
  CHECK(a.players_size() == 1 && a.players(0).name() == "Ada");
  CHECK(a.players(0).state() == bbcoop::LobbyPlayer::TITLE);
  CHECK(!a.has_team());

  CHECK(lobby.Request("a", Request(Action::CREATE)).changed);
  CHECK(lobby.TeamOf("a") && lobby.TeamOf("a")->leader() == "a");
  // Inviting someone not signed in is refused with a notice.
  CHECK(!lobby.Request("a", Request(Action::INVITE, "nobody")).notice.empty());
  CHECK(lobby.Request("a", Request(Action::INVITE, "b")).changed);
  bbcoop::LobbyState b = lobby.StateFor("b");
  CHECK(b.invites_size() == 1 && b.invites(0).from_name() == "Kaniel");
  const std::string team = b.invites(0).team();
  // Only the leader of the team names a team: b cannot invite before joining.
  CHECK(!lobby.Request("b", Request(Action::INVITE, "a")).notice.empty());
  CHECK(lobby.Request("b", Request(Action::ACCEPT, {}, team)).changed);
  CHECK(lobby.TeamOf("b") == lobby.TeamOf("a"));
  CHECK(lobby.StateFor("b").invites_size() == 0);
  CHECK(lobby.StateFor("a").team().members_size() == 2);
  // An accepted invite cannot be used twice; accepting a made-up one is
  // refused.
  CHECK(
      !lobby.Request("b", Request(Action::ACCEPT, {}, "nope")).notice.empty());
  return 0;
}

int TeamHoldsFour() {
  TeamLobby lobby = NewLobby();
  for (const char *m : {"a", "b", "c", "d", "e"}) {
    lobby.Arrive(m, m, false);
  }
  lobby.Request("a", Request(Action::CREATE));
  const std::string team = lobby.TeamOf("a")->id();
  for (const char *m : {"b", "c", "d"}) {
    lobby.Request("a", Request(Action::INVITE, m));
    CHECK(lobby.Request(m, Request(Action::ACCEPT, {}, team)).changed);
  }
  CHECK(lobby.TeamOf("a")->members_size() == 4);
  // A full team takes no more: the invite itself is refused.
  CHECK(!lobby.Request("a", Request(Action::INVITE, "e")).notice.empty());
  return 0;
}

int ReadyAndStart() {
  TeamLobby lobby = NewLobby();
  lobby.Arrive("a", "Kaniel", false);
  lobby.Arrive("b", "Ada", false);
  lobby.Arrive("c", "Lisa", false);
  lobby.Request("a", Request(Action::CREATE));
  const std::string team = lobby.TeamOf("a")->id();
  for (const char *m : {"b", "c"}) {
    lobby.Request("a", Request(Action::INVITE, m));
    lobby.Request(m, Request(Action::ACCEPT, {}, team));
  }
  // Only the leader starts, and only once it is ready itself.
  CHECK(!lobby.Request("b", Request(Action::START)).notice.empty());
  CHECK(!lobby.Request("a", Request(Action::START)).notice.empty());
  lobby.Request("a", Request(Action::READY, {}, {}, 2));
  lobby.Request("b", Request(Action::READY, {}, {}, 0));
  // Kaniel 2026-10-14: the hunt begins only once every member is ready (c is
  // not yet).
  const TeamLobby::Outcome early = lobby.Request("a", Request(Action::START));
  CHECK(!early.notice.empty() && early.starts.empty());
  CHECK(!lobby.TeamOf("a")->started());
  lobby.Request("c", Request(Action::READY, {}, {}, 1));
  const TeamLobby::Outcome start = lobby.Request("a", Request(Action::START));
  CHECK(start.notice.empty() && start.changed);
  CHECK(start.starts.size() == 3);
  CHECK(lobby.TeamOf("a")->started());
  const bbcoop::TeamStart go = lobby.StartFor("a");
  CHECK(go.party() == TeamLobby::PartyOf(team) && go.host() == "a" &&
        go.slot() == 2);
  CHECK(lobby.StartFor("b").slot() == 0 && lobby.StartFor("b").host() == "a");
  // c back at the title readies again and goes at once (joins the team as it
  // plays).
  lobby.Request("c", Request(Action::UNREADY));
  const TeamLobby::Outcome late =
      lobby.Request("c", Request(Action::READY, {}, {}, 1));
  CHECK(late.starts.size() == 1 && late.starts[0] == "c");
  // Playing shows; when nobody plays any more, the team is not started.
  lobby.Arrive("a", "Kaniel", true);
  CHECK(lobby.StateFor("b").team().members(0).state() ==
        bbcoop::LobbyPlayer::PLAYING);
  lobby.Arrive("a", "Kaniel", false);
  CHECK(!lobby.TeamOf("a")->started());
  // A member gone from the server: offline in its team, still a member.
  lobby.Leave("c");
  CHECK(lobby.TeamOf("a")->members_size() == 3);
  CHECK(lobby.StateFor("a").team().members(2).state() ==
        bbcoop::LobbyPlayer::OFFLINE);
  return 0;
}

int StartedTeamKeepsGoingWhileOnePlays() {
  TeamLobby lobby = NewLobby();
  lobby.Arrive("a", "Kaniel", false);
  lobby.Arrive("b", "Ada", false);
  lobby.Request("a", Request(Action::CREATE));
  lobby.Request("a", Request(Action::INVITE, "b"));
  lobby.Request("b", Request(Action::ACCEPT, {}, lobby.TeamOf("a")->id()));
  lobby.Request("a", Request(Action::READY, {}, {}, 0));
  lobby.Request("b", Request(Action::READY, {}, {}, 0));
  lobby.Request("a", Request(Action::START));
  lobby.Arrive("a", "Kaniel", true);
  lobby.Arrive("b", "Ada", true);
  // The host's game crashes: b still plays, the team stays started, and the
  // host's Continue (ready again) goes at once.
  lobby.Leave("a");
  CHECK(lobby.TeamOf("b")->started());
  lobby.Arrive("a", "Kaniel", false);
  const TeamLobby::Outcome again =
      lobby.Request("a", Request(Action::READY, {}, {}, 0));
  CHECK(again.starts.size() == 1 && again.starts[0] == "a");
  return 0;
}

int LeaveAndRemove() {
  TeamLobby lobby = NewLobby();
  for (const char *m : {"a", "b", "c"}) {
    lobby.Arrive(m, m, false);
  }
  lobby.Request("a", Request(Action::CREATE));
  const std::string team = lobby.TeamOf("a")->id();
  for (const char *m : {"b", "c"}) {
    lobby.Request("a", Request(Action::INVITE, m));
    lobby.Request(m, Request(Action::ACCEPT, {}, team));
  }
  // Only the leader removes.
  CHECK(!lobby.Request("b", Request(Action::REMOVE, "c")).notice.empty());
  CHECK(lobby.Request("a", Request(Action::REMOVE, "c")).changed);
  CHECK(lobby.TeamOf("c") == nullptr);
  // A player in a team creates no other one.
  CHECK(!lobby.Request("b", Request(Action::CREATE)).notice.empty());
  // The leader leaves: the next member leads.
  CHECK(lobby.Request("a", Request(Action::LEAVE)).changed);
  CHECK(lobby.TeamOf("b")->leader() == "b");
  // The last one leaves: the team is gone (its world stays on the server's
  // disk).
  lobby.Request("b", Request(Action::LEAVE));
  CHECK(lobby.TeamOf("b") == nullptr);
  CHECK(lobby.Store().teams_size() == 0);
  // A player in a team accepts no other team's invite before leaving its own.
  lobby.Request("a", Request(Action::CREATE));
  lobby.Request("b", Request(Action::CREATE));
  lobby.Request("a", Request(Action::INVITE, "b"));
  CHECK(
      !lobby.Request("b", Request(Action::ACCEPT, {}, lobby.TeamOf("a")->id()))
           .notice.empty());
  // A declined invite is gone.
  lobby.Request("b", Request(Action::DECLINE, {}, lobby.TeamOf("a")->id()));
  CHECK(lobby.StateFor("b").invites_size() == 0);
  return 0;
}

int StoreKeepsTeams() {
  TeamLobby lobby = NewLobby();
  lobby.Arrive("a", "Kaniel", false);
  lobby.Arrive("b", "Ada", false);
  lobby.Arrive("c", "Lisa", false);
  lobby.Request("a", Request(Action::CREATE));
  lobby.Request("a", Request(Action::INVITE, "b"));
  lobby.Request("b", Request(Action::ACCEPT, {}, lobby.TeamOf("a")->id()));
  lobby.Request("a", Request(Action::INVITE, "c"));
  lobby.Request("a", Request(Action::READY, {}, {}, 1));
  lobby.Request("a", Request(Action::START));

  TeamLobby restored = NewLobby();
  restored.Restore(lobby.Store());
  // Teams and invites come back; nobody signed in, ready or started.
  CHECK(restored.TeamOf("a") && restored.TeamOf("a")->members_size() == 2);
  CHECK(!restored.TeamOf("a")->started());
  CHECK(!restored.TeamOf("a")->members(0).ready());
  CHECK(restored.TeamOf("a")->members(1).name() == "Ada");
  restored.Arrive("c", "Lisa", false);
  CHECK(restored.StateFor("c").invites_size() == 1);
  CHECK(restored.StateFor("c").players_size() == 0);
  // New ids do not collide with restored ones.
  restored.Arrive("d", "Dee", false);
  restored.Request("d", Request(Action::CREATE));
  CHECK(restored.TeamOf("d")->id() != restored.TeamOf("a")->id());
  return 0;
}

// Phase 7: the server gives each guest its player slot (the sign it joins
// with: 7, 6, 5), and a guest keeps it while others come and go.
uint32_t SignOf(const TeamLobby &lobby, const std::string &member) {
  for (const bbcoop::TeamMember &m : lobby.TeamOf(member)->members()) {
    if (m.member() == member) {
      return m.sign();
    }
  }
  return 99;
}

int GuestsKeepTheirPlayerSlots() {
  TeamLobby lobby = NewLobby();
  for (const char *m : {"a", "b", "c", "d", "e"}) {
    lobby.Arrive(m, m, false);
  }
  lobby.Request("a", Request(Action::CREATE));
  const std::string team = lobby.TeamOf("a")->id();
  for (const char *m : {"b", "c", "d"}) {
    lobby.Request("a", Request(Action::INVITE, m));
    lobby.Request(m, Request(Action::ACCEPT, {}, team));
  }
  CHECK(SignOf(lobby, "a") == 0); // the leader hosts: no sign
  CHECK(SignOf(lobby, "b") == 7 && SignOf(lobby, "c") == 6 &&
        SignOf(lobby, "d") == 5);
  // c leaves: b and d keep theirs, and e takes the free one.
  lobby.Request("c", Request(Action::LEAVE));
  CHECK(SignOf(lobby, "b") == 7 && SignOf(lobby, "d") == 5);
  lobby.Request("a", Request(Action::INVITE, "e"));
  lobby.Request("e", Request(Action::ACCEPT, {}, team));
  CHECK(SignOf(lobby, "e") == 6);
  // The leader leaves: the new one hosts, the others keep theirs.
  lobby.Request("a", Request(Action::LEAVE));
  CHECK(lobby.TeamOf("b")->leader() == "b" && SignOf(lobby, "b") == 0);
  CHECK(SignOf(lobby, "d") == 5 && SignOf(lobby, "e") == 6);
  // Kept through a restart of the server.
  TeamLobby restored = NewLobby();
  restored.Restore(lobby.Store());
  CHECK(SignOf(restored, "d") == 5 && SignOf(restored, "e") == 6);
  return 0;
}

} // namespace

// Phase 11 (Kaniel 2026-10-15): players are seen by their character's name
// only. A game at the title has none to say (""): the last one it said stays,
// and before any it is "Hunter". A new name reaches its team and its invites.
int PlayersAreSeenByTheirCharactersName() {
  TeamLobby lobby = NewLobby();
  lobby.Arrive("a", "", false);
  lobby.Arrive("b", "", false);
  CHECK(lobby.StateFor("b").players(0).name() == "Hunter");
  CHECK(lobby.Request("a", Request(Action::CREATE)).changed);
  CHECK(lobby.Request("a", Request(Action::INVITE, "b")).changed);
  CHECK(lobby.StateFor("b").invites(0).from_name() == "Hunter");

  CHECK(lobby.Arrive("a", "Lady Maria", true).changed);
  bbcoop::LobbyState b = lobby.StateFor("b");
  CHECK(b.players(0).name() == "Lady Maria");
  CHECK(b.invites(0).from_name() == "Lady Maria");
  CHECK(lobby.StateFor("a").team().members(0).name() == "Lady Maria");
  // Back at the title, it says no name: the character's stays.
  lobby.Arrive("a", "", false);
  CHECK(lobby.StateFor("b").players(0).name() == "Lady Maria");
  // And after a restart of the server, from its store.
  TeamLobby again = NewLobby();
  again.Restore(lobby.Store());
  again.Arrive("a", "", false);
  again.Arrive("b", "", false);
  CHECK(again.StateFor("b").players(0).name() == "Lady Maria");
  return 0;
}

int OfflineMemberLeavesAfterGrace() {
  // Kaniel 2026-10-16: a game closed for good drops its player from the team
  // after a grace period; a crash and Continue inside it still rejoin.
  TeamLobby lobby = NewLobby();
  lobby.Arrive("a", "Kaniel", false);
  lobby.Arrive("b", "Ada", false);
  lobby.Request("a", Request(Action::CREATE));
  lobby.Request("a", Request(Action::INVITE, "b"));
  lobby.Request("b", Request(Action::ACCEPT, {}, lobby.TeamOf("a")->id()));
  lobby.Leave("a");
  // The clock starts at the first look after the game is gone.
  CHECK(!lobby.Expire(1000).changed);
  CHECK(!lobby.Expire(1000 + OfflineGraceMs - 1).changed);
  CHECK(lobby.TeamOf("a") != nullptr);
  // Back in time: still a member, and its clock is gone.
  lobby.Arrive("a", "Kaniel", false);
  CHECK(!lobby.Expire(1000 + OfflineGraceMs).changed);
  lobby.Leave("a");
  CHECK(!lobby.Expire(5000).changed);
  CHECK(!lobby.Expire(5000 + OfflineGraceMs - 1).changed);
  // Gone for the whole grace: out of the team, and b leads.
  CHECK(lobby.Expire(5000 + OfflineGraceMs).changed);
  CHECK(lobby.TeamOf("a") == nullptr);
  CHECK(lobby.TeamOf("b")->leader() == "b" &&
        lobby.TeamOf("b")->members_size() == 1);
  // A team kept on the server's disk, nobody online after a restart: the
  // grace starts with the server, then the team is gone.
  TeamLobby again = NewLobby();
  again.Restore(lobby.Store());
  CHECK(!again.Expire(7000).changed);
  CHECK(again.TeamOf("b") != nullptr);
  CHECK(again.Expire(7000 + OfflineGraceMs).changed);
  CHECK(again.TeamOf("b") == nullptr && again.Store().teams_size() == 0);
  return 0;
}

int main() {
  int failed = 0;
  failed |= PlayersAreSeenByTheirCharactersName();
  failed |= TeamFormsByInvite();
  failed |= TeamHoldsFour();
  failed |= ReadyAndStart();
  failed |= StartedTeamKeepsGoingWhileOnePlays();
  failed |= LeaveAndRemove();
  failed |= OfflineMemberLeavesAfterGrace();
  failed |= StoreKeepsTeams();
  failed |= GuestsKeepTheirPlayerSlots();
  if (failed == 0) {
    std::cout << "team lobby: all checks passed\n";
  }
  return failed;
}
