// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/bloodborne_team.h"

#include <string_view>

namespace Core::Bloodborne {

bool TeamModeEnabled(const char* setting) {
    if (setting == nullptr) {
        return false;
    }
    const std::string_view value{setting};
    return value == "1" || value == "on";
}

TeamGate::Decision TeamGate::Tick(const std::string& me, u32 slot, const std::string& character,
                                  s64 now_ms) {
    Decision decision;
    if (!holding_since) {
        holding_since = now_ms;
    }
    if (started) {
        decision.start = started;
        return decision;
    }
    if (!lobby) {
        if (now_ms - *holding_since >= NoLobbyLoadsAfterMs) {
            return decision; // no server: the save plays alone
        }
        decision.step = Step::Hold;
        return decision;
    }
    if (!lobby->has_team()) {
        return decision;
    }
    decision.step = Step::Hold;
    if (!asked) {
        asked = true;
        bbcoop::TeamRequest& ready = decision.requests.emplace_back();
        ready.set_action(bbcoop::TeamRequest::READY);
        ready.set_slot(slot);
        ready.set_character(character);
        if (lobby->team().leader() == me) {
            decision.requests.emplace_back().set_action(bbcoop::TeamRequest::START);
        }
    }
    return decision;
}

void TeamGate::Reset() {
    started.reset();
    holding_since.reset();
    asked = false;
}

} // namespace Core::Bloodborne
