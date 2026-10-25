// SPDX-License-Identifier: GPL-2.0-or-later
// IT_GET_LOD_STATS (shadPS4 #5493): the compute queue stopped the game with "Unknown PM4 type 3
// opcode" on it. Both queues now decode the packet; this checks its layout and fields.
#include <cassert>
#include <cstdint>
#include <span>
#include "video_core/amdgpu/pm4_cmds.h"

int main() {
    static_assert(sizeof(AmdGpu::PM4CmdGetLodStats) == 0x10);
    // Read in place from the command buffer, as Liverpool does.
    alignas(4) const std::uint32_t words[] = {0xc0028e00, 0x00030040, 0x89abcdef, 0x00000012};
    const auto* packet = reinterpret_cast<const AmdGpu::PM4CmdGetLodStats*>(words);
    assert(packet->DataOffset() == 0x40);
    assert(packet->control_flags.Value() == 3);
    assert(packet->Address() == 0x1289abcdefull);
    return 0;
}
