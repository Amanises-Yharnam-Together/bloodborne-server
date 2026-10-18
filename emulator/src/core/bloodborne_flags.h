// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <array>
#include <limits>
#include <span>
#include <string_view>

#include "common/types.h"

namespace Core::Bloodborne {

// Who decides an event flag's value in a party (see docs/FLAGS.md).
enum class FlagOwner : u8 {
    // The host's world: a guest's copy follows the host in a session.
    Host,
    // Each player's own: the other player's "set this event flag" and "event ended" messages
    // leave it alone (KeepsLocalFlag).
    PerPlayer,
    // The party's: posted to the party's flag board, and every member sets it when one sees it
    // set (SharedWithParty), since the game does not sync it itself.
    Shared,
    // Only this session or map visit: never kept for a guest's own world nor shared.
    Session,
};

// What else the co-op runtime does with a flag (bits of FlagRule::behaviour).
enum FlagBehaviour : u32 {
    // A guest keeps its change in a session for its own world (CarriesToGuestWorld).
    FlagCarriesHome = 1u << 0,
    // A guest loading the host's map takes the host's value (TakesHostMapFlag).
    FlagTakesHostMap = 1u << 1,
    // A map item's pickup: a guest keeps its own pickups in their own list (IsMapPickupFlag).
    FlagMapPickup = 1u << 2,
    // A lamp's "travel to the Hunter's Dream" request (IsLampTravelFlag).
    FlagLampTrip = 1u << 3,
    // A Dream headstone's "travel to <lamp>" request (IsHeadstoneTravelFlag).
    FlagHeadstoneTrip = 1u << 4,
    // A boss fog's "host entered" flag (IsBossFogEntryFlag).
    FlagBossFogEntry = 1u << 5,
    // A guest out in the world in a session sets it for itself (GuestProgressFlags).
    FlagGuestSetsInWorld = 1u << 6,
    // A messenger weapon line cleared once per save slot (ShopLineFlags).
    FlagShopLine = 1u << 7,
    // A Shared flag's clears go to the party too, not only its sets (FlagsToShare).
    FlagSharesClears = 1u << 8,
};

struct FlagRule {
    FlagOwner owner = FlagOwner::Host;
    u32 behaviour = 0;
    constexpr bool Has(u32 bits) const {
        return (behaviour & bits) == bits;
    }
    bool operator==(const FlagRule&) const = default;
};

// Flags first..last (inclusive). With a period, only the flags whose flag % period lies in
// [low, high], and with a stride too, of those only the first `width` of every `stride`.
struct FlagRange {
    s32 first = 0;
    s32 last = 0;
    s32 period = 0;
    s32 low = 0;
    s32 high = 0;
    s32 stride = 0;
    s32 width = 0;
    FlagRule rule{};
    std::string_view why;
    constexpr bool Contains(s32 flag) const {
        if (flag < first || flag > last) {
            return false;
        }
        if (period == 0) {
            return true;
        }
        const s32 within = flag % period;
        if (within < low || within > high) {
            return false;
        }
        return stride == 0 || (within - low) % stride < width;
    }
};

// The flag each boss fog's host event sets and its phantom event waited for (found by
// party_boss_events.py's open_boss_fog_for_guests in the 01.09 map event files). Party guests
// set it too, and it holds for one map visit; the flag after it is "a phantom entered" (local
// music).
inline constexpr std::array<s32, 21> BossFogEntryFlags{
    12104800, 12104850, 12204800, 12304700, 12304800, 12404800, 12414700,
    12414800, 12424700, 12424800, 12504800, 12604800, 12604850, 12704800,
    12804800, 13304800, 13404808, 13404858, 13504808, 13504858, 13604808,
};

// The Dream's m21_00 event 12100450 sets each of these flags once the player holds its goods (the
// workshop's tools, the hunter badges). The badges' givers read them: a boss event gives its badge
// only while the flag is off (the Cleric Beast's 12411700 its Sword Hunter Badge by 6645, m21_00
// 12101800 by 6642, m23_00 12301700 its Spark Hunter Badge by 6644); common event 9910 marks the
// NPC badge gifts' lots taken by 6641 6643 6647 6649; the maps' start events switch objects by
// 6630 6631 6640 6646 6648.
struct HeldItemFlag {
    s32 flag;
    u32 goods;
};
inline constexpr std::array HeldItemFlags{
    HeldItemFlag{6630, 4103}, // Blood Gem Workshop Tool
    HeldItemFlag{6631, 4104}, // Rune Workshop Tool
    HeldItemFlag{6632, 4105}, // Short Ritual Root Chalice
    HeldItemFlag{6633, 4102}, // Workshop Haze Extractor
    HeldItemFlag{6640, 4110}, // Saw Hunter Badge
    HeldItemFlag{6641, 4112}, // Powder Keg Hunter Badge
    HeldItemFlag{6642, 4113}, // Old Hunter Badge
    HeldItemFlag{6643, 4111}, // Crow Hunter Badge
    HeldItemFlag{6644, 4118}, // Spark Hunter Badge
    HeldItemFlag{6645, 4114}, // Sword Hunter Badge
    HeldItemFlag{6646, 4115}, // Radiant Sword Hunter Badge
    HeldItemFlag{6647, 4116}, // Wheel Hunter Badge
    HeldItemFlag{6648, 4119}, // Cosmic Eye Watcher Badge
    HeldItemFlag{6649, 4117}, // Cainhurst Badge
};

namespace FlagTableDetail {

constexpr FlagRange Range(s32 first, s32 last, FlagOwner owner, u32 behaviour,
                          std::string_view why) {
    return FlagRange{.first = first, .last = last, .rule = {owner, behaviour}, .why = why};
}

// Rows before the boss fogs'.
inline constexpr std::array FirstRows{
    Range(std::numeric_limits<s32>::min(), 0, FlagOwner::Session, 0, "not an event flag"),
    Range(4000, 4999, FlagOwner::Session, 0,
          "death and multiplayer bookkeeping (4000 a boss fight on, 4047 a player on the way "
          "out): the session's own"),
    Range(6630, 6633, FlagOwner::PerPlayer, FlagCarriesHome,
          "holds a workshop tool (HeldItemFlags, the Dream's 12100450): what this player holds"),
    Range(6640, 6649, FlagOwner::PerPlayer, FlagCarriesHome,
          "holds a hunter badge (HeldItemFlags): what this player holds; as the host's (a team "
          "world) a guest never got its own Sword Hunter Badge from the Cleric Beast"),
    Range(9180, 9199, FlagOwner::Session, 0,
          "the bell's bookkeeping (common events 9181-9198), not progress; kept for the "
          "guest's world it gave Insight again"),
    Range(9300, 9307, FlagOwner::PerPlayer, FlagCarriesHome,
          "a boss's \"found\" flag (EncounterInsights): each player gets the Insight for "
          "finding a boss"),
    Range(9336, 9336, FlagOwner::PerPlayer, FlagCarriesHome, "a boss's \"found\" flag"),
    Range(9338, 9344, FlagOwner::PerPlayer, FlagCarriesHome, "a boss's \"found\" flag"),
    Range(9346, 9348, FlagOwner::PerPlayer, FlagCarriesHome, "a boss's \"found\" flag"),
    Range(9401, 9401, FlagOwner::Host, FlagCarriesHome | FlagGuestSetsInWorld,
          "Central Yharnam reached (the Dream's first visit, its shop): a phantom in the "
          "host's world never sets it, so a guest out in the world sets it for itself"),
    Range(9403, 9403, FlagOwner::Host, FlagCarriesHome | FlagGuestSetsInWorld,
          "the workshop's doors open (Central Yharnam's 12410900 sets it for a live player "
          "only): set by a guest out in the world for itself"),
    Range(12101000, 12101009, FlagOwner::PerPlayer, FlagCarriesHome | FlagShopLine,
          "the messengers' weapon lines (Dream event 12101000, slot 0-9 once the player has its "
          "badge; run in each player's own game): each player's follow its own badges; cleared "
          "once per save slot after a broken patch set them all"),
    Range(12101020, 12101029, FlagOwner::PerPlayer, FlagCarriesHome,
          "the Dream messengers' gift events and flags: what one player received is not given "
          "to the other"),
};

// Rows after the boss fogs': the general ranges the earlier rows cut into.
inline constexpr std::array LastRows{
    FlagRange{.first = 12107810,
              .last = 13998191,
              .period = 10000,
              .low = 7810,
              .high = 8191,
              .stride = 20,
              .width = 2,
              .rule = {FlagOwner::Shared, FlagCarriesHome | FlagTakesHostMap},
              .why = "a lamp's active flag (10007810 + map * 10000 + lamp * 20, maps 210-399, "
                     "lamps 0-19) and the one after: the game does not sync them"},
    Range(12000000, 13999999, FlagOwner::Host, FlagCarriesHome | FlagTakesHostMap,
          "the maps' flags (1AABxxxx: doors, shortcuts, bosses, enemies): the host's world"),
    Range(50000000, 52899999, FlagOwner::Host, FlagMapPickup,
          "map item pickups (ItemLotParam getItemFlagId 5[0-3]MMxxxx): a guest's own list"),
    Range(52900000, 52999999, FlagOwner::Host, FlagCarriesHome,
          "chalice dungeon item flags: left out of the pickups, kept like any other change"),
    Range(53000000, 53999999, FlagOwner::Host, FlagMapPickup, "map item pickups"),
    Range(72101000, 72101099, FlagOwner::PerPlayer, FlagCarriesHome,
          "the pick in the Dream messengers' talk: each player's own"),
    Range(72102000, 72103999, FlagOwner::Session, FlagHeadstoneTrip,
          "a Dream headstone's \"travel to <lamp>\" (7210MMML for event 12107000): a request, "
          "not progress"),
    FlagRange{.first = 72110000,
              .last = 73999999,
              .period = 10000,
              .low = 100,
              .high = 199,
              .rule = {FlagOwner::Session, FlagLampTrip},
              .why = "a lamp's \"travel to the Hunter's Dream\" (7MMM01LL, map codes 211-399; "
                     "the Dream's 721001xx is the Doll's, 70000140 Gilbert's talk, 799001xx "
                     "the NPCs'): a request, not progress"},
    FlagRange{.first = 94000000,
              .last = 94905999,
              .period = 100000,
              .low = 0,
              .high = 5999,
              .rule = {FlagOwner::Shared, FlagCarriesHome | FlagSharesClears},
              .why = "a ritual altar's dungeon (altar k: 94k00000-94k05999, its record packed "
                     "into the flags, 94k05001 a chalice on it): team altars, a ritual or a join "
                     "at one member's altar puts the dungeon on every member's"},
};

constexpr std::array<FlagRange, BossFogEntryFlags.size() * 2> BossFogRows() {
    std::array<FlagRange, BossFogEntryFlags.size() * 2> rows{};
    for (size_t index = 0; index < BossFogEntryFlags.size(); ++index) {
        const s32 flag = BossFogEntryFlags[index];
        rows[index * 2] =
            Range(flag, flag, FlagOwner::Shared, FlagBossFogEntry,
                  "a boss fog's \"host entered\" flag: a guest's (unsynced) fog event sets it "
                  "to start the boss; holds for one map visit");
        rows[index * 2 + 1] = Range(flag + 1, flag + 1, FlagOwner::Session, 0,
                                    "a boss fog's \"phantom entered\" flag (local music)");
    }
    return rows;
}

constexpr auto MakeFlagTable() {
    constexpr auto fogs = BossFogRows();
    std::array<FlagRange, FirstRows.size() + fogs.size() + LastRows.size()> table{};
    size_t at = 0;
    for (const FlagRange& row : FirstRows) {
        table[at++] = row;
    }
    for (const FlagRange& row : fogs) {
        table[at++] = row;
    }
    for (const FlagRange& row : LastRows) {
        table[at++] = row;
    }
    return table;
}

} // namespace FlagTableDetail

// Every event flag's party rule, most specific row first: a flag takes the first row that
// contains it. Rows only overlap when a later row's range holds an earlier one whole (the map
// flags hold the boss fogs and the lamps' active flags); a test checks that.
inline constexpr auto FlagTable = FlagTableDetail::MakeFlagTable();

// Any other flag: the host's world, kept for the guest's own (quest steps, NPCs, the Dream).
inline constexpr FlagRule EveryOtherFlag{FlagOwner::Host, FlagCarriesHome};

constexpr const FlagRange* RowFor(s32 flag) {
    for (const FlagRange& row : FlagTable) {
        if (row.Contains(flag)) {
            return &row;
        }
    }
    return nullptr;
}

constexpr FlagRule RuleFor(s32 flag) {
    const FlagRange* row = RowFor(flag);
    return row != nullptr ? row->rule : EveryOtherFlag;
}

// The flags of the rows with these behaviour bits (rows without a period), in table order.
constexpr size_t CountFlagsWith(u32 bits) {
    size_t count = 0;
    for (const FlagRange& row : FlagTable) {
        if (row.rule.Has(bits) && row.period == 0) {
            count += static_cast<size_t>(row.last - row.first) + 1;
        }
    }
    return count;
}

template <u32 Bits>
constexpr std::array<s32, CountFlagsWith(Bits)> FlagsWith() {
    std::array<s32, CountFlagsWith(Bits)> flags{};
    size_t at = 0;
    for (const FlagRange& row : FlagTable) {
        if (row.rule.Has(Bits) && row.period == 0) {
            for (s32 flag = row.first; flag <= row.last; ++flag) {
                flags[at++] = flag;
            }
        }
    }
    return flags;
}

} // namespace Core::Bloodborne
