// SPDX-License-Identifier: GPL-2.0-or-later
// Co-op guest code hooks on the native port (gpu/bbcoop/guest_hooks.cpp with the emulator's shared
// trampoline generator): hooks are installed into small x86-64 functions written here, which
// are then called the way the game's code is.
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <xbyak/xbyak.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#endif
#include "core/cpu_patches.h"

using namespace Xbyak::util;

static int failures;
#define CHECK(condition)                                                                           \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #condition);     \
            ++failures;                                                                            \
        }                                                                                          \
    } while (false)

namespace {

constexpr size_t CodeBytes = 0x4000;
constexpr size_t TrampolineBytes = 0x10000;

u8* MapExecutable(size_t size) {
#ifdef _WIN32
    return static_cast<u8*>(
        VirtualAlloc(nullptr, size, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE));
#else
    void* p =
        mmap(nullptr, size, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    return p == MAP_FAILED ? nullptr : static_cast<u8*>(p);
#endif
}

// The game's code is mapped read-execute; the installer has to cope with that.
void MakeReadExecute(u8* p, size_t size) {
#ifdef _WIN32
    DWORD old{};
    VirtualProtect(p, size, PAGE_EXECUTE_READ, &old);
#else
    mprotect(p, size, PROT_READ | PROT_EXEC);
#endif
}

bool IsWritable(const void* p) {
#ifdef _WIN32
    MEMORY_BASIC_INFORMATION info{};
    VirtualQuery(p, &info, sizeof(info));
    return info.Protect == PAGE_READWRITE || info.Protect == PAGE_EXECUTE_READWRITE ||
           info.Protect == PAGE_WRITECOPY || info.Protect == PAGE_EXECUTE_WRITECOPY;
#else
    std::ifstream maps{"/proc/self/maps"};
    const auto address = reinterpret_cast<uintptr_t>(p);
    for (std::string line; std::getline(maps, line);) {
        uintptr_t start{}, end{};
        char dash{};
        std::string perms;
        std::istringstream fields{line};
        fields >> std::hex >> start >> dash >> end >> perms;
        if (address >= start && address < end) {
            return perms.size() > 1 && perms[1] == 'w';
        }
    }
    return false;
#endif
}

using Function2 = u64 PS4_SYSV_ABI (*)(u64, u64);

struct Functions {
    u8* add;      // mov rax, rdi; add rax, rsi; ret
    u8* answer;   // mov eax, 42; ret
    u8* relative; // lea rax, [rip]; ret
    u8* plain;    // mov rax, rdi; add rax, rsi; ret (never hooked)
    u8* chosen;   // mov eax, 42; ret (its result chosen by the hook)
};

Functions WriteFunctions(u8* code) {
    Xbyak::CodeGenerator gen(CodeBytes, code);
    Functions f{};
    f.add = const_cast<u8*>(gen.getCurr());
    gen.mov(rax, rdi);
    gen.add(rax, rsi);
    gen.ret();
    gen.align(16);
    f.answer = const_cast<u8*>(gen.getCurr());
    gen.mov(eax, 42);
    gen.ret();
    gen.align(16);
    f.relative = const_cast<u8*>(gen.getCurr());
    gen.lea(rax, ptr[rip]);
    gen.ret();
    gen.align(16);
    f.plain = const_cast<u8*>(gen.getCurr());
    gen.mov(rax, rdi);
    gen.add(rax, rsi);
    gen.ret();
    gen.align(16);
    f.chosen = const_cast<u8*>(gen.getCurr());
    gen.mov(eax, 42);
    gen.ret();
    return f;
}

u64 seen_tag;
u64 seen_rdi;
u64 seen_rsi;
int calls;

void PS4_SYSV_ABI Observe(u64 tag, const Core::GuestRegisterSnapshot* registers) {
    seen_tag = tag;
    seen_rdi = registers->rdi;
    seen_rsi = registers->rsi;
    ++calls;
}

void PS4_SYSV_ABI ReplaceFirst(u64, Core::GuestRegisterSnapshot* registers) {
    registers->rdi = 1000;
}

bool PS4_SYSV_ABI SkipOnSeven(u64, const Core::GuestRegisterSnapshot* registers) {
    return registers->rdi == 7;
}

// Skips the function on 7 with 9 as its result.
bool PS4_SYSV_ABI NineOnSeven(u64, Core::GuestRegisterSnapshot* registers) {
    if (registers->rdi != 7) {
        return false;
    }
    registers->rax = 9;
    return true;
}

} // namespace

int main() {
    u8* code = MapExecutable(CodeBytes + TrampolineBytes);
    CHECK(code != nullptr);
    if (code == nullptr) {
        return 1;
    }
    u8* trampolines = code + CodeBytes;
    const Functions f = WriteFunctions(code);
    MakeReadExecute(code, CodeBytes);
    Core::RegisterPatchModule(code, CodeBytes, trampolines, TrampolineBytes);

    const std::array<u8, 6> add_bytes{0x48, 0x89, 0xf8, 0x48, 0x01, 0xf0};
    const std::array<u8, 5> answer_bytes{0xb8, 0x2a, 0x00, 0x00, 0x00};
    const std::array<u8, 7> relative_bytes{0x48, 0x8d, 0x05, 0x00, 0x00, 0x00, 0x00};
    CHECK(std::memcmp(f.add, add_bytes.data(), add_bytes.size()) == 0);
    CHECK(std::memcmp(f.answer, answer_bytes.data(), answer_bytes.size()) == 0);
    CHECK(std::memcmp(f.relative, relative_bytes.data(), relative_bytes.size()) == 0);

    // An observer sees the arguments and leaves the result alone.
    CHECK(Core::InstallGuestCodeHook(f.add, add_bytes, 0x1234, Observe));
    CHECK(reinterpret_cast<Function2>(f.add)(3, 4) == 7);
    CHECK(calls == 1 && seen_tag == 0x1234 && seen_rdi == 3 && seen_rsi == 4);
    CHECK(f.add[0] == 0xe9); // the site jumps to the trampoline now
    CHECK(!IsWritable(f.add));

    // The same instructions cannot be hooked twice.
    CHECK(!Core::InstallGuestRegisterEditHook(f.add, add_bytes, 1, ReplaceFirst));
    CHECK(!Core::InstallGuestCodeHook(f.add + 3, std::span{add_bytes}.subspan(3), 1, Observe));

    // A register edit changes what the copied instructions compute.
    CHECK(Core::InstallGuestRegisterEditHook(f.plain, add_bytes, 2, ReplaceFirst));
    CHECK(reinterpret_cast<Function2>(f.plain)(3, 4) == 1004);

    // An override returns 0 to the caller when it skips the function, else the function runs.
    CHECK(Core::InstallGuestFunctionOverride(f.answer, answer_bytes, 3, SkipOnSeven));
    CHECK(reinterpret_cast<Function2>(f.answer)(7, 0) == 0);
    CHECK(reinterpret_cast<Function2>(f.answer)(8, 0) == 42);

    // A result override returns the value it chose instead (the game's "sent").
    CHECK(Core::InstallGuestFunctionResult(f.chosen, answer_bytes, 7, NineOnSeven));
    CHECK(reinterpret_cast<Function2>(f.chosen)(7, 0) == 9);
    CHECK(reinterpret_cast<Function2>(f.chosen)(8, 0) == 42);

    // A hook taken out again (an all-or-nothing install that failed half way) puts the game's
    // instructions back, read-execute as they were, and the site can be hooked again.
    CHECK(Core::RemoveGuestCodeHook(f.add));
    CHECK(std::memcmp(f.add, add_bytes.data(), add_bytes.size()) == 0);
    CHECK(!IsWritable(f.add));
    const int calls_before = calls;
    CHECK(reinterpret_cast<Function2>(f.add)(3, 4) == 7);
    CHECK(calls == calls_before);
    CHECK(!Core::RemoveGuestCodeHook(f.add)); // no hook there any more
    CHECK(!Core::RemoveGuestCodeHook(f.relative));
    CHECK(Core::InstallGuestCodeHook(f.add, add_bytes, 0x1234, Observe));
    CHECK(reinterpret_cast<Function2>(f.add)(3, 4) == 7);
    CHECK(calls == calls_before + 1);

    // Instructions that do not match, or that cannot run from a trampoline, are refused and left
    // as they were.
    std::array<u8, 7> wrong = relative_bytes;
    wrong[0] = 0x90;
    CHECK(!Core::InstallGuestCodeHook(f.relative, wrong, 4, Observe));
    CHECK(!Core::InstallGuestCodeHook(f.relative, relative_bytes, 4, Observe));
    CHECK(std::memcmp(f.relative, relative_bytes.data(), relative_bytes.size()) == 0);

    // Too short to hold the jump, or outside every registered module.
    CHECK(!Core::InstallGuestCodeHook(f.relative, std::span{relative_bytes}.first(4), 5, Observe));
    std::array<u8, 6> outside_code = add_bytes;
    CHECK(!Core::InstallGuestCodeHook(outside_code.data(), add_bytes, 6, Observe));

    if (failures != 0) {
        std::fprintf(stderr, "coop hooks: %d check(s) failed\n", failures);
        return 1;
    }
    std::puts("coop hooks: ok");
    return 0;
}
