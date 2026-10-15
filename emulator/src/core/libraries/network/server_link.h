// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <string>
#include <string_view>

#include "common/types.h"

// The server link (net_link.h): in the port (always) every
// frame the P2P transport sends to a peer goes through shadNet, and the party's world and boards
// live there (phase 3).
namespace Libraries::Net {

bool ServerLinkSwitchOn();
// The port: a peer's datagrams only through the server, none straight (phase 6).
bool PeersOnlyThroughServer();
// Once signed in: shadNet's matching UDP port (where the STUN pings go) and this game's online id.
void ConfigureServerLink(u32 server_addr_nbo, u16 server_port_nbo, std::string online_id);
// Which game this is, for the hello: shadNet refuses a game on another build.
void SetServerLinkBuild(std::string build);
// The link's events ("welcome", "refused", "quiet", "back", "member-quiet", "member-back",
// "member-gone", "rtt") for the co-op trace.
using ServerLinkObserver = void (*)(std::string_view event, std::string_view detail);
void SetServerLinkObserver(ServerLinkObserver observer);
// Our messages with shadNet itself (the link's Coop kind, phase 3): one stream frame's body each
// way. Send is false while there is no link (no server known, the transport not started); Receive
// gives one body (its size, -1 when none waits).
bool ServerCoopSend(const void* body, u32 len);
int ServerCoopReceive(void* body, u32 len);
// Whether the server took this game's hello (it may restart: then false until it does again).
bool ServerLinkWelcomed();
// Phase 7 (net_link.h ConnectsWithoutHandshake, PingsStun): the port's room members are connected
// at once through the server, and it sends no STUN ping (the server has its address from its
// hello).
bool ConnectsPeersWithoutHandshake();
bool PingsServerStun();
// The rig's dropped connection (autoplay cut-link, phase 7): nothing goes out or comes in on this
// game's UDP socket for `ms`, as when its Wi-Fi drops.
void CutServerLinkForTest(u32 ms);
// Why the server refused this game's hello (another build), empty while it did not.
std::string ServerLinkRefusal();

} // namespace Libraries::Net
