// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

namespace Libraries::Np::NpSignaling {

// A connection both sides deactivated is only kept for its 60 s keepalive linger before DEAD.
// A new activation in that window must start a new connection: reusing the lingering one sent
// the peer an activation on a connection its game had already deactivated, and that game never
// heard the invite (the host's second summon of the guest went nowhere).
inline bool LingeringConnectionIsFinished(bool established, bool locally_deactivated,
                                          bool peer_deactivated, bool dead_fired) {
    return established && locally_deactivated && peer_deactivated && !dead_fired;
}

} // namespace Libraries::Np::NpSignaling
