// SPDX-License-Identifier: GPL-2.0-or-later
// bbport: the frame limiter's slots (VideoOutDriver::PresentThread). The flip times are the frame
// times the game sees (flip status), and its movement slows down above ~120 FPS. Slots used to
// stay on a fixed grid that "never lags behind by more than a period": the frame after a late one
// was flipped early, so single intervals of 6 ms (165 FPS) passed at a cap of 120. The next slot
// is a period after the slot, but no earlier than a period minus 1/16 after the flip made:
// counting only from the flip added the wake-up delay to every frame (116 FPS at a cap of 120).
// From Supermedo's bbport (dd1686d2, d646b1dc).
#pragma once

#include <algorithm>
#include <chrono>

namespace BbFrameSlots {

/// The next flip slot after a flip at `flipped` in the slot `slot`.
template <typename TimePoint, typename Duration>
constexpr TimePoint NextSlot(TimePoint slot, TimePoint flipped, Duration period) {
    return std::max(slot + period, flipped + period - period / 16);
}

} // namespace BbFrameSlots
