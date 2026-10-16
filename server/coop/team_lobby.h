// SPDX-FileCopyrightText: Copyright 2026 shadNet Project
// SPDX-License-Identifier: GPL-2.0-or-later

// The team lobby's rules (phase 5, docs/SERVER_COOP_PLAN.md "The team lobby"): who is signed in,
// the teams (up to TeamSize members, a leader that hosts), the invites, Ready and Start. shadNet
// keeps one TeamLobby (bloodborne_world.h) and sends every signed-in player its LobbyState after a
// change; the teams outlive the games and the server (Store/Restore, teams.pb), but a member whose
// game stays gone for OfflineGraceMs leaves its team (Expire).
#pragma once

#include <functional>
#include <map>
#include <string>
#include <vector>

#include "coop.pb.h"
#include "coop/coop_types.h"

namespace BloodborneCoop {

// The game's remote player slots: the host and three more.
constexpr size_t TeamSize = 4;

// Kaniel 2026-10-16: how long a member's game may stay gone (a crash and a restart) before the
// member leaves its team; a team with nobody left is gone.
constexpr s64 OfflineGraceMs = 10 * 60 * 1000;

class TeamLobby {
public:
    // `make_id` names a new team (random on the server, counted in the tests).
    explicit TeamLobby(std::function<std::string()> make_id);

    struct Outcome {
        bool changed = false;            // send everyone their state again
        std::string notice;              // why the request was refused (to the requester)
        std::vector<std::string> starts; // members to send a TeamStart now
    };

    // A player signed in (or said it now plays / is back at the title).
    Outcome Arrive(const std::string& member, const std::string& name, bool playing);
    // Its game is gone.
    Outcome Leave(const std::string& member);
    // Members gone for OfflineGraceMs leave their teams. The clock of a member starts at the first
    // call that finds it gone (after Leave, or after Restore for everyone not back yet).
    Outcome Expire(s64 now_ms);
    Outcome Request(const std::string& member, const bbcoop::TeamRequest& request);

    // What `member` sees.
    bbcoop::LobbyState StateFor(const std::string& member) const;
    // The TeamStart for `member` (its team must be started).
    bbcoop::TeamStart StartFor(const std::string& member) const;
    // The team `member` is in, or none.
    const bbcoop::Team* TeamOf(const std::string& member) const;
    std::vector<std::string> SignedIn() const;

    // The teams and invites as the store keeps them (no states, nobody started).
    bbcoop::StoredTeams Store() const;
    void Restore(const bbcoop::StoredTeams& stored);

    // A team's party key, the world it plays.
    static std::string PartyOf(const std::string& team);

private:
    struct Player {
        std::string name;
        bool playing = false;
    };

    bbcoop::Team* FindTeam(const std::string& id);
    bbcoop::TeamMember* MemberIn(bbcoop::Team& team, const std::string& member);
    bbcoop::LobbyPlayer::State StateOf(const std::string& member) const;
    void RemoveFrom(bbcoop::Team& team, const std::string& member);
    // Phase 7: the leader's sign is 0 (it hosts); a guest keeps its own, one without takes the
    // first free of 7, 6, 5.
    static void GiveSigns(bbcoop::Team& team);
    // A started team nobody plays in any more is not started.
    void Settle();

    std::function<std::string()> make_id;
    std::map<std::string, Player> players; // signed in
    std::vector<bbcoop::Team> teams;
    std::map<std::string, std::vector<bbcoop::TeamInvite>> invites; // by invited member
    std::map<std::string, std::string> names; // every member's last known name
    std::map<std::string, s64> gone_since;    // team members not signed in, since when
};

} // namespace BloodborneCoop
