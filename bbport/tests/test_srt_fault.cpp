// A shader table walker that reads through a null or unmapped pointer (gpu/shim/bbport_srt_fault.h):
// that one walk reads zeros, the next walk with a good pointer reads the table. Patching the load
// to zero for good (what the walker's fault handler did before) left every later draw of the
// shader without its textures: Forbidden Woods lost all its textures for Kaniel and Lisara on
// v1.2.2 and stayed so until the game was restarted.
// No game, Vulkan device or window.
//   ninja -C out/gpu srt-fault-test && out/gpu/srt-fault-test.exe
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "bbport_srt_fault.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <csignal>
#include <sys/mman.h>
#include <ucontext.h>
#endif

using BbSrtFault::Decode;
using BbSrtFault::Reg;

namespace {

// The walker's guest loads, as xbyak encodes them (flatten_extended_userdata_pass.cpp).
struct Form {
    const char* name;
    uint8_t bytes[8];
    Reg reg;
    uint8_t length;
};
const Form kForms[] = {
    {"mov rdi, [rdi]", {0x48, 0x8b, 0x3f}, Reg::Rdi, 3},
    {"mov rdi, [rdi+8]", {0x48, 0x8b, 0x7f, 0x08}, Reg::Rdi, 4},
    {"mov rdi, [rdi+0x400]", {0x48, 0x8b, 0xbf, 0x00, 0x04, 0x00, 0x00}, Reg::Rdi, 7},
    {"mov rdi, [rdi+r10]", {0x4a, 0x8b, 0x3c, 0x17}, Reg::Rdi, 4},
    {"mov r10d, [rdi]", {0x44, 0x8b, 0x17}, Reg::R10, 3},
    {"mov r10d, [rdi+8]", {0x44, 0x8b, 0x57, 0x08}, Reg::R10, 4},
    {"mov r10d, [rdi+0x400]", {0x44, 0x8b, 0x97, 0x00, 0x04, 0x00, 0x00}, Reg::R10, 7},
    {"mov r10d, [rdi+r10]", {0x46, 0x8b, 0x14, 0x17}, Reg::R10, 4},
};

// push rdi; mov rdi, [rdi]; mov r10, 0xffffffffffff; and rdi, r10; mov r10d, [rdi+8];
// mov [rsi+0x40], r10d; pop rdi; ret: user data dword pair 0 points at a table, its dword 2 goes
// to flat dword 16, as a walker for one texture would.
const uint8_t kWalker[] = {
    0x57,                                                       // push rdi
    0x48, 0x8b, 0x3f,                                           // mov rdi, [rdi]
    0x49, 0xba, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00, 0x00, // mov r10, 0xffffffffffff
    0x4c, 0x21, 0xd7,                                           // and rdi, r10
    0x44, 0x8b, 0x57, 0x08,                                     // mov r10d, [rdi+8]
    0x44, 0x89, 0x56, 0x40,                                     // mov [rsi+0x40], r10d
    0x5f,                                                       // pop rdi
    0xc3,                                                       // ret
};

uint8_t* g_code = nullptr;
int g_faults = 0;

bool InWalker(uint64_t rip) {
    const auto start = reinterpret_cast<uint64_t>(g_code);
    return rip >= start && rip < start + sizeof(kWalker);
}

#ifdef _WIN32
LONG CALLBACK OnFault(EXCEPTION_POINTERS* info) {
    auto* c = info->ContextRecord;
    if (info->ExceptionRecord->ExceptionCode != EXCEPTION_ACCESS_VIOLATION || !InWalker(c->Rip)) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    uint64_t rip = c->Rip, rdi = c->Rdi, r10 = c->R10;
    if (!BbSrtFault::Resume(rip, rdi, r10)) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    c->Rip = rip, c->Rdi = rdi, c->R10 = r10;
    ++g_faults;
    return EXCEPTION_CONTINUE_EXECUTION;
}
#else
void OnFault(int, siginfo_t*, void* raw) {
    auto& g = static_cast<ucontext_t*>(raw)->uc_mcontext.gregs;
    uint64_t rip = g[REG_RIP], rdi = g[REG_RDI], r10 = g[REG_R10];
    if (!InWalker(rip) || !BbSrtFault::Resume(rip, rdi, r10)) {
        std::_Exit(3);
    }
    g[REG_RIP] = rip, g[REG_RDI] = rdi, g[REG_R10] = r10;
    ++g_faults;
}
#endif

// The walker's calling convention (PS4_SYSV_ABI): user data in rdi, the flat buffer in rsi.
typedef void(__attribute__((sysv_abi)) * Walker)(const uint32_t*, uint32_t*);

uint32_t Walk(Walker walker, const void* table) {
    uint32_t user_data[16] = {};
    std::memcpy(user_data, &table, sizeof(table));
    uint32_t flat[32];
    std::memset(flat, 0xcd, sizeof(flat));
    walker(user_data, flat);
    return flat[16];
}

} // namespace

int main() {
    for (const auto& f : kForms) {
        const auto load = Decode(f.bytes);
        if (load.reg != f.reg || load.length != f.length) {
            std::printf("srt-fault-test: %s decoded as reg %d, %d bytes\n", f.name,
                        int(load.reg), int(load.length));
            return 1;
        }
    }
    // Anything else is not a walker load: left to the next handler (the crash report).
    const uint8_t store[] = {0x44, 0x89, 0x56, 0x40}; // mov [rsi+0x40], r10d
    assert(Decode(store).reg == Reg::None);
    const uint8_t rcx_load[] = {0x48, 0x8b, 0x09}; // mov rcx, [rcx]
    assert(Decode(rcx_load).reg == Reg::None);

#ifdef _WIN32
    g_code = static_cast<uint8_t*>(
        VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    AddVectoredExceptionHandler(1, OnFault);
#else
    g_code = static_cast<uint8_t*>(mmap(nullptr, 4096, PROT_READ | PROT_WRITE | PROT_EXEC,
                                        MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
    struct sigaction sa {};
    sa.sa_sigaction = OnFault;
    sa.sa_flags = SA_SIGINFO;
    sigaction(SIGSEGV, &sa, nullptr);
#endif
    std::memcpy(g_code, kWalker, sizeof(kWalker));
    const auto walker = reinterpret_cast<Walker>(g_code);

    const uint32_t table[4] = {0, 0, 0x1234, 0};
    if (Walk(walker, table) != 0x1234) {
        std::printf("srt-fault-test: a good table is not read\n");
        return 1;
    }
    // A null table: that walk reads zeros (one fault, the texture's dword through the null
    // table)...
    if (Walk(walker, nullptr) != 0 || g_faults != 1) {
        std::printf("srt-fault-test: a null table read %d faults\n", g_faults);
        return 1;
    }
    // ...and the walker stays as it was: the next good table is read again.
    if (std::memcmp(g_code, kWalker, sizeof(kWalker)) != 0) {
        std::printf("srt-fault-test: the walker's code was changed by a fault\n");
        return 1;
    }
    if (Walk(walker, table) != 0x1234) {
        std::printf("srt-fault-test: after one null table, a good table reads zeros\n");
        return 1;
    }
    std::printf("srt-fault-test: ok\n");
    return 0;
}
