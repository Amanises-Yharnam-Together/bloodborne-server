// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <chrono>
#include <optional>
#include <string>
#include <string_view>

#include "common/types.h"

namespace Core::Bloodborne {

// Validates a "<name>:<secret>" party setting and returns the header value to send.
std::optional<std::string> ParsePartySetting(std::string_view setting);

enum class PartyRole { Host, Guest };
enum class PartyAction { Wait, RingBell };

constexpr s32 BeckoningBellGoods = 200;
constexpr s32 BeckoningBellEffect = 9001;
constexpr s32 SmallResonantBellGoods = 205;
constexpr s32 SmallResonantBellEffect = 9005;

// What the game looks like this tick, read from guest memory by bloodborne_re.cpp.
struct PartyObservation {
    bool player_valid = false;
    bool map_usable = false;
    bool stage_transition = false;
    bool matching_idle = false; // no multiplayer session, not even a pending one
    bool bell_effect_active = false;
};

// Auto-rejoin for a configured party: whenever this player is back in its own world with no
// session and no active bell, ring the role's bell again (host: Beckoning, guest: Small
// Resonant) once the world has been stable for a while.
class PartyController {
public:
    using Clock = std::chrono::steady_clock;
    static constexpr u32 StableTicksBeforeRing = 20;
    static constexpr Clock::duration RetryDelay = std::chrono::seconds{15};

    explicit PartyController(PartyRole role) : role{role} {}

    s32 BellGoodsId() const {
        return role == PartyRole::Host ? BeckoningBellGoods : SmallResonantBellGoods;
    }
    s32 BellEffectId() const {
        return role == PartyRole::Host ? BeckoningBellEffect : SmallResonantBellEffect;
    }
    PartyAction Tick(const PartyObservation& observation, Clock::time_point now);

private:
    PartyRole role;
    u32 stable_ticks = 0;
    std::optional<Clock::time_point> last_ring;
};

} // namespace Core::Bloodborne
