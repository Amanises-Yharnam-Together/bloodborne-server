// SPDX-FileCopyrightText: Copyright 2026 shadNet Project
// SPDX-License-Identifier: GPL-2.0-or-later
#include "bloodborne_party_boards.h"

#include <algorithm>

namespace BloodborneWorld {

namespace {

constexpr std::uint32_t VitalUp = 1;
constexpr std::uint32_t VitalDown = 2;

bool Same(const google::protobuf::MessageLite& a, const google::protobuf::MessageLite& b) {
    return a.SerializeAsString() == b.SerializeAsString();
}

} // namespace

unsigned PartyBoards::Map(const std::string& member, std::uint32_t map, std::int64_t now_ms) {
    MapEntry& entry = maps[member];
    const bool changed = entry.at_ms == 0 || entry.map != map;
    entry.map = map;
    entry.at_ms = now_ms;
    return changed ? Maps : None;
}

unsigned PartyBoards::Rest(const std::string& member, const bbcoop::Rest& rest,
                           std::int64_t now_ms) {
    if (rest.lamp() == 0) {
        return rests.erase(member) > 0 ? Rests : None;
    }
    RestEntry& entry = rests[member];
    const bool changed = entry.at_ms == 0 || !Same(entry.rest, rest);
    entry.rest = rest;
    entry.at_ms = now_ms;
    return changed ? Rests : None;
}

unsigned PartyBoards::Trophies(const bbcoop::Trophies& message) {
    unsigned changed = None;
    for (const std::int32_t id : message.ids()) {
        if (id >= 0 && id <= MaxTrophyId && trophies.insert(id).second) {
            changed = TrophyBoard;
        }
    }
    return changed;
}

unsigned PartyBoards::Vitals(const std::string& member, const bbcoop::Vitals& message,
                             std::int64_t now_ms) {
    VitalsEntry& entry = vitals[member];
    unsigned changed = entry.at_ms == 0 || !Same(entry.vitals, message) ? VitalsBoard : None;
    entry.vitals = message;
    entry.at_ms = now_ms;
    const std::uint32_t map = message.map();
    if (map == 0) {
        return changed;
    }
    bool standing = false;
    std::vector<std::string> down;
    for (const auto& [name, other] : vitals) {
        if (other.vitals.map() != map) {
            continue;
        }
        standing = standing || other.vitals.vital() == VitalUp;
        const bool wiped = std::ranges::any_of(wipes, [&](const Wipe& wipe) {
            return std::ranges::find(wipe.wipe.members(), name) != wipe.wipe.members().end();
        });
        if (other.vitals.vital() == VitalDown && !wiped) {
            down.push_back(name);
        }
    }
    if (!standing && !down.empty()) {
        Wipe wipe;
        wipe.wipe.set_id(++last_wipe);
        wipe.wipe.set_map(map);
        for (const std::string& name : down) {
            wipe.wipe.add_members(name);
        }
        wipe.declared_ms = now_ms;
        wipes.push_back(std::move(wipe));
        changed |= VitalsBoard;
    }
    return changed;
}

unsigned PartyBoards::Expire(std::int64_t now_ms) {
    unsigned changed = None;
    const auto stale = [now_ms](const auto& entry) {
        return now_ms - entry.second.at_ms > FreshMs;
    };
    if (std::erase_if(maps, stale) > 0) {
        changed |= Maps;
    }
    if (std::erase_if(rests, stale) > 0) {
        changed |= Rests;
    }
    if (std::erase_if(vitals, stale) > 0) {
        changed |= VitalsBoard;
    }
    if (std::erase_if(
            wipes, [now_ms](const Wipe& wipe) { return now_ms - wipe.declared_ms > WipeMs; }) > 0) {
        changed |= VitalsBoard;
    }
    return changed;
}

unsigned PartyBoards::Forget(const std::string& member) {
    unsigned changed = None;
    changed |= maps.erase(member) > 0 ? Maps : None;
    changed |= rests.erase(member) > 0 ? Rests : None;
    changed |= vitals.erase(member) > 0 ? VitalsBoard : None;
    return changed;
}

bbcoop::MapBoard PartyBoards::MapMessage() const {
    bbcoop::MapBoard board;
    for (const auto& [member, entry] : maps) {
        bbcoop::MapBoard::Entry* out = board.add_members();
        out->set_member(member);
        out->set_map(entry.map);
    }
    return board;
}

bbcoop::RestBoard PartyBoards::RestMessage() const {
    bbcoop::RestBoard board;
    for (const auto& [member, entry] : rests) {
        bbcoop::RestBoard::Entry* out = board.add_rests();
        out->set_member(member);
        out->set_lamp(entry.rest.lamp());
        if (entry.rest.has_at()) {
            *out->mutable_at() = entry.rest.at();
        }
    }
    return board;
}

bbcoop::Trophies PartyBoards::TrophyMessage() const {
    bbcoop::Trophies board;
    for (const std::int32_t id : trophies) {
        board.add_ids(id);
    }
    return board;
}

bbcoop::VitalsBoard PartyBoards::VitalsMessage() const {
    bbcoop::VitalsBoard board;
    for (const auto& [member, entry] : vitals) {
        bbcoop::VitalsBoard::Member* out = board.add_members();
        out->set_member(member);
        *out->mutable_vitals() = entry.vitals;
        out->mutable_vitals()->clear_hits();
        std::uint32_t damage = 0;
        for (const auto& [striker, other] : vitals) {
            for (const bbcoop::ReviveHit& hit : other.vitals.hits()) {
                if (hit.target() == member && hit.down() == entry.vitals.down()) {
                    damage += hit.damage();
                }
            }
        }
        out->set_revive_damage(damage);
    }
    for (const Wipe& wipe : wipes) {
        *board.add_wipes() = wipe.wipe;
    }
    return board;
}

} // namespace BloodborneWorld
