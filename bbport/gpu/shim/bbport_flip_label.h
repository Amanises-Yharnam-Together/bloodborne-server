// SPDX-License-Identifier: GPL-2.0-or-later
// bbport: the EOP flip's IRQ and its buffer label. The label's write may still be on its way from
// the draw recording thread when the IRQ comes ("Out of order flip IRQ" stopped the game). It is
// waited for briefly; then the flip goes on anyway (at worst a frame shows twice, and the game
// still gets its flip event). From Supermedo's bbport (76f5b5f, from Awaken's port).
#pragma once

#include <chrono>
#include <cstdint>
#include <thread>

namespace BbFlipLabel {

/// True once `label` reads 1, false when it still does not after `timeout`.
inline bool WaitForLabel(const std::uint64_t& label, std::chrono::microseconds timeout) {
    const auto& watched = reinterpret_cast<const volatile std::uint64_t&>(label);
    if (watched == 1) {
        return true;
    }
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        std::this_thread::yield();
        if (watched == 1) {
            return true;
        }
    }
    return watched == 1;
}

} // namespace BbFlipLabel
