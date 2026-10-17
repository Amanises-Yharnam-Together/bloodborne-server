// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <utility>
#include <boost/container/small_vector.hpp>
#include "bbport_toggles.h"
#include "bblayer_write_traps.h"
#include "common/adaptive_mutex.h"
#include "common/assert.h"
#include "common/debug.h"
#include "common/div_ceil.h"
#include "common/error.h"
#include "common/multi_level_page_table.h"
#include "common/signal_context.h"
#include "common/thread.h"
#include "core/emulator_settings.h"
#include "core/memory.h"
#include "video_core/buffer_cache/buffer.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdio>
#include <sys/uio.h>
#include <dlfcn.h>
#include <string>
#include <fmt/format.h>
#ifndef _WIN32
#include <ucontext.h>
#endif
#include <unistd.h>
#include "core/signals.h"
#include "video_core/page_manager.h"
#include "video_core/renderer_vulkan/vk_rasterizer.h"

#ifndef _WIN64
#include <sys/mman.h>
#include "common/adaptive_mutex.h"
#else
#include <windows.h>
#endif

#ifdef __linux__
#include <thread>
#include <fcntl.h>
#include <linux/userfaultfd.h>
#include <poll.h>
#include <unordered_set>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <mutex>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#else
#include "common/spin_lock.h"
#endif

extern "C" void runtime_memory_set_write_watch(uintptr_t address, uint64_t size, int watch);
extern "C" int runtime_memory_vma_info(uintptr_t address, int* prot, int* type, uintptr_t* end);

namespace VideoCore {

namespace {
/// bbport: write fault sites (guest code), an open-addressing table keyed by the instruction.
struct FaultSite {
    std::atomic<u64> rip{0};
    std::atomic<u64> caller{0};
    std::atomic<u64> count{0};
    std::atomic<u64> last_address{0};
};
std::array<FaultSite, 512> fault_sites;
std::atomic<u64> fault_sites_dropped{0};
constexpr u64 GuestImage = 0x800000000ull, GuestImageEnd = 0x810000000ull;
constexpr u64 HostSite = 1ull << 63;
constexpr u64 ReadSite = 1ull << 62; ///< a read fault (BB_READBACKS=2 protects GPU data from reads)

/// "+offset" for guest code, the symbol for host code.
std::string SiteName(u64 key) {
    const char* access = key & ReadSite ? "read:" : "";
    key &= ~ReadSite;
    if (!(key & HostSite)) {
        return fmt::format("{}+{:#x}", access, key);
    }
    const u64 address = key & ~HostSite;
    Dl_info info{};
    if (dladdr(reinterpret_cast<void*>(address), &info) && info.dli_sname) {
        return fmt::format("{}host:{}+{:#x}", access, info.dli_sname, address - u64(info.dli_saddr));
    }
    if (info.dli_fname) {
        return fmt::format("{}host:{}+{:#x}", access, info.dli_fname, address - u64(info.dli_fbase));
    }
    return fmt::format("{}host:{:#x}", access, address);
}

/// The write fault this thread is handling (guest offsets; 0: none).
thread_local u64 current_fault_rip = 0, current_fault_caller = 0;

/// Guest code that wrote image (texture) memory: keyed by instruction and image.
struct ImageFaultSite {
    std::atomic<u64> key{0}; // a mix of the site and the image address
    std::atomic<u64> rip{0}, caller{0}, count{0}, image{0}, size{0};
    std::atomic<u32> width{0}, height{0}, format{0}, tiling{0};
};
std::array<ImageFaultSite, 256> image_fault_sites;

void NoteFaultSite(void* context, VAddr address) {
#ifdef _WIN32
    // The context is an EXCEPTION_POINTERS here, not a ucontext: no site statistics.
    (void)context;
    (void)address;
    current_fault_rip = 0;
    return;
#else
    const auto* g = static_cast<const ucontext_t*>(context)->uc_mcontext.gregs;
    current_fault_rip = 0;
    const u64 rip = u64(g[REG_RIP]);
    const bool guest_code = rip >= GuestImage && rip < GuestImageEnd;
    u64 caller = 0;
    if (guest_code) {
        // The caller: [rbp + 8] when the guest code keeps frames (its memcpy-like leaves do not).
        u64 saved[2] = {};
        iovec local{saved, sizeof(saved)}, remote{reinterpret_cast<void*>(g[REG_RBP]), sizeof(saved)};
        if (process_vm_readv(getpid(), &local, 1, &remote, 1, 0) == ssize_t(sizeof(saved)) &&
            saved[1] >= GuestImage && saved[1] < GuestImageEnd) {
            caller = saved[1];
        }
    } else {
        // Host code (a libc import the port runs natively, the runtime): the first guest return
        // address on the stack is its guest caller.
        std::array<u64, 64> stack{};
        iovec local{stack.data(), sizeof(stack)},
            remote{reinterpret_cast<void*>(g[REG_RSP]), sizeof(stack)};
        const ssize_t got = process_vm_readv(getpid(), &local, 1, &remote, 1, 0);
        for (ssize_t i = 0; i < got / 8; ++i) {
            if (stack[i] >= GuestImage && stack[i] < GuestImageEnd) {
                caller = stack[i];
                break;
            }
        }
    }
    // Guest code: its offset in the image; host code: its address with HostSite set.
    const u64 key = (guest_code ? rip - GuestImage : rip | HostSite) |
                    (Common::IsWriteError(context) ? 0 : ReadSite);
    current_fault_rip = key;
    current_fault_caller = caller ? caller - GuestImage : 0;
    for (u64 i = 0, slot = (key * 0x9E3779B97F4A7C15ull) >> 55; i < fault_sites.size(); ++i) {
        auto& site = fault_sites[(slot + i) % fault_sites.size()];
        u64 expected = 0;
        if (site.rip.load(std::memory_order_relaxed) == key ||
            site.rip.compare_exchange_strong(expected, key)) {
            site.caller.store(caller ? caller - GuestImage : 0, std::memory_order_relaxed);
            site.last_address.store(address, std::memory_order_relaxed);
            site.count.fetch_add(1, std::memory_order_relaxed);
            return;
        }
    }
    fault_sites_dropped.fetch_add(1, std::memory_order_relaxed);
#endif
}
} // namespace

static void ReportImageFaultSites() {
    bool any = false;
    for (auto& site : image_fault_sites) {
        const u64 count = site.count.exchange(0, std::memory_order_relaxed);
        if (count == 0) {
            continue;
        }
        std::printf("%s %s (from +%#llx) %llu into image %#llx size %llu %ux%u fmt %u tile %u",
                    any ? ";" : "Image write sites (guest code -> texture, this window):",
                    SiteName(site.rip.load()).c_str(), (unsigned long long)site.caller.load(),
                    (unsigned long long)count, (unsigned long long)site.image.load(),
                    (unsigned long long)site.size.load(), site.width.load(), site.height.load(),
                    site.format.load(), site.tiling.load());
        any = true;
    }
    if (any) {
        std::printf("\n");
    }
}

void PageManager::NoteImageFault(VAddr image_address, u64 image_size, u32 width, u32 height,
                                 u32 format, u32 tiling) {
    if (current_fault_rip == 0) {
        return;
    }
    const u64 key = (current_fault_rip * 0x9E3779B97F4A7C15ull) ^ image_address;
    for (u64 i = 0, slot = (key * 0x9E3779B97F4A7C15ull) >> 56; i < image_fault_sites.size(); ++i) {
        auto& site = image_fault_sites[(slot + i) % image_fault_sites.size()];
        u64 expected = 0;
        if (site.key.load(std::memory_order_relaxed) == key ||
            site.key.compare_exchange_strong(expected, key)) {
            site.rip.store(current_fault_rip, std::memory_order_relaxed);
            site.caller.store(current_fault_caller, std::memory_order_relaxed);
            site.image.store(image_address, std::memory_order_relaxed);
            site.size.store(image_size, std::memory_order_relaxed);
            site.width.store(width, std::memory_order_relaxed);
            site.height.store(height, std::memory_order_relaxed);
            site.format.store(format, std::memory_order_relaxed);
            site.tiling.store(tiling, std::memory_order_relaxed);
            site.count.fetch_add(1, std::memory_order_relaxed);
            return;
        }
    }
}

void PageManager::ReportFaultSites() {
    struct Row {
        u64 rip, caller, count, address;
    };
    std::array<Row, 512> rows{};
    std::size_t n = 0;
    u64 total = 0;
    for (auto& site : fault_sites) {
        const u64 count = site.count.exchange(0, std::memory_order_relaxed);
        if (count != 0) {
            rows[n++] = {site.rip.load(), site.caller.load(), count, site.last_address.load()};
            total += count;
        }
    }
    if (total == 0) {
        return;
    }
    ReportImageFaultSites();
    std::sort(rows.begin(), rows.begin() + n, [](const Row& a, const Row& b) { return a.count > b.count; });
    std::printf("Write fault sites (guest code, this window): %llu faults", (unsigned long long)total);
    for (std::size_t i = 0; i < std::min<std::size_t>(n, 12); ++i) {
        std::printf("%s %s (from +%#llx) %llu at %#llx", i ? ";" : ":",
                    SiteName(rows[i].rip).c_str(), (unsigned long long)rows[i].caller,
                    (unsigned long long)rows[i].count, (unsigned long long)rows[i].address);
    }
    std::printf("\n");
}

// bbport: with the game's memory in place (no write tracking), the pages under images are
// trapped for writes (BbLayer::WriteTraps::Image): a write nobody announced (the game's own copy
// loops, its heap over an old texture) still reaches the texture cache, on any game and GPU,
// without hooks in the game's code; the hooks and the libc wrappers only make it sooner.
// BB_IMAGE_TRAPS=0: announced writes only, as before. Latched at the first use: the counts must
// match their traps for the whole run.
static bool ImageTraps() {
    static const bool on = [] {
        const char* env = std::getenv("BB_IMAGE_TRAPS");
        const bool enabled = !(env && env[0] == '0') && VideoCore::GuestInPlace() &&
                             !VideoCore::WriteTracking() && !VideoCore::WriteVerify();
        if (enabled) {
            std::printf("Guest memory: writes under images are trapped (BB_IMAGE_TRAPS=0: "
                        "announced writes only)\n");
        }
        return enabled;
    }();
    return on;
}
struct PageManager::Impl {
    struct PageState {
        u8 num_write_watchers;
        u8 num_read_watchers;

        Core::MemoryPermission WritePerm() const noexcept {
            return num_write_watchers == 0 ? Core::MemoryPermission::Write
                                           : Core::MemoryPermission::None;
        }

        Core::MemoryPermission ReadPerm() const noexcept {
            return num_read_watchers == 0 ? Core::MemoryPermission::Read
                                          : Core::MemoryPermission::None;
        }

        Core::MemoryPermission Perms() const noexcept {
            return ReadPerm() | WritePerm();
        }

        template <bool is_read>
        u8 GetPage() const {
            if constexpr (is_read) {
                return num_read_watchers;
            } else {
                return num_write_watchers;
            }
        }

        constexpr Core::MemoryPermission Update(PageOp write_op, bool update_write = true,
                                                PageOp read_op = PageOp::None,
                                                bool update_read = false) {
            if (update_read) {
                if (read_op == PageOp::Track) {
                    ASSERT_MSG(num_read_watchers == 0, "Too many watchers");
                } else if (read_op == PageOp::Untrack) {
                    ASSERT_MSG(num_read_watchers > 0, "Not enough watchers");
                }
                num_read_watchers += std::to_underlying(read_op);
            }
            if (update_write) {
                if (write_op == PageOp::Track) {
                    ASSERT_MSG(num_write_watchers < 255, "Too many watchers");
                } else if (write_op == PageOp::Untrack) {
                    ASSERT_MSG(num_write_watchers > 0, "Not enough watchers");
                }
                num_write_watchers += std::to_underlying(write_op);
            }
            return Perms();
        }
    };

    static constexpr size_t ADDRESS_BITS = 40;
    static constexpr size_t NUM_ADDRESS_PAGES = 1ULL << (40 - PM_PAGE_BITS);
    static constexpr size_t NUM_ADDRESS_LOCKS = NUM_ADDRESS_PAGES / NUM_REGION_PAGES;
    inline static Vulkan::Rasterizer* rasterizer;

    Impl() = default;
    virtual ~Impl() = default;

    virtual void OnMap(VAddr address, size_t size) {
        // No-op
        EnsurePages(address, address + size);
    }

    virtual void OnUnmap(VAddr address, size_t size) {
        // No-op
    }

    virtual void Protect(VAddr address, size_t size, Core::MemoryPermission perms) = 0;

    static bool GuestFaultSignalHandler(void* context, void* fault_address) {
        const auto addr = reinterpret_cast<VAddr>(fault_address);
        NoteFaultSite(context, addr);
        // As upstream: an access near the end of a page stays within that page.
        const auto size = std::min<u64>(8, PageManager::GetNextPageAddr(addr) - addr);
        // bbport: the draw recording thread handles its faults inline too (vk_draw_pipe.h).
        const auto is_gpu_thread = rasterizer->IsGpuSideThread();
        if (is_gpu_thread) {
            BbStats::gpu_signal_faults.fetch_add(1, std::memory_order_relaxed);
        }
        // Traps on reads too (no access, BbLayer::WriteTraps::NoAccess): their owner first.
        if (BbLayer::WriteTraps::Reasons(addr) & BbLayer::WriteTraps::VramData) {
            return rasterizer->OnVramDataAccess(addr, is_gpu_thread);
        }
        if (BbLayer::WriteTraps::Reasons(addr) & BbLayer::WriteTraps::QueryReads) {
            return rasterizer->OnOcclusionPageAccess(
                addr, reinterpret_cast<u64>(Common::GetRip(context)),
                Common::IsWriteError(context), is_gpu_thread);
        }
        if (Common::IsWriteError(context)) {
            if (ImageTraps() &&
                !(BbLayer::WriteTraps::Reasons(addr) & BbLayer::WriteTraps::Image)) {
                // Not an image's trap: another owner's being lifted by another thread, or one
                // lifted meanwhile (the write runs again), or a write the game may not do.
                int prot = 0, type = -1;
                uintptr_t end = 0;
                return runtime_memory_vma_info(addr, &prot, &type, &end) && (prot & 0x2);
            }
            BbStats::Timer timer{BbStats::t_write_faults};
            const bool handled = rasterizer->OnWriteFault(
                addr, is_gpu_thread, u64(reinterpret_cast<uintptr_t>(Common::GetRip(context))));
            current_fault_rip = 0;
            return handled;
        } else {
            BbStats::read_faults.fetch_add(1, std::memory_order_relaxed);
            BbStats::Timer timer{BbStats::t_read_faults};
            return rasterizer->ReadMemory(addr, size, is_gpu_thread);
        }
        return false;
    }

    void EnsurePages(VAddr begin, VAddr end) {
        end = std::min(end, VAddr{1} << ADDRESS_BITS) - 1;
        const size_t start_page = begin >> PM_PAGE_BITS;
        const size_t end_page = end >> PM_PAGE_BITS;
        cached_pages.reserve(start_page, end_page);
        locks.reserve(start_page, end_page);
    }

    void UpdatePageWatchers(VAddr addr, u64 size, PageOp write_op) {
        const u64 page_start = addr >> PM_PAGE_BITS;
        const u64 page_end = Common::DivCeil(addr + size, PM_PAGE_SIZE);

        Core::MemoryPermission perms{};
        u64 range_begin = page_start;
        u64 range_pages = 0;
        u64 potential_pages = 0;

        const auto release_pending = [&] {
            if (range_pages > 0) {
                Protect(range_begin << PM_PAGE_BITS, range_pages << PM_PAGE_BITS, perms);
                range_pages = 0;
                potential_pages = 0;
            }
        };

        // Iterate requested pages
        const u64 aligned_addr = page_start << PM_PAGE_BITS;
        const u64 aligned_end = page_end << PM_PAGE_BITS;
        if (!rasterizer->IsMapped(aligned_addr, aligned_end - aligned_addr)) {
            LOG_WARNING(Render,
                        "Tracking memory region {:#x} - {:#x} which is not fully GPU mapped.",
                        aligned_addr, aligned_end);
            EnsurePages(aligned_addr, aligned_end);
        }

        for (u64 page = page_start; page != page_end; ++page) {
            PageState* state = cached_pages.find(page);
            if (!state) {
                continue;
            }

            locks[page].lock();

            const auto old_perms = state->Perms();
            if (page == page_start) {
                perms = old_perms;
            }

            // Apply the change to the page state
            const auto new_perms = state->Update(write_op);
            if (new_perms != perms) [[unlikely]] {
                // If the protection changed add pending (un)protect action
                release_pending();
                perms = new_perms;
            } else if (range_pages != 0) {
                ++potential_pages;
            }

            // If the page must be (un)protected
            if (new_perms != old_perms) {
                if (range_pages == 0) {
                    // Start a new potential range
                    range_begin = page;
                    potential_pages = 1;
                }
                // Extend current range up to potential range
                range_pages = potential_pages;
            }
        }

        // Add pending (un)protect action
        release_pending();

        for (u64 page = page_start; page != page_end; ++page) {
            if (auto* lock = locks.find(page)) {
                lock->unlock();
            }
        }
    }

    void UpdatePageWatchersForRegion(VAddr base_addr, const Bounds& bounds,
                                     const RegionBits& write_mask, const RegionBits& read_mask,
                                     PageOp write_op, PageOp read_op) {
        const u64 base_page = base_addr >> PM_PAGE_BITS;
        const u64 page_start = bounds.start_word * PAGES_PER_WORD + bounds.start_page;
        const u64 page_end = bounds.end_word * PAGES_PER_WORD + bounds.end_page + 1;

        Core::MemoryPermission perms{};
        u64 range_begin = base_page + page_start;
        u64 range_pages = 0;
        u64 potential_pages = 0;

        const auto release_pending = [&] {
            if (range_pages > 0) {
                Protect(range_begin << PM_PAGE_BITS, range_pages << PM_PAGE_BITS, perms);
                range_pages = 0;
                potential_pages = 0;
            }
        };

        for (u64 page = page_start; page != page_end; ++page) {
            PageState* state = cached_pages.find(base_page + page);
            if (!state) {
                continue;
            }

            locks[base_page + page].lock();

            const auto old_perms = state->Perms();
            if (page == page_start) {
                perms = old_perms;
            }

            // Apply the change to the page state
            const bool update_write = write_op != PageOp::None && write_mask.GetPage(page);
            const bool update_read = read_op != PageOp::None && read_mask.GetPage(page);
            const auto new_perms = state->Update(write_op, update_write, read_op, update_read);

            if (new_perms != perms) [[unlikely]] {
                // If the protection changed add pending (un)protect action
                release_pending();
                perms = new_perms;
            } else if (range_pages != 0) {
                // If the protection did not change, extend the potential range
                ++potential_pages;
            }

            // If the page must be (un)protected
            if (new_perms != old_perms) {
                if (range_pages == 0) {
                    // Start a new potential range
                    range_begin = base_page + page;
                    potential_pages = 1;
                }
                // Extend current rango up to potential range
                range_pages = potential_pages;
            }
        }

        // Add pending (un)protect action
        release_pending();

        for (u64 page = page_start; page != page_end; ++page) {
            if (auto* lock = locks.find(base_page + page)) {
                lock->unlock();
            }
        }
    }

    /// bbport ImageTraps: the images over each page; a page is trapped while one is there.
    template <bool track>
    void UpdateImageTraps(VAddr addr, u64 size) {
        std::call_once(image_watchers_once, [this] {
#ifdef _WIN32
            // Committed as touched: Windows has no MAP_NORESERVE, and the table is 512 MiB.
            void* mem = VirtualAlloc(nullptr, NUM_ADDRESS_PAGES * sizeof(u16), MEM_RESERVE,
                                     PAGE_READWRITE);
            ASSERT_MSG(mem != nullptr, "image watcher table");
#else
            void* mem = mmap(nullptr, NUM_ADDRESS_PAGES * sizeof(u16), PROT_READ | PROT_WRITE,
                             MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
            ASSERT_MSG(mem != MAP_FAILED, "image watcher table");
#endif
            image_watchers = static_cast<u16*>(mem);
        });
        size_t page = addr >> PM_PAGE_BITS;
        const u64 page_end = Common::DivCeil(addr + size, PM_PAGE_SIZE);
        // One lock for the whole table: image (un)registrations are rare next to the page watchers.
        std::scoped_lock lk{image_watchers_mutex};
#ifdef _WIN32
        // The table is only reserved: commit the counts of these pages (a no-op where committed).
        VirtualAlloc(&image_watchers[page], (page_end - page) * sizeof(u16), MEM_COMMIT,
                     PAGE_READWRITE);
#endif
        // Runs of pages whose first image came or last one went: one trap change each.
        u64 run_begin = 0, run_pages = 0;
        const auto flush = [&] {
            if (run_pages != 0) {
                BbLayer::WriteTraps::Set(run_begin << PM_PAGE_BITS, run_pages << PM_PAGE_BITS,
                                         BbLayer::WriteTraps::Image, track);
                run_pages = 0;
            }
        };
        for (; page != page_end; ++page) {
            u16& count = image_watchers[page];
            bool change;
            if constexpr (track) {
                change = count++ == 0;
            } else {
                ASSERT_MSG(count > 0, "image watchers below zero");
                change = --count == 0;
            }
            if (!change) {
                continue;
            }
            if (run_pages != 0 && run_begin + run_pages == page) {
                ++run_pages;
            } else {
                flush();
                run_begin = page;
                run_pages = 1;
            }
        }
        flush();
    }
    u16* image_watchers = nullptr;
    std::once_flag image_watchers_once;
    std::mutex image_watchers_mutex;

    struct PageTraits {
        using Entry = PageState;
        static constexpr size_t ADDRESS_SPACE_BITS = ADDRESS_BITS;
        static constexpr size_t L1_BITS = 16;
        static constexpr size_t PAGE_BITS = PM_PAGE_BITS;
        static constexpr bool NULL_CHECK = false;
    };
    Common::MultiLevelPageTable<PageTraits> cached_pages;
    struct MutexTraits {
#ifdef PTHREAD_ADAPTIVE_MUTEX_INITIALIZER_NP
        using Entry = Common::AdaptiveMutex;
#else
        using Entry = std::mutex;
#endif
        static constexpr size_t ADDRESS_SPACE_BITS = ADDRESS_BITS;
        static constexpr size_t L1_BITS = 16;
        static constexpr size_t PAGE_BITS = PM_PAGE_BITS;
        static constexpr bool NULL_CHECK = false;
    };
    Common::MultiLevelPageTable<MutexTraits> locks;
};

#ifdef __linux__
// bbport: write tracking with userfaultfd write-protection instead of mprotect. mprotect takes
// the address space lock for writing and splits mappings; while guest threads fault on
// per-frame buffers and the GPU thread re-protects uploaded pages, every page fault in the
// process (copy threads included) waits for it. UFFDIO_WRITEPROTECT changes page table bits
// under the lock for reading. Read protection (readbacks) still uses mprotect: userfaultfd
// write-protection cannot deny reads. BB_UFFD=1.
struct UffdImpl : public PageManager::Impl {
private:
    std::jthread ufd_thread;
    int uffd;
    std::mutex read_revoked_mutex;
    std::unordered_set<u64> read_revoked_pages; ///< 4 KiB pages denied reads with mprotect
    std::atomic<size_t> num_read_revoked{0};

public:
    UffdImpl(Vulkan::Rasterizer* rasterizer_) : Impl() {
        rasterizer = rasterizer_;
        uffd = syscall(SYS_userfaultfd, O_CLOEXEC | O_NONBLOCK | UFFD_USER_MODE_ONLY);
        if (uffd == -1) {
            LOG_ERROR(Common_Memory,
                      "userfaultfd syscall failed: {}, falling back to signal implementation",
                      Common::GetLastErrorMsg());
            throw std::runtime_error("userfaultfd");
        }

        // Guest memory is a shared memfd mapping: write-protection there needs the shmem
        // feature, and unpopulated pages must be protectable too.
        uffdio_api api{};
        api.api = UFFD_API;
        api.features =
            UFFD_FEATURE_THREAD_ID | UFFD_FEATURE_WP_HUGETLBFS_SHMEM | UFFD_FEATURE_WP_UNPOPULATED;
        if (ioctl(uffd, UFFDIO_API, &api) != 0) {
            LOG_ERROR(Common_Memory,
                      "uffdio_api call failed: {}, falling back to signal implementation",
                      Common::GetLastErrorMsg());
            close(uffd);
            throw std::runtime_error("uffdio_api");
        }

        // Read faults (readbacks) still arrive as signals.
        Core::Signals::Instance()->RegisterAccessViolationHandler(
            GuestFaultSignalHandler, 1u);

        ufd_thread = std::jthread([this](std::stop_token token) { UffdHandler(token); });
        std::printf("GPU: memory tracking with userfaultfd write-protection\n");
    }

    ~UffdImpl() = default;

    void OnMap(VAddr address, size_t size) override {
        PageManager::Impl::OnMap(address, size);
        uffdio_register reg{};
        reg.range.start = address;
        reg.range.len = size;
        reg.mode = UFFDIO_REGISTER_MODE_WP;
        if (ioctl(uffd, UFFDIO_REGISTER, &reg) == -1) {
            LOG_ERROR(Common_Memory, "Uffdio register {:#x}+{:#x} failed: {}", address, size,
                      Common::GetLastErrorMsg());
        }
    }

    void OnUnmap(VAddr address, size_t size) override {
        uffdio_range range{};
        range.start = address;
        range.len = size;
        ioctl(uffd, UFFDIO_UNREGISTER, &range);
    }

    void Protect(VAddr address, size_t size, Core::MemoryPermission perms) override {
        auto& address_space = Core::Memory::Instance()->GetAddressSpace();
        const bool allow_read = True(perms & Core::MemoryPermission::Read);
        const bool allow_write = True(perms & Core::MemoryPermission::Write);
        const u64 first = address >> 12, last = (address + size + 4095) >> 12;
        if (!allow_read) {
            // Readbacks: deny all access with mprotect.
            address_space.Protect(address, size, Core::MemoryPermission::None);
            std::scoped_lock lk{read_revoked_mutex};
            for (u64 page = first; page < last; ++page) {
                read_revoked_pages.insert(page);
            }
            num_read_revoked.store(read_revoked_pages.size(), std::memory_order_release);
            return;
        }
        if (num_read_revoked.load(std::memory_order_acquire) != 0) {
            std::scoped_lock lk{read_revoked_mutex};
            bool restore = false;
            for (u64 page = first; page < last; ++page) {
                restore |= read_revoked_pages.erase(page) != 0;
            }
            num_read_revoked.store(read_revoked_pages.size(), std::memory_order_release);
            if (restore) {
                address_space.Protect(address, size, Core::MemoryPermission::ReadWrite);
            }
        }
        // Counted like AddressSpace::Protect (BB_FRAME_LOG protect_ms).
        BbStats::Timer timer{BbStats::t_protect};
        BbStats::protect_calls.fetch_add(1, std::memory_order_relaxed);
        BbStats::protect_pages.fetch_add(last - first, std::memory_order_relaxed);
        if (!allow_write) {
            BbStats::protect_revoke_calls.fetch_add(1, std::memory_order_relaxed);
            BbStats::protect_revoke_pages.fetch_add(last - first, std::memory_order_relaxed);
        }
        uffdio_writeprotect wp{};
        wp.range.start = address;
        wp.range.len = size;
        wp.mode = allow_write ? UFFDIO_WRITEPROTECT_MODE_DONTWAKE : UFFDIO_WRITEPROTECT_MODE_WP;
        if (ioctl(uffd, UFFDIO_WRITEPROTECT, &wp) == -1) {
            LOG_ERROR(Common_Memory, "Uffdio writeprotect {:#x}+{:#x} failed: {}", address, size,
                      Common::GetLastErrorMsg());
        }
    }

    void UffdHandler(std::stop_token token) {
        Common::SetCurrentThreadName("bb:Uffd");
        while (!token.stop_requested()) {
            pollfd pollfd{};
            pollfd.fd = uffd;
            pollfd.events = POLLIN;
            // Short timeout so the stop request is seen.
            const int pollres = poll(&pollfd, 1, 100);
            if (pollres <= 0 || !(pollfd.revents & POLLIN)) {
                continue;
            }
            uffd_msg msg;
            const int readret = read(uffd, &msg, sizeof(msg));
            if (readret != sizeof(msg)) {
                continue;
            }
            if (msg.event != UFFD_EVENT_PAGEFAULT) {
                continue;
            }
            const VAddr addr = msg.arg.pagefault.address;
            const auto ptid = msg.arg.pagefault.feat.ptid;
            {
                BbStats::Timer timer{BbStats::t_write_faults};
                rasterizer->OnWriteFault(addr, rasterizer->IsGpuSideThreadId(ptid));
            }
            // Protect() clears with DONTWAKE (it may run for pages nobody waits on).
            uffdio_range wake{};
            wake.start = addr & ~u64(PageManager::PM_PAGE_SIZE - 1);
            wake.len = PageManager::PM_PAGE_SIZE;
            ioctl(uffd, UFFDIO_WAKE, &wake);
        }
    }
};
#endif // __linux__

struct SignalImpl : public PageManager::Impl {
    SignalImpl(Vulkan::Rasterizer* rasterizer_) : Impl() {
        rasterizer = rasterizer_;

        // Should be called first (bbport: after BB_VRAM_ACCESS_TRAP, priority 0, diagnostics only).
        constexpr u32 priority = 1;
        Core::Signals::Instance()->RegisterAccessViolationHandler(GuestFaultSignalHandler,
                                                                  priority);
    }

    void Protect(VAddr address, size_t size, Core::MemoryPermission perms) override {
        RENDERER_TRACE;
        // bbport BB_GUEST_IN_PLACE: no page protection; the pages are marked in the runtime's write
        // watch instead, and the writers that check it tell the GPU side (WriteTracking).
        if (!VideoCore::WriteTracking()) {
            runtime_memory_set_write_watch(address, size, !True(perms & Core::MemoryPermission::Write));
            if (!VideoCore::WriteVerify()) {
                return;
            }
        }
        auto* memory = Core::Memory::Instance();
        auto& impl = memory->GetAddressSpace();
        ASSERT_MSG(perms != Core::MemoryPermission::Write,
                   "Attempted to protect region as write-only which is not a valid permission");
        impl.Protect(address, size, perms);
    }

};

PageManager::PageManager(Vulkan::Rasterizer* rasterizer_) {
#ifdef __linux__
    // bbport: dma-buf guest memory (BB_GUEST_GPU_MEMORY=1) cannot be write-protected by userfaultfd
    // (anonymous and shmem only): its registration would fail and writes go unseen.
    const char* guest_gpu_memory = std::getenv("BB_GUEST_GPU_MEMORY");
    const bool dma_buf_guest = guest_gpu_memory && guest_gpu_memory[0] == '1';
    if (std::getenv("BB_UFFD") && std::getenv("BB_UFFD")[0] == '1' && dma_buf_guest) {
        std::printf("GPU: userfaultfd off with BB_GUEST_GPU_MEMORY=1 (dma-buf memory)\n");
    } else if (std::getenv("BB_UFFD") && std::getenv("BB_UFFD")[0] == '1') {
        try {
            impl = std::make_unique<UffdImpl>(rasterizer_);
            LOG_INFO(Config, "Memory tracking method: userfaultfd");
            return;
        } catch (const std::runtime_error& e) {
            // if uffd is unsupported, falls back to SignalImpl
        }
    }
    LOG_INFO(Config, "Memory tracking method: signals");
#endif
    impl = std::make_unique<SignalImpl>(rasterizer_);
}

PageManager::~PageManager() = default;

void PageManager::OnGpuMap(VAddr address, size_t size) {
    impl->OnMap(address, size);
}

void PageManager::OnGpuUnmap(VAddr address, size_t size) {
    impl->OnUnmap(address, size);
}

void PageManager::UpdatePageWatchers(VAddr addr, u64 size, PageOp write_op) const {
    impl->UpdatePageWatchers(addr, size, write_op);
    // bbport: only the texture cache comes here (the buffer cache: UpdatePageWatchersForRegion).
    if (ImageTraps()) {
        if (write_op == PageOp::Track) {
            impl->UpdateImageTraps<true>(addr, size);
        } else if (write_op == PageOp::Untrack) {
            impl->UpdateImageTraps<false>(addr, size);
        }
    }
}

void PageManager::UpdatePageWatchersForRegion(VAddr base_addr, const Bounds& bounds,
                                              const RegionBits& write_mask,
                                              const RegionBits& read_mask, PageOp write_op,
                                              PageOp read_op) const {
    impl->UpdatePageWatchersForRegion(base_addr, bounds, write_mask, read_mask, write_op, read_op);
}

} // namespace VideoCore
