// The push constant block (shader_recompiler/resource.h, PushData) as the SPIR-V backend declares
// it (spirv_emit_context.cpp, DefinePushDataBlock): every buffer binding's offset byte must lie in
// one of the buf_offsets members, never in the members after them (the object motion parameter,
// the scene size). With 56 bindings and members for only 40, bindings 48-55 read their offsets
// from the motion parameter: buffers read at a byte offset that changed every frame.
//   ninja -C out/gpu push-data-test && out/gpu/push-data-test.exe
#include <cassert>
#include <cstddef>
#include <cstdio>
#include "shader_recompiler/resource.h"

int main() {
    using Shader::PushData;
    constexpr u32 members = PushData::MotionParamIndex - PushData::BufOffsetIndex;
    // The SPIR-V members are uvec4s at 16-byte steps from the C++ array.
    constexpr u32 first = offsetof(PushData, buf_offsets);
    assert(first == 16);
    assert(first + 16 * members <= offsetof(PushData, motion_param));
    for (u32 binding = 0; binding < Shader::NUM_BUFFERS; ++binding) {
        // As DefineBufferProperties picks the member, component and byte.
        const u32 member = PushData::BufOffsetIndex + (binding >> 4);
        const u32 comp = (binding & 0xf) >> 2;
        const u32 byte = (binding & 0x3);
        if (member >= PushData::MotionParamIndex) {
            std::printf("binding %u: member %u is past the buf_offsets members (%u)\n", binding,
                        member, members);
            return 1;
        }
        const u32 at = first + 16 * (member - PushData::BufOffsetIndex) + 4 * comp + byte;
        assert(at == offsetof(PushData, buf_offsets) + binding);
        assert(binding < sizeof(PushData::buf_offsets));
    }
    // The last two members follow at their C++ offsets, inside the guaranteed 128 bytes.
    assert(PushData::SceneSizeIndex == PushData::MotionParamIndex + 1);
    assert(offsetof(PushData, scene_size) == offsetof(PushData, motion_param) + 4);
    static_assert(sizeof(PushData) <= 128);
    std::printf("push-data-test: ok (%u bindings in %u members)\n", Shader::NUM_BUFFERS, members);
    return 0;
}
