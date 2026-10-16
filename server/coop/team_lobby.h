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
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "coop.pb.h"
#include "coop/coop_types.h"

namespace BloodborneCoop {

// The game's remote player slots: the host and three more.
constexpr size_t TeamSize = 4;

// Kaniel 2026-10-16: how long a member's game may stay gone (a crash and a restart) before the
// member leaves its team; a team with nobody left is gone.
constexpr s64 OfflineGraceMs = 10 * 60 * 1000;

// Kaniel 2026-10-16: a team keeps this many of its worlds (the last played), each apart.
constexpr size_t MaxTeamWorlds = 8;
// The id of the world a team had before its worlds were kept apart (its party "team:<id>").
inline constexpr std::string_view FirstTeamWorld = "0";

class TeamLobby {
public:
    // `make_id` names a new team (random on the server, counted in the tests).
    explicit TeamLobby(std::function<std::string()> make_id);

    // Kaniel 2026-10-16 (the team menu in game): a member out of a team, removed by its leader or
    // gone itself; the server takes it out of that team's world too.
    struct Departure {
        std::string member;
        std::string team;
        bool operator==(const Departure&) const = default;
    };
    struct Outcome {
        bool changed = false;            // send everyone their state again
        std::string notice;              // why the request was refused (to the requester)
        std::vector<std::string> starts; // members to send a TeamStart now
        std::vector<Departure> departed; // members out of their team
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

    // Kaniel 2026-10-16: what the server learnt of one of a team's worlds (its host's character,
    // its flags, when a Start played it; empty or 0 fields say nothing). A world the team does
    // not have yet is added. Whether anything changed.
    bool NoteWorld(const std::string& team, const bbcoop::TeamWorldInfo& note);

    // The teams and invites as the store keeps them (no states, nobody started).
    bbcoop::StoredTeams Store() const;
    void Restore(const bbcoop::StoredTeams& stored);

    // The party key of a team's world: "team:<team>/<world>" ("team:<team>" for
    // FirstTeamWorld).
    static std::string PartyOf(const std::string& team, const std::string& world);

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
    // Kaniel 2026-10-16: the started teams one of whose games played since the Start (a team
    // whose games still load is not over: a member invited in game joins it with its Ready).
    std::set<std::string> played;
};

} // namespace BloodborneCoop
