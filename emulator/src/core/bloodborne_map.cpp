// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>

#include "core/bloodborne_lamp_points.h"
#include "core/bloodborne_map.h"
#include "core/bloodborne_party.h"

namespace Core::Bloodborne {

namespace {

constexpr s32 LampWarpBase = 2950; // a lamp object's entity id + 1000 (LampOfActiveFlag's)
constexpr s32 LampsPerMap = 8;     // 2958 and 2959: a boss arena's points
constexpr s32 ActiveFlagBase = 10007810;

// The map (e.g. 241) and the lamp's index on it of a lamp's warp point; nothing for any other.
std::optional<std::pair<s32, s32>> MapAndIndex(s32 warp_point) {
    const s32 within = warp_point % 10000 - LampWarpBase;
    if (warp_point <= 0 || within < 0 || within >= LampsPerMap) {
        return std::nullopt;
    }
    return std::pair{warp_point / 10000, within};
}

} // namespace

const std::vector<MapLamp>& MapLamps() {
    static const std::vector<MapLamp> lamps = [] {
        std::vector<MapLamp> list;
        for (const LampPoint& point : LampPoints) {
            const bool dream = point.packed_map == PackedMapOfWarpPoint(HuntersDreamWarpPoint);
            if (dream ? point.warp_point != HuntersDreamWarpPoint
                      : !MapAndIndex(point.warp_point)) {
                continue;
            }
            list.push_back(MapLamp{point.warp_point, point.packed_map, point.x, point.y, point.z});
        }
        std::ranges::sort(list, {}, &MapLamp::warp_point);
        return list;
    }();
    return lamps;
}

const MapLamp* FindMapLamp(s32 warp_point) {
    const auto& lamps = MapLamps();
    const auto found = std::ranges::lower_bound(lamps, warp_point, {}, &MapLamp::warp_point);
    return found != lamps.end() && found->warp_point == warp_point ? &*found : nullptr;
}

std::optional<s32> LampActiveFlag(s32 warp_point) {
    const auto lamp = MapAndIndex(warp_point);
    if (!lamp || warp_point == HuntersDreamWarpPoint) {
        return std::nullopt;
    }
    return ActiveFlagBase + lamp->first * 10000 + lamp->second * 20;
}

s32 LampNameId(s32 warp_point) {
    if (warp_point == HuntersDreamWarpPoint) {
        return 70002100;
    }
    return 70000000 + warp_point / 10000 * 10 + (warp_point % 10000 - LampWarpBase);
}

s32 AreaNameId(u32 packed_map) {
    return static_cast<s32>((packed_map >> 24) * 100 + ((packed_map >> 16) & 0xFF) * 10);
}

MapTravelBlock MapTravelCheck(const MapTravelState& state) {
    if (!state.in_world) {
        return MapTravelBlock::NotInWorld;
    }
    if (state.travelling) {
        return MapTravelBlock::Travelling;
    }
    if (state.down) {
        return MapTravelBlock::Down;
    }
    if (state.boss_fight) {
        return MapTravelBlock::BossFight;
    }
    if (!state.lit) {
        return MapTravelBlock::NotLit;
    }
    return MapTravelBlock::None;
}

bool MapTripArrived(const MapTripProgress& trip, u32 map_now, bool loading_now) {
    constexpr double NoLoadSeconds = 8.0;
    if (loading_now || map_now != trip.target_map) {
        return false;
    }
    return trip.load_seen || trip.seconds >= NoLoadSeconds;
}

} // namespace Core::Bloodborne
