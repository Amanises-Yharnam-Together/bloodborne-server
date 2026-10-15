// SPDX-FileCopyrightText: Copyright 2026 shadNet Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "coop/team_lobby.h"

#include <algorithm>
#include <array>
#include <vector>

namespace BloodborneCoop {

namespace {

constexpr u32 StoredTeamsVersion = 1;

bool AnyPlays(const bbcoop::Team& team, const std::function<bool(const std::string&)>& plays) {
    return std::any_of(team.members().begin(), team.members().end(),
                       [&](const bbcoop::TeamMember& m) { return plays(m.member()); });
}

} // namespace

TeamLobby::TeamLobby(std::function<std::string()> make_id_) : make_id{std::move(make_id_)} {}

std::string TeamLobby::PartyOf(const std::string& team) {
    return "team:" + team;
}

bbcoop::Team* TeamLobby::FindTeam(const std::string& id) {
    for (bbcoop::Team& team : teams) {
        if (team.id() == id) {
            return &team;
        }
    }
    return nullptr;
}

const bbcoop::Team* TeamLobby::TeamOf(const std::string& member) const {
    for (const bbcoop::Team& team : teams) {
        for (const bbcoop::TeamMember& m : team.members()) {
            if (m.member() == member) {
                return &team;
            }
        }
    }
    return nullptr;
}

bbcoop::TeamMember* TeamLobby::MemberIn(bbcoop::Team& team, const std::string& member) {
    for (bbcoop::TeamMember& m : *team.mutable_members()) {
        if (m.member() == member) {
            return &m;
        }
    }
    return nullptr;
}

bbcoop::LobbyPlayer::State TeamLobby::StateOf(const std::string& member) const {
    const auto player = players.find(member);
    if (player == players.end()) {
        return bbcoop::LobbyPlayer::OFFLINE;
    }
    return player->second.playing ? bbcoop::LobbyPlayer::PLAYING : bbcoop::LobbyPlayer::TITLE;
}

void TeamLobby::RemoveFrom(bbcoop::Team& team, const std::string& member) {
    auto* members = team.mutable_members();
    members->erase(
        std::remove_if(members->begin(), members->end(),
                       [&](const bbcoop::TeamMember& m) { return m.member() == member; }),
        members->end());
    if (team.leader() == member && !members->empty()) {
        team.set_leader(members->begin()->member());
    }
    GiveSigns(team);
    if (members->empty()) {
        const std::string id = team.id();
        std::erase_if(teams, [&](const bbcoop::Team& t) { return t.id() == id; });
        for (auto& [to, list] : invites) {
            std::erase_if(list, [&](const bbcoop::TeamInvite& i) { return i.team() == id; });
        }
    }
}

void TeamLobby::GiveSigns(bbcoop::Team& team) {
    constexpr std::array<u32, 3> GuestSigns{7, 6, 5};
    std::vector<u32> taken;
    for (bbcoop::TeamMember& m : *team.mutable_members()) {
        const bool guest_sign = std::ranges::find(GuestSigns, m.sign()) != GuestSigns.end();
        if (m.member() == team.leader() || !guest_sign ||
            std::ranges::find(taken, m.sign()) != taken.end()) {
            m.set_sign(0);
        } else {
            taken.push_back(m.sign());
        }
    }
    for (bbcoop::TeamMember& m : *team.mutable_members()) {
        if (m.member() == team.leader() || m.sign() != 0) {
            continue;
        }
        for (const u32 sign : GuestSigns) {
            if (std::ranges::find(taken, sign) == taken.end()) {
                m.set_sign(sign);
                taken.push_back(sign);
                break;
            }
        }
    }
}

void TeamLobby::Settle() {
    const auto plays = [this](const std::string& member) {
        return StateOf(member) == bbcoop::LobbyPlayer::PLAYING;
    };
    for (bbcoop::Team& team : teams) {
        if (team.started() && !AnyPlays(team, plays)) {
            // Back at the title, every one: the next session needs a Start again.
            team.set_started(false);
            for (bbcoop::TeamMember& m : *team.mutable_members()) {
                m.set_ready(false);
            }
        }
    }
}

TeamLobby::Outcome TeamLobby::Arrive(const std::string& member, const std::string& name,
                                     bool playing) {
    const bool arrived = !players.contains(member);
    Player& player = players[member];
    const bool changed = arrived || player.name != name || player.playing != playing;
    const bool was_playing = player.playing;
    player.name = name;
    player.playing = playing;
    names[member] = name;
    if (was_playing && !playing) {
        // Back at the title: not ready any more (its game loads nothing now).
        for (bbcoop::Team& team : teams) {
            if (bbcoop::TeamMember* m = MemberIn(team, member)) {
                m->set_ready(false);
            }
        }
    }
    Settle();
    return {.changed = changed};
}

TeamLobby::Outcome TeamLobby::Leave(const std::string& member) {
    if (players.erase(member) == 0) {
        return {};
    }
    for (bbcoop::Team& team : teams) {
        if (bbcoop::TeamMember* m = MemberIn(team, member)) {
            m->set_ready(false);
        }
    }
    Settle();
    return {.changed = true};
}

TeamLobby::Outcome TeamLobby::Request(const std::string& member,
                                      const bbcoop::TeamRequest& request) {
    const auto refuse = [](std::string why) { return Outcome{.notice = std::move(why)}; };
    if (!players.contains(member)) {
        return refuse("not signed in to the lobby");
    }
    bbcoop::Team* mine = const_cast<bbcoop::Team*>(TeamOf(member));
    using Action = bbcoop::TeamRequest;
    switch (request.action()) {
    case Action::CREATE: {
        if (mine) {
            return refuse("you are in a team already: leave it first");
        }
        std::string id = make_id();
        while (FindTeam(id)) {
            id = make_id();
        }
        bbcoop::Team& team = teams.emplace_back();
        team.set_id(id);
        team.set_leader(member);
        bbcoop::TeamMember& m = *team.add_members();
        m.set_member(member);
        m.set_name(names[member]);
        return {.changed = true};
    }
    case Action::INVITE: {
        if (!mine) {
            return refuse("create a team first");
        }
        if (static_cast<size_t>(mine->members_size()) >= TeamSize) {
            return refuse("the team is full");
        }
        if (!players.contains(request.member())) {
            return refuse("that player is not online");
        }
        if (MemberIn(*mine, request.member())) {
            return refuse("already in your team");
        }
        auto& list = invites[request.member()];
        if (std::none_of(list.begin(), list.end(),
                         [&](const bbcoop::TeamInvite& i) { return i.team() == mine->id(); })) {
            bbcoop::TeamInvite& invite = list.emplace_back();
            invite.set_team(mine->id());
            invite.set_from(member);
            invite.set_from_name(names[member]);
        }
        return {.changed = true};
    }
    case Action::ACCEPT:
    case Action::DECLINE: {
        auto& list = invites[member];
        const auto invite = std::find_if(list.begin(), list.end(),
                                         [&](const auto& i) { return i.team() == request.team(); });
        if (invite == list.end()) {
            return refuse("no such invite");
        }
        if (request.action() == Action::DECLINE) {
            list.erase(invite);
            return {.changed = true};
        }
        if (mine) {
            return refuse("you are in a team already: leave it first");
        }
        bbcoop::Team* team = FindTeam(request.team());
        if (!team) {
            list.erase(invite);
            return {.changed = true, .notice = "that team is gone"};
        }
        if (static_cast<size_t>(team->members_size()) >= TeamSize) {
            return refuse("the team is full");
        }
        list.erase(invite);
        bbcoop::TeamMember& m = *team->add_members();
        m.set_member(member);
        m.set_name(names[member]);
        GiveSigns(*team);
        return {.changed = true};
    }
    case Action::LEAVE:
        if (!mine) {
            return refuse("you are not in a team");
        }
        RemoveFrom(*mine, member);
        return {.changed = true};
    case Action::REMOVE:
        if (!mine || mine->leader() != member) {
            return refuse("only the team's leader removes members");
        }
        if (request.member() == member || !MemberIn(*mine, request.member())) {
            return refuse("not a member you can remove");
        }
        RemoveFrom(*mine, request.member());
        return {.changed = true};
    case Action::READY:
    case Action::UNREADY: {
        if (!mine) {
            return refuse("you are not in a team");
        }
        bbcoop::TeamMember& m = *MemberIn(*mine, member);
        const bool ready = request.action() == Action::READY;
        m.set_ready(ready);
        Outcome outcome{.changed = true};
        if (ready) {
            m.set_slot(request.slot());
            m.set_character(request.character());
            if (mine->started()) {
                outcome.starts.push_back(member); // the team plays: in at once
            }
        }
        return outcome;
    }
    case Action::START: {
        if (!mine || mine->leader() != member) {
            return refuse("only the team's leader starts");
        }
        if (!MemberIn(*mine, member)->ready()) {
            return refuse("choose your character first");
        }
        // Kaniel 2026-10-14: the hunt begins only once every member is ready (a started team's
        // leader back from a crash goes in at once).
        if (!mine->started()) {
            for (const bbcoop::TeamMember& m : mine->members()) {
                if (!m.ready() || !players.contains(m.member())) {
                    return refuse("not every hunter is ready");
                }
            }
        }
        mine->set_started(true);
        Outcome outcome{.changed = true};
        for (const bbcoop::TeamMember& m : mine->members()) {
            if (m.ready() && players.contains(m.member())) {
                outcome.starts.push_back(m.member());
            }
        }
        return outcome;
    }
    default:
        return refuse("unknown request");
    }
}

bbcoop::LobbyState TeamLobby::StateFor(const std::string& member) const {
    bbcoop::LobbyState state;
    if (const bbcoop::Team* team = TeamOf(member)) {
        bbcoop::Team& out = *state.mutable_team();
        out = *team;
        for (bbcoop::TeamMember& m : *out.mutable_members()) {
            m.set_state(StateOf(m.member()));
        }
    }
    if (const auto list = invites.find(member); list != invites.end()) {
        for (const bbcoop::TeamInvite& invite : list->second) {
            *state.add_invites() = invite;
        }
    }
    for (const auto& [other, player] : players) {
        if (other == member) {
            continue;
        }
        bbcoop::LobbyPlayer& out = *state.add_players();
        out.set_member(other);
        out.set_name(player.name);
        out.set_state(StateOf(other));
        if (const bbcoop::Team* team = TeamOf(other)) {
            out.set_team(team->id());
        }
    }
    return state;
}

bbcoop::TeamStart TeamLobby::StartFor(const std::string& member) const {
    bbcoop::TeamStart start;
    const bbcoop::Team* team = TeamOf(member);
    if (!team) {
        return start;
    }
    start.set_team(team->id());
    start.set_party(PartyOf(team->id()));
    start.set_host(team->leader());
    for (const bbcoop::TeamMember& m : team->members()) {
        if (m.member() == member) {
            start.set_slot(m.slot());
        }
    }
    return start;
}

std::vector<std::string> TeamLobby::SignedIn() const {
    std::vector<std::string> out;
    for (const auto& [member, player] : players) {
        out.push_back(member);
    }
    return out;
}

bbcoop::StoredTeams TeamLobby::Store() const {
    bbcoop::StoredTeams stored;
    stored.set_version(StoredTeamsVersion);
    for (const bbcoop::Team& team : teams) {
        bbcoop::Team& out = *stored.add_teams();
        out.set_id(team.id());
        out.set_leader(team.leader());
        for (const bbcoop::TeamMember& m : team.members()) {
            bbcoop::TeamMember& kept = *out.add_members();
            kept.set_member(m.member());
            kept.set_name(m.name());
            kept.set_slot(m.slot());
            kept.set_character(m.character());
            kept.set_sign(m.sign());
        }
    }
    for (const auto& [to, list] : invites) {
        for (const bbcoop::TeamInvite& invite : list) {
            bbcoop::StoredInvite& out = *stored.add_invites();
            out.set_to(to);
            *out.mutable_invite() = invite;
        }
    }
    return stored;
}

void TeamLobby::Restore(const bbcoop::StoredTeams& stored) {
    teams.assign(stored.teams().begin(), stored.teams().end());
    invites.clear();
    for (const bbcoop::StoredInvite& invite : stored.invites()) {
        invites[invite.to()].push_back(invite.invite());
    }
    for (bbcoop::Team& team : teams) {
        for (const bbcoop::TeamMember& m : team.members()) {
            names[m.member()] = m.name();
        }
        GiveSigns(team); // teams stored before the signs
    }
}

} // namespace BloodborneCoop
