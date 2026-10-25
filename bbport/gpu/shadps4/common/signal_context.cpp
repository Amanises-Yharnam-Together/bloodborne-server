// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "common/arch.h"
#include "common/signal_context.h"

#ifdef _WIN32
#include <windows.h>
#elif defined(__FreeBSD__)
#include <machine/npx.h>
#include <sys/ucontext.h>
#else
#include <sys/ucontext.h>
#endif

namespace Common {

void* GetRip(void* ctx) {
#if defined(_WIN32)
    return (void*)((EXCEPTION_POINTERS*)ctx)->ContextRecord->Rip;
#elif defined(__APPLE__) && defined(ARCH_X86_64)
    return (void*)((ucontext_t*)ctx)->uc_mcontext->__ss.__rip;
#elif defined(__APPLE__) && defined(ARCH_ARM64)
    return (void*)((ucontext_t*)ctx)->uc_mcontext->__ss.__pc;
#elif defined(__FreeBSD__)
    return (void*)((ucontext_t*)ctx)->uc_mcontext.mc_rip;
#elif defined(ARCH_X86_64)
    return (void*)((ucontext_t*)ctx)->uc_mcontext.gregs[REG_RIP];
#else
#error "Unsupported architecture"
#endif
}

ResumeRegs GetResumeRegs(void* ctx) {
#if defined(_WIN32)
    auto* c = ((EXCEPTION_POINTERS*)ctx)->ContextRecord;
    return {(u64*)&c->Rip, (u64*)&c->Rdi, (u64*)&c->R10};
#elif defined(__APPLE__) && defined(ARCH_X86_64)
    auto& ss = ((ucontext_t*)ctx)->uc_mcontext->__ss;
    return {(u64*)&ss.__rip, (u64*)&ss.__rdi, (u64*)&ss.__r10};
#elif defined(__FreeBSD__) && defined(ARCH_X86_64)
    auto& mc = ((ucontext_t*)ctx)->uc_mcontext;
    return {(u64*)&mc.mc_rip, (u64*)&mc.mc_rdi, (u64*)&mc.mc_r10};
#elif defined(ARCH_X86_64)
    auto& g = ((ucontext_t*)ctx)->uc_mcontext.gregs;
    return {(u64*)&g[REG_RIP], (u64*)&g[REG_RDI], (u64*)&g[REG_R10]};
#else
    return {};
#endif
}

bool IsWriteError(void* ctx) {
#if defined(_WIN32)
    return ((EXCEPTION_POINTERS*)ctx)->ExceptionRecord->ExceptionInformation[0] == 1;
#elif defined(__APPLE__) && defined(ARCH_X86_64)
    return ((ucontext_t*)ctx)->uc_mcontext->__es.__err & 0x2;
#elif defined(__APPLE__) && defined(ARCH_ARM64)
    return ((ucontext_t*)ctx)->uc_mcontext->__es.__esr & 0x40;
#elif defined(__FreeBSD__) && defined(ARCH_X86_64)
    return ((ucontext_t*)ctx)->uc_mcontext.mc_err & 0x2;
#elif defined(ARCH_X86_64)
    return ((ucontext_t*)ctx)->uc_mcontext.gregs[REG_ERR] & 0x2;
#else
#error "Unsupported architecture"
#endif
}

bool IsExecuteError(void* ctx) {
#if defined(_WIN32)
    return ((EXCEPTION_POINTERS*)ctx)->ExceptionRecord->ExceptionInformation[0] == 0xf;
#elif defined(__APPLE__) && defined(ARCH_X86_64)
    return ((ucontext_t*)ctx)->uc_mcontext->__es.__err & 0x10;
#elif defined(__FreeBSD__) && defined(ARCH_X86_64)
    return ((ucontext_t*)ctx)->uc_mcontext.mc_err & 0x10;
#elif defined(ARCH_X86_64)
    return ((ucontext_t*)ctx)->uc_mcontext.gregs[REG_ERR] & 0x10;
#else
#error "Unsupported architecture"
#endif
}

} // namespace Common
