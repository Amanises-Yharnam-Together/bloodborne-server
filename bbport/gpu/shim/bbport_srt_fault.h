// bbport: a shader table walker (flatten_extended_userdata_pass.cpp) that reads through a null or
// unmapped guest pointer. Pure: no Vulkan, no game; tests/test_srt_fault.cpp.
#pragma once

#include <cstdint>

namespace BbSrtFault {

enum class Reg : uint8_t { None, Rdi, R10 };

/// A walker load: the register it loads and its length in bytes.
struct Load {
    Reg reg;
    uint8_t length;
};

/// The walker's guest loads, as xbyak encodes them: mov rdi, [rdi + disp] / [rdi + r10] and
/// mov r10d, [rdi + disp] / [rdi + r10], disp none, 8 or 32 bits. Anything else is Reg::None.
inline Load Decode(const uint8_t* code) {
    Reg reg = Reg::None;
    bool indexed = false;
    switch (code[0]) {
    case 0x48: // REX.W: rdi
        reg = Reg::Rdi;
        break;
    case 0x4a: // REX.WX: rdi, index r10
        reg = Reg::Rdi, indexed = true;
        break;
    case 0x44: // REX.R: r10d
        reg = Reg::R10;
        break;
    case 0x46: // REX.RX: r10d, index r10
        reg = Reg::R10, indexed = true;
        break;
    default:
        return {Reg::None, 0};
    }
    if (code[1] != 0x8b) {
        return {Reg::None, 0};
    }
    const uint8_t modrm = code[2];
    const uint8_t mod = modrm >> 6;
    // ModRM reg: 7 (rdi) for REX.W, 2 (r10 with REX.R) for REX.R.
    const uint8_t want_reg = reg == Reg::Rdi ? 7 : 2;
    if (((modrm >> 3) & 7) != want_reg || mod == 3) {
        return {Reg::None, 0};
    }
    if (indexed) {
        // SIB: scale 1, index r10, base rdi; no displacement.
        if (mod != 0 || (modrm & 7) != 4 || code[3] != 0x17) {
            return {Reg::None, 0};
        }
        return {reg, 4};
    }
    if ((modrm & 7) != 7) { // base rdi
        return {Reg::None, 0};
    }
    return {reg, uint8_t(3 + (mod == 1 ? 1 : mod == 2 ? 4 : 0))};
}

/// Steps over the walker load at rip as if it read zero: that walk reads zeros through the bad
/// pointer, as the game's GPU would for that one draw, and the code stays as it is for the next
/// one. False when rip is not at a walker load.
inline bool Resume(uint64_t& rip, uint64_t& rdi, uint64_t& r10) {
    const Load load = Decode(reinterpret_cast<const uint8_t*>(rip));
    switch (load.reg) {
    case Reg::Rdi:
        rdi = 0;
        break;
    case Reg::R10:
        r10 = 0; // a 32-bit load clears the upper half too
        break;
    case Reg::None:
        return false;
    }
    rip += load.length;
    return true;
}

} // namespace BbSrtFault
