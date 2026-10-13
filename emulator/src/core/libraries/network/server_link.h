// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <string>
#include <string_view>

#include "common/types.h"

// The server link (net_link.h): with SHADPS4_BLOODBORNE_SERVER_LINK=1 (the port only, off until
// the rig passes it) every frame the P2P transport sends to a peer goes through shadNet.
namespace Libraries::Net {

bool ServerLinkSwitchOn();
// Once signed in: shadNet's matching UDP port (where the STUN pings go) and this game's online id.
void ConfigureServerLink(u32 server_addr_nbo, u16 server_port_nbo, std::string online_id);
// Which game this is, for the hello: shadNet refuses a game on another build.
void SetServerLinkBuild(std::string build);
// The link's events ("welcome", "refused", "quiet", "back", "member-quiet", "member-back",
// "member-gone", "rtt") for the co-op trace.
using ServerLinkObserver = void (*)(std::string_view event, std::string_view detail);
void SetServerLinkObserver(ServerLinkObserver observer);

} // namespace Libraries::Net
