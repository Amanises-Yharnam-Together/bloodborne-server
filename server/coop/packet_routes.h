// SPDX-FileCopyrightText: Copyright 2026 shadNet Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <string_view>

#include "coop/game_link.h"

// Phase 9 (docs/SERVER_COOP_PLAN.md): shadNet reads every game packet it relays and forwards it
// only to a teammate that needs it. The channel table (phase 1's, which each receiving game ran)
// and phase 4/8's enemy owner rules live here, decided where every packet passes: the server,
// which knows each member's map and each map's owner first hand.
namespace BloodborneCoop {

// A game packet is {u32 channel, u32 size, payload}: channels 0..0x43, 0..6 unreliable.
constexpr u32 NativeChannelCount = 0x44;
constexpr bool IsReliableNativeChannel(u32 channel) {
    return channel > 6;
}
// What a reliable packet the server drops becomes: its sequence number stays (the receiver's
// stream would wait for it forever otherwise), the packet is this channel past the game's last
// with nothing in it, and the receiving game hands it to nobody.
constexpr u32 DroppedNativeChannel = NativeChannelCount;

// What happens to each game packet, per channel:
//   Pass     the game's own (join handshake, character data, the unknown channels)
//   SameMap  only between members on one map (players, hits, enemies, events, objects, bullets):
//            a member apart has another copy of the world, so these act on the wrong one
//   Ours     our code carries that state instead (0x2C map items stay each player's, 0x2F the
//            host's door corrections: the world log and the map state board)
//   Drop     nobody's (0x27, the boss "thanks" kick)
enum class ChannelPolicy : u8 { Pass, SameMap, Ours, Drop };

constexpr ChannelPolicy NativeChannelPolicy(u32 channel) {
    switch (channel) {
    case 0x04: // enemy positions
    case 0x05: // enemy HP
    case 0x06: // player state
    case 0x0E: // Lua event calls
    case 0x0F: // CompleteEvent flags
    case 0x12: // throws
    case 0x14: // hits on players
    case 0x15: // a guest's hit on an enemy
    case 0x16:
    case 0x17: // enemy near lists
    case 0x24:
    case 0x25: // forced animations
    case 0x29: // enemy join catch-up
    case 0x2A: // breaks
    case 0x2B: // object animations
    case 0x2D: // object acts
    case 0x2E: // object enable
    case 0x30: // throw events
    case 0x31: // EMEVD instance sync
    case 0x34: // enemy hand-over block
    case 0x36: // enemy join animations
    case 0x37: // throw pair
    case 0x3D: // enemy AI goal records
    case 0x3E: // new bullets
    case 0x40: // bullet updates
    case 0x41: // enemy per-hit record
        return ChannelPolicy::SameMap;
    case 0x2C: // map items
    case 0x2F: // the host's object state correction
        return ChannelPolicy::Ours;
    case 0x27: // the boss "thanks" kick
        return ChannelPolicy::Drop;
    default:
        return ChannelPolicy::Pass;
    }
}

constexpr std::string_view ChannelPolicyName(ChannelPolicy policy) {
    switch (policy) {
    case ChannelPolicy::SameMap:
        return "same map";
    case ChannelPolicy::Ours:
        return "ours";
    case ChannelPolicy::Drop:
        return "drop";
    case ChannelPolicy::Pass:
        break;
    }
    return "pass";
}

// A member's part in its map's enemies (phase 8: one game runs every enemy on a map).
enum class EnemyPart : u8 { None, Owner, Follower };

// Positions, HP, catch-up and join animations: what only the enemies' owner sends.
constexpr bool IsOwnersEnemyChannel(u32 channel) {
    return channel == 0x04 || channel == 0x05 || channel == 0x29 || channel == 0x36;
}

// The two ends of a packet as the server knows them: each one's map (0 while it loads or before it
// said one) and its part in that map's enemies.
struct PacketEnds {
    u32 sender_map = 0;
    u32 target_map = 0;
    EnemyPart sender = EnemyPart::None;
    EnemyPart target = EnemyPart::None;
};

// Whether a packet on `channel` goes from the sender to the target.
constexpr bool RoutesNativePacket(u32 channel, const PacketEnds& ends) {
    if (channel >= NativeChannelCount) {
        return false;
    }
    switch (NativeChannelPolicy(channel)) {
    case ChannelPolicy::Pass:
        return true;
    case ChannelPolicy::Ours:
    case ChannelPolicy::Drop:
        return false;
    case ChannelPolicy::SameMap:
        break;
    }
    // A map not known (a load, or not said yet) lets them through: the game's own behavior.
    if (ends.sender_map != 0 && ends.target_map != 0 && ends.sender_map != ends.target_map) {
        return false;
    }
    // A follower's near list (0x16) goes to the owner only: the owner's game sends a member the
    // state of the enemies on its list, attacks too. The owner's answer (0x17), which hands
    // enemies away, never goes.
    if (channel == 0x16) {
        return ends.sender == EnemyPart::Follower && ends.target == EnemyPart::Owner;
    }
    if (channel == 0x17) {
        return false;
    }
    // A hit on an enemy goes to whoever runs it: never from the owner (it applied its own), never
    // to a follower.
    if (channel == 0x15) {
        return ends.sender != EnemyPart::Owner && ends.target != EnemyPart::Follower;
    }
    // The enemies themselves come from the owner only; with no owner, the game's own way.
    if (IsOwnersEnemyChannel(channel)) {
        switch (ends.target) {
        case EnemyPart::None:
            return ends.sender != EnemyPart::Follower;
        case EnemyPart::Follower:
            return ends.sender == EnemyPart::Owner;
        case EnemyPart::Owner:
            return false;
        }
    }
    return true;
}

// The channel of a packet ({u32 channel, u32 size, payload}), or NativeChannelCount when short.
inline u32 NativePacketChannel(std::span<const u8> packet) {
    if (packet.size() < 8) {
        return NativeChannelCount;
    }
    return u32{packet[0]} | u32{packet[1]} << 8 | u32{packet[2]} << 16 | u32{packet[3]} << 24;
}

// The packet a dropped reliable entry carries: DroppedNativeChannel, empty.
inline std::vector<u8> DroppedNativePacket() {
    std::vector<u8> packet;
    GameLinkDetail::Put(packet, DroppedNativeChannel);
    GameLinkDetail::Put(packet, u32{0});
    return packet;
}

// The frame without the entries `routes(channel)` refuses: an unreliable one goes, a reliable one
// keeps its place and sequence number as a DroppedNativePacket. The number of entries refused (one
// dropped already stays as it is).
template <typename Routes>
size_t RouteGameFrame(GameFrame& frame, Routes&& routes) {
    size_t refused = 0;
    std::vector<GameFrameEntry> kept;
    kept.reserve(frame.entries.size());
    for (GameFrameEntry& entry : frame.entries) {
        const u32 channel = NativePacketChannel(entry.packet);
        if (channel == DroppedNativeChannel && entry.packet_class == PacketClass::Reliable) {
            kept.push_back(std::move(entry));
            continue;
        }
        if (routes(channel)) {
            kept.push_back(std::move(entry));
            continue;
        }
        ++refused;
        if (entry.packet_class == PacketClass::Reliable) {
            entry.packet = DroppedNativePacket();
            kept.push_back(std::move(entry));
        }
    }
    frame.entries = std::move(kept);
    return refused;
}

} // namespace BloodborneCoop
