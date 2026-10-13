// SPDX-FileCopyrightText: Copyright 2026 shadNet Project
// SPDX-License-Identifier: GPL-2.0-or-later
// The party's boards on the server's world (phase 3c), what the HTTP boards (/bloodborne/party/*)
// were: the map each member is on, who rests at which lamp, the trophies the party unlocked and
// each member's vitals for party death, with the wipes the server declares. Members send their
// own part; every member gets the whole board when it changes (bloodborne_world.h sends it).
#pragma once

#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "coop.pb.h"

namespace BloodborneWorld {

class PartyBoards {
public:
    // A member's map, rest or vitals not heard of for this long is gone (its game went).
    static constexpr std::int64_t FreshMs = 5'000;
    // A wipe stays on the board this long, so every member it takes reads it.
    static constexpr std::int64_t WipeMs = 20'000;
    static constexpr std::int32_t MaxTrophyId = 127;

    enum Changed : unsigned {
        None = 0,
        Maps = 1,
        Rests = 2,
        TrophyBoard = 4,
        VitalsBoard = 8,
    };

    // Each returns the boards that changed (Changed bits).
    unsigned Map(const std::string& member, std::uint32_t map, std::int64_t now_ms);
    unsigned Rest(const std::string& member, const bbcoop::Rest& rest, std::int64_t now_ms);
    unsigned Trophies(const bbcoop::Trophies& trophies);
    // The member's vitals; a map is wiped when nobody there stands and someone there is down that
    // no wipe of the last WipeMs took yet.
    unsigned Vitals(const std::string& member, const bbcoop::Vitals& vitals, std::int64_t now_ms);
    // Parts gone stale and wipes gone old.
    unsigned Expire(std::int64_t now_ms);
    // A member's own events took it out (its leave).
    unsigned Forget(const std::string& member);

    bbcoop::MapBoard MapMessage() const;
    bbcoop::RestBoard RestMessage() const;
    bbcoop::Trophies TrophyMessage() const;
    // Each member as it sent itself (no hits), with the party's revive damage on its down.
    bbcoop::VitalsBoard VitalsMessage() const;

private:
    struct Heard {
        std::int64_t at_ms = 0;
    };
    struct MapEntry : Heard {
        std::uint32_t map = 0;
    };
    struct RestEntry : Heard {
        bbcoop::Rest rest;
    };
    struct VitalsEntry : Heard {
        bbcoop::Vitals vitals;
    };
    struct Wipe {
        bbcoop::VitalsBoard::Wipe wipe;
        std::int64_t declared_ms = 0;
    };

    std::map<std::string, MapEntry> maps;
    std::map<std::string, RestEntry> rests;
    std::set<std::int32_t> trophies;
    std::map<std::string, VitalsEntry> vitals;
    std::vector<Wipe> wipes;
    std::uint32_t last_wipe = 0;
};

} // namespace BloodborneWorld
