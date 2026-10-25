// SPDX-License-Identifier: GPL-2.0-or-later
// S_SENDMSG's immediate (shader_recompiler/frontend/opcodes.h, SendMsgSimm) as the translator
// reads it (scalar_flow.cpp): message in bits 0-3, GS operation in bits 4-5, the GS stream in
// bits 8-9 (shadPS4 #5487, which also emits a vertex and ends the primitive for EmitCut).
//   ninja -C out/gpu sendmsg-simm-test && out/gpu/sendmsg-simm-test.exe
#include <cassert>
#include <cstdint>
#include "shader_recompiler/frontend/opcodes.h"

int main() {
    using Shader::Gcn::SendMsgSimm;
    // s_sendmsg sendmsg(MSG_GS, GS_OP_EMIT_CUT, 2)
    const std::uint16_t simm = 2 | (3 << 4) | (2 << 8);
    const auto& msg = reinterpret_cast<const SendMsgSimm&>(simm);
    assert(msg.msg == SendMsgSimm::Message::Gs);
    assert(msg.op == SendMsgSimm::GsOp::EmitCut);
    assert(msg.stream == 2);
    return 0;
}
