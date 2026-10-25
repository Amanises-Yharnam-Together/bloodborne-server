// SPDX-FileCopyrightText: Copyright 2026 shadNet Project
// SPDX-License-Identifier: GPL-2.0-or-later

// The team lobby's rules (phase 5, docs/SERVER_COOP_PLAN.md "The team lobby"): who is signed in,
// the teams (up to TeamSize members, a leader that hosts), the invites, Ready and Start. shadNet
// keeps one TeamLobby (bloodborne_world.h) and sends every signed-in player its LobbyState after a
// change. Kaniel 2026-10-17: a team is only a session, kept in memory: a member whose game stays
// gone for OfflineGraceMs leaves its team (Expire), and once every member's game is gone the team
// is gone. The worlds are the server's, apart from the teams (any team plays any of them), and the
// server keeps their list on disk (bloodborne_world.h).
#pragma once

#include <functional>
#include <map>
#include <set>
#include <span>
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

// Kaniel 2026-10-17: the server lists this many worlds (the last played); a new one past them puts
// the least lately played aside.
constexpr size_t MaxWorlds = 16;

// Kaniel 2026-10-25 (the Rot rule): a player's rot stacks up to this many penalties.
constexpr size_t MaxRot = 5;

// How far a world is, for the players: its lamps lit, from its world flags on (a lamp's active
// flag, 10007810 + map * 10000 + lamp * 20, maps 210-399, lamps 0-19: the game's FlagTable).
u32 LampsLit(std::span<const s32> on);

class TeamLobby {
public:
    // `make_id` names a new team or world (random on the server, counted in the tests).
    explicit TeamLobby(std::function<std::string()> make_id);

    // Kaniel 2026-10-16 (the team menu in game): a member out of a team, removed by its leader or
    // gone itself; the server takes it out of that team's world too.
    struct Departure {
        std::string member;
        std::string team;
        std::string party; // the party of the world the team plays (Team.party), if any
        bool operator==(const Departure&) const = default;
    };
    struct Outcome {
        bool changed = false;            // send everyone their state again
        std::string notice;              // why the request was refused (to the requester)
        std::vector<std::string> starts; // members to send a TeamStart now
        std::vector<Departure> departed; // members out of their team
        // Worlds off the list (forgotten by a leader, or past MaxWorlds): their folders go aside.
        std::vector<std::string> forgotten;
        // Kaniel 2026-10-17: a world's line changed (a character played in it): the list to disk.
        bool worlds_changed = false;
    };

    // A player signed in (or said it now plays / is back at the title).
    Outcome Arrive(const std::string& member, const std::string& name, bool playing);
    // Its game is gone. A team none of whose members is signed in any more is gone.
    Outcome Leave(const std::string& member);
    // Members gone for OfflineGraceMs leave their teams. The clock of a member starts at the first
    // call that finds it gone.
    Outcome Expire(s64 now_ms);
    Outcome Request(const std::string& member, const bbcoop::TeamRequest& request);

    // What `member` sees.
    bbcoop::LobbyState StateFor(const std::string& member) const;
    // The TeamStart for `member` (its team must be started).
    bbcoop::TeamStart StartFor(const std::string& member) const;
    // The team `member` is in, or none.
    const bbcoop::Team* TeamOf(const std::string& member) const;
    std::vector<std::string> SignedIn() const;

    // Kaniel 2026-10-17: the server's worlds, the last played first (its store's, at its start).
    void SetWorlds(std::vector<bbcoop::TeamWorldInfo> list);
    const std::vector<bbcoop::TeamWorldInfo>& Worlds() const {
        return worlds;
    }
    // What the server learnt of one of its worlds (its host's character, its flags and lamps, when
    // a Start played it, when it was made: kept from the first note; characters that played in it:
    // added to its own; empty or 0 fields say nothing). A world not listed stays unlisted.
    // Whether anything changed.
    bool NoteWorld(const bbcoop::TeamWorldInfo& note);
    // An id no listed world (nor team) has.
    std::string NewWorldId();

    // The party key of a world: "world:<id>".
    static std::string PartyOf(const std::string& world);

private:
    struct Player {
        std::string name;
        bool playing = false;
        // Kaniel 2026-10-17: the world it picked while in no team (the team it makes takes it).
        std::string world;
        // Kaniel 2026-10-25: its launcher's host rules (the team it makes takes them).
        bbcoop::TeamRules rules;
    };

    bbcoop::Team* FindTeam(const std::string& id);
    bbcoop::TeamMember* MemberIn(bbcoop::Team& team, const std::string& member);
    bbcoop::LobbyPlayer::State StateOf(const std::string& member) const;
    void RemoveFrom(bbcoop::Team& team, const std::string& member);
    void DropTeam(const std::string& id);
    bbcoop::TeamWorldInfo* FindWorld(const std::string& id);
    // The started team (not `except`) that plays `world`, if any.
    const bbcoop::Team* PlayedBy(const std::string& world, const bbcoop::Team* except) const;
    static void PickWorld(bbcoop::Team& team, const std::string& world);
    // Past MaxWorlds: the least lately played no team has picked go.
    void KeepLastWorlds(std::vector<std::string>& forgotten);
    // Phase 7: the leader's sign is 0 (it hosts); a guest keeps its own, one without takes the
    // first free of 7, 6, 5.
    static void GiveSigns(bbcoop::Team& team);
    // A started team nobody plays in any more is not started.
    void Settle();

    std::function<std::string()> make_id;
    std::map<std::string, Player> players; // signed in
    std::vector<bbcoop::Team> teams;
    std::vector<bbcoop::TeamWorldInfo> worlds;                      // the last played first
    std::map<std::string, std::vector<bbcoop::TeamInvite>> invites; // by invited member
    std::map<std::string, std::string> names; // every member's last known name
    std::map<std::string, s64> gone_since;    // team members not signed in, since when
    // Kaniel 2026-10-16: the started teams one of whose games played since the Start (a team
    // whose games still load is not over: a member invited in game joins it with its Ready).
    std::set<std::string> played;
};

} // namespace BloodborneCoop
