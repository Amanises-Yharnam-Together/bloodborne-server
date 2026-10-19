// The PC memory model's hooks in the game's code (gpu/shim/bbport_guest_hooks.cpp), run against
// small functions this test writes at the hooked offsets of the 1.09 eboot (image 0x800000000):
// the GPU memory range allocator's return, a GPU heap call site and a resource loader's memcpy
// call. Native detours by default; "traps" runs them as int3 traps (BB_GUEST_HOOK_TRAPS=1:
// SIGTRAP on Linux, the vectored breakpoint handler on Windows). The game's code calls with the
// System V ABI, as the test's calls into these functions do.
//   ninja -C out/gpu guest-hooks-test && out/gpu/guest-hooks-test.exe && out/gpu/guest-hooks-test.exe traps
#include "bbport_guest_hooks.h"

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#endif

extern "C" std::uint64_t runtime_disabled_optimizations = 0;
extern "C" std::uint64_t runtime_experiment_bits = 0;

namespace {
using u8 = std::uint8_t;
using u64 = std::uint64_t;
#define GUEST_ABI __attribute__((sysv_abi))

constexpr u64 ImageBase = 0x800000000ull;
constexpr u64 ImageSize = 0x2700000ull;
constexpr u64 AllocReturn = 0x26aa255, HeapReturn = 0x11e6629, CopyCall = 0x216d5ea,
              Memcpy = 0x261a1b0, GxNodeSite = 0x2199310, GxNodeSkip = 0x219934e;

u64 noted_address = 0, noted_size = 0, range_start = 0, range_size = 0;
int ranges = 0;

u8* Put(u64 offset, std::initializer_list<u8> bytes) {
    u8* at = reinterpret_cast<u8*>(ImageBase + offset);
    for (const u8 b : bytes) {
        *at++ = b;
    }
    return at;
}
u8* Put64(u8* at, u64 v) {
    std::memcpy(at, &v, 8);
    return at + 8;
}
u8* Put32(u8* at, std::uint32_t v) {
    std::memcpy(at, &v, 4);
    return at + 4;
}
u8* More(u8* at, std::initializer_list<u8> bytes) {
    for (const u8 b : bytes) {
        *at++ = b;
    }
    return at;
}

/// A function ending in the allocator's epilogue: returns `node` through `add rsp, 0x18; pop rbx`
/// at AllocReturn.
u64 WriteAllocFunction(u64 node) {
    const u64 entry = AllocReturn - 15;
    u8* at = Put(entry, {0x53, 0x48, 0x83, 0xec, 0x18, 0x48, 0xb8}); // push rbx; sub rsp, 0x18; mov rax,
    at = Put64(at, node);
    More(at, {0x48, 0x83, 0xc4, 0x18, 0x5b, 0xc3}); // add rsp, 0x18; pop rbx; ret
    return ImageBase + entry;
}

/// A heap caller: [rbp - 0x50] the size, rax the address, then `mov rbx, rax; test rbx, rbx` at
/// HeapReturn; returns rbx.
u64 WriteHeapFunction(u64 address, std::uint32_t size) {
    const u64 entry = HeapReturn - 27;
    // push rbp; mov rbp, rsp; push rbx; sub rsp, 0x58; mov qword [rbp - 0x50], size
    u8* at = Put(entry, {0x55, 0x48, 0x89, 0xe5, 0x53, 0x48, 0x83, 0xec, 0x58, 0x48, 0xc7, 0x45, 0xb0});
    at = Put32(at, size);
    at = More(at, {0x48, 0xb8}); // mov rax, address
    at = Put64(at, address);
    // mov rbx, rax; test rbx, rbx; mov rax, rbx; add rsp, 0x58; pop rbx; pop rbp; ret
    More(at, {0x48, 0x89, 0xc3, 0x48, 0x85, 0xdb, 0x48, 0x89, 0xd8, 0x48, 0x83, 0xc4, 0x58, 0x5b, 0x5d,
              0xc3});
    return ImageBase + entry;
}

/// A resource loader's copy: `call memcpy` at CopyCall; the game's memcpy here copies nothing.
u64 WriteCopyFunction() {
    const u64 entry = CopyCall - 4;
    // sub rsp, 8; call memcpy; add rsp, 8; ret
    Put(entry, {0x48, 0x83, 0xec, 0x08, 0xe8, 0xc1, 0xcb, 0x4a, 0x00, 0x48, 0x83, 0xc4, 0x08, 0xc3});
    Put(Memcpy, {0x48, 0x89, 0xf8, 0xc3}); // mov rax, rdi; ret
    return ImageBase + entry;
}

/// The GX per-frame render command (rdi the node, rsi the value): `mov [r12 + 8], r13` at
/// GxNodeSite, then 1 when it went on, 0 from GxNodeSkip (where the game skips the post).
u64 WriteGxNodeFunction() {
    const u64 entry = GxNodeSite - 15;
    // push r12; push r13; push r15; mov r12, rdi; mov r13, rsi; mov r15, rsp
    Put(entry, {0x41, 0x54, 0x41, 0x55, 0x41, 0x57, 0x49, 0x89, 0xfc, 0x49, 0x89, 0xf5, 0x49, 0x89,
                0xe7});
    // mov [r12 + 8], r13; mov rax, [r15 + 0xb0]; mov eax, 1; jmp done
    Put(GxNodeSite, {0x4d, 0x89, 0x6c, 0x24, 0x08, 0x49, 0x8b, 0x87, 0xb0, 0x00, 0x00, 0x00, 0xb8,
                     0x01, 0x00, 0x00, 0x00, 0xe9, 0x2a, 0x00, 0x00, 0x00});
    // xor eax, eax; done: pop r15; pop r13; pop r12; ret
    Put(GxNodeSkip, {0x31, 0xc0, 0x41, 0x5f, 0x41, 0x5d, 0x41, 0x5c, 0xc3});
    return ImageBase + entry;
}
} // namespace

extern "C" void runtime_memory_note_write(uintptr_t address, uint64_t size) {
    noted_address = address;
    noted_size = size;
}

// The render job slot fix checks its runner is mapped (not exercised here).
extern "C" int runtime_memory_is_mapped(uintptr_t, uint64_t) {
    return 0;
}

int main(int argc, char** argv) {
    const bool traps = argc > 1 && std::strcmp(argv[1], "traps") == 0;
    if (traps) {
#ifdef _WIN32
        _putenv_s("BB_GUEST_HOOK_TRAPS", "1");
#else
        setenv("BB_GUEST_HOOK_TRAPS", "1", 1);
#endif
    }
#ifdef _WIN32
    void* image = VirtualAlloc(reinterpret_cast<void*>(ImageBase), ImageSize, MEM_RESERVE | MEM_COMMIT,
                               PAGE_EXECUTE_READWRITE);
#else
    void* image = mmap(reinterpret_cast<void*>(ImageBase), ImageSize, PROT_READ | PROT_WRITE | PROT_EXEC,
                       MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    if (image == MAP_FAILED) {
        image = nullptr;
    }
#endif
    assert(image == reinterpret_cast<void*>(ImageBase));

    static u64 next[2] = {0, 0x5000 + 0x3000};
    static u64 node[4] = {0, 0x5000, 0, reinterpret_cast<u64>(next)};
    using Call = GUEST_ABI u64 (*)();
    using Copy = GUEST_ABI void* (*)(void*, const void*, std::size_t);
    const auto alloc_fn = reinterpret_cast<Call>(WriteAllocFunction(reinterpret_cast<u64>(node)));
    const auto heap_fn = reinterpret_cast<Call>(WriteHeapFunction(0x7700000, 0x4000));
    const auto copy_fn = reinterpret_cast<Copy>(WriteCopyFunction());
    using GxNode = GUEST_ABI u64 (*)(u64*, u64);
    const auto gx_fn = reinterpret_cast<GxNode>(WriteGxNodeFunction());

    BbGuestHooks::Install([](u64 address, u64 size) {
        range_start = address;
        range_size = size;
        ++ranges;
    });
    const u8* code = reinterpret_cast<const u8*>(ImageBase);
    // Detours: jumps over the sites; traps: the int3s stay.
    const u8 expected_alloc = traps ? 0xcc : 0xe9, expected_call = traps ? 0xcc : 0xe8;
    assert(code[AllocReturn] == expected_alloc && code[HeapReturn] == expected_alloc &&
           code[CopyCall] == expected_call);

    for (int round = 0; round < 2; ++round) {
        ranges = 0;
        assert(alloc_fn() == reinterpret_cast<u64>(node));
        assert(ranges == 1 && range_start == 0x5000 && range_size == 0x3000);
        assert(heap_fn() == 0x7700000);
        assert(ranges == 2 && range_start == 0x7700000 && range_size == 0x4000);
        char from[64] = "resource data", to[64] = {};
        noted_address = noted_size = 0;
        assert(copy_fn(to, from, sizeof(from)) == to);
        assert(std::strcmp(to, "resource data") == 0);
        assert(noted_address == reinterpret_cast<u64>(to) && noted_size == sizeof(from));
    }
    // The GX command heap guard (a detour only): a node is written as before; without one the
    // game's write to address 8 is skipped with the post.
    if (!traps) {
        assert(code[GxNodeSite] == 0xe9);
        for (int round = 0; round < 2; ++round) {
            u64 gx_node[2] = {0, 0};
            assert(gx_fn(gx_node, 0x1234) == 1 && gx_node[1] == 0x1234);
            assert(gx_fn(nullptr, 0x1234) == 0);
        }
    }
    std::printf("guest hooks (%s): ok\n", traps ? "int3 traps" : "native detours");
    return 0;
}
