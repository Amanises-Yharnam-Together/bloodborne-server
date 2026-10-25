// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <cmath>
#include <tuple>

#include "core/bloodborne_duel.h"

namespace Core::Bloodborne {

namespace {

const bbcoop::Duelist* Seat(const bbcoop::DuelState& state, std::string_view member) {
    for (const bbcoop::Duelist& duelist : state.duelists()) {
        if (duelist.member() == member) {
            return &duelist;
        }
    }
    return nullptr;
}

DuelPart PartOf(const bbcoop::DuelState& state, std::string_view self) {
    const bbcoop::Duelist* seat = Seat(state, self);
    if (seat == nullptr || seat->answer() == 2) {
        return DuelPart::None;
    }
    switch (state.stage()) {
    case bbcoop::DuelState::ASKING:
        return seat->answer() == 1 ? DuelPart::Answered : DuelPart::Asked;
    case bbcoop::DuelState::COUNTDOWN:
        return seat->answer() == 1 ? DuelPart::Countdown : DuelPart::None;
    case bbcoop::DuelState::FIGHTING:
        return seat->answer() != 1 ? DuelPart::None
               : seat->out()       ? DuelPart::Out
                                   : DuelPart::Fighting;
    default:
        return DuelPart::None;
    }
}

bool InDuelPart(DuelPart part) {
    return part == DuelPart::Countdown || part == DuelPart::Fighting || part == DuelPart::Out;
}

} // namespace

DuelNews DuelFollower::Take(const bbcoop::DuelState& next, std::string_view me) {
    DuelNews news;
    if (!next.refused().empty()) {
        news.refused = next.refused();
    }
    // A state older than the one followed (a resend) changes nothing.
    if (next.duel() < state.duel()) {
        return news;
    }
    const DuelPart before = part;
    const bool was_in = InDuelPart(before);
    self = me;
    state = next;
    state.clear_refused();
    part = PartOf(state, self);
    news.asked = part == DuelPart::Asked && before != DuelPart::Asked;
    news.joined = InDuelPart(part) && !was_in;
    news.fight = part == DuelPart::Fighting && before != DuelPart::Fighting;
    news.out = part == DuelPart::Out && before != DuelPart::Out;
    if (was_in && !InDuelPart(part)) {
        news.over = true;
        if (state.stage() == bbcoop::DuelState::OVER && state.end() == bbcoop::DuelState::WON) {
            const bbcoop::Duelist* mine = Seat(state, self);
            const bbcoop::Duelist* winner = Seat(state, state.winner());
            news.won = mine != nullptr && winner != nullptr && mine->side() == winner->side();
            news.lost = !news.won;
        }
    }
    return news;
}

std::optional<u32> DuelFollower::AskedDuel() const {
    return part == DuelPart::Asked ? std::optional{state.duel()} : std::nullopt;
}

std::optional<u32> DuelFollower::InDuel() const {
    return InDuelPart(part) ? std::optional{state.duel()} : std::nullopt;
}

std::vector<std::string> DuelFollower::Foes() const {
    std::vector<std::string> foes;
    const bbcoop::Duelist* mine = Seat(state, self);
    if (part != DuelPart::Fighting || mine == nullptr) {
        return foes;
    }
    for (const bbcoop::Duelist& duelist : state.duelists()) {
        if (duelist.answer() == 1 && !duelist.out() && duelist.side() != mine->side()) {
            foes.push_back(duelist.member());
        }
    }
    return foes;
}

bool DuelFollower::MayPropose() const {
    return state.stage() == bbcoop::DuelState::STAGE_NONE ||
           state.stage() == bbcoop::DuelState::OVER;
}

DuelGiveBack DuelGiveBackOf(const DuelSupplies& before, const DuelSupplies& now, s32 max_hp) {
    DuelGiveBack back;
    const s32 hp = std::clamp(before.hp, 1, std::max(max_hp, 1));
    if (now.hp != hp) {
        back.hp = hp;
    }
    back.vials = before.vials > now.vials ? before.vials - now.vials : 0;
    back.bullets = before.bullets > now.bullets ? before.bullets - now.bullets : 0;
    return back;
}

std::vector<s32> DuelAilmentsToEnd(std::span<const s32> before, std::span<const s32> now) {
    std::vector<s32> end;
    for (const s32 effect : now) {
        if (std::ranges::find(DuelAilmentEffects, effect) != DuelAilmentEffects.end() &&
            std::ranges::find(before, effect) == before.end() &&
            std::ranges::find(end, effect) == end.end()) {
            end.push_back(effect);
        }
    }
    return end;
}

std::vector<u32> DuelFoeBodies(std::span<const DuelBody> bodies,
                               std::span<const DuelMemberAt> others,
                               std::span<const std::string> foes) {
    std::vector<u32> hostile;
    if (foes.empty()) {
        return hostile;
    }
    const bool only_foes = std::ranges::all_of(others, [&](const DuelMemberAt& other) {
        return std::ranges::find(foes, other.member) != foes.end();
    });
    if (only_foes) {
        for (const DuelBody& body : bodies) {
            hostile.push_back(body.handle);
        }
        return hostile;
    }
    // Each (body, member) pair within reach, the nearest first; each body and member once.
    std::vector<std::tuple<float, size_t, size_t>> pairs;
    for (size_t b = 0; b < bodies.size(); ++b) {
        for (size_t m = 0; m < others.size(); ++m) {
            const auto& p = bodies[b].at;
            const auto& q = others[m].at;
            const float distance = std::hypot(p[0] - q[0], p[1] - q[1], p[2] - q[2]);
            if (distance <= DuelBodyReach) {
                pairs.emplace_back(distance, b, m);
            }
        }
    }
    std::ranges::sort(pairs);
    std::vector<bool> body_taken(bodies.size()), member_taken(others.size());
    for (const auto& [distance, b, m] : pairs) {
        if (body_taken[b] || member_taken[m]) {
            continue;
        }
        body_taken[b] = member_taken[m] = true;
        if (std::ranges::find(foes, others[m].member) != foes.end()) {
            hostile.push_back(bodies[b].handle);
        }
    }
    return hostile;
}

} // namespace Core::Bloodborne
