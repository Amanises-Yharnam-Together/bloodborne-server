// SPDX-License-Identifier: GPL-2.0-or-later
// bbport: button icons that follow the device in use (bbport_prompts.h).

#include "bbport_prompts.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <set>
#include <tuple>
#include <unordered_map>
#include <vector>

#include <xxhash.h>

extern "C" void runtime_memory_read_backing(uintptr_t address, void* data, uint64_t size);
extern "C" int runtime_memory_is_mapped(uintptr_t address, uint64_t size);
extern "C" void runtime_memory_note_write(uintptr_t address, uint64_t size);

namespace BbPrompts {
namespace {

using BbPromptSets::Set;
using BbPromptSets::SetCount;

/// The prompt sets file, read once.
struct Sets {
    std::vector<BbPromptSets::Entry> entries;
    /// The hash of an entry's pieces in each set.
    std::vector<std::array<std::uint64_t, SetCount>> hashes;
    /// Glyph entries by the hash of their bytes in any set (an icon found again after a write).
    std::unordered_map<std::uint64_t, std::pair<int, int>> glyph_by_hash; ///< entry, set
};

const Sets* Load() {
    static const Sets* const sets = []() -> const Sets* {
        const char* path = std::getenv("BB_PROMPT_SETS");
        if (!path || !*path) {
            return nullptr;
        }
        std::vector<std::uint8_t> bytes;
        if (FILE* f = std::fopen(path, "rb")) {
            std::uint8_t chunk[65536];
            for (std::size_t n; (n = std::fread(chunk, 1, sizeof(chunk), f)) > 0;) {
                bytes.insert(bytes.end(), chunk, chunk + n);
            }
            std::fclose(f);
        }
        auto entries = BbPromptSets::Parse(bytes);
        if (!entries) {
            std::printf("Button icons: %s is not a prompt sets file; the game's own icons stay\n", path);
            return nullptr;
        }
        auto* out = new Sets{std::move(*entries), {}, {}};
        for (std::size_t i = 0; i < out->entries.size(); ++i) {
            const auto& e = out->entries[i];
            auto& h = out->hashes.emplace_back();
            for (int s = 0; s < SetCount; ++s) {
                h[s] = XXH3_64bits(e.pieces[s].data(), e.pieces[s].size());
                if (e.IsGlyph()) {
                    out->glyph_by_hash.emplace(h[s], std::pair{int(i), s});
                }
            }
        }
        std::printf("Button icons: follow the device in use (%zu pictures in %d sets)\n",
                    out->entries.size(), int(SetCount));
        return out;
    }();
    return sets;
}

/// An entry found in guest memory at `base` (the texture's address): `written` is the hash of what
/// its pieces hold now (the game's or ours); anything else means the game reused the memory.
struct Found {
    std::uintptr_t base;
    int entry;
    std::uint64_t written;
};

std::mutex mutex; ///< found and searched; never held while guest memory is written
std::vector<Found> found;
std::set<std::pair<std::uintptr_t, int>> searched; ///< atlas textures (address, entry) looked at
std::atomic<int> device{-1};                       ///< -1 nothing seen yet, else a Set
std::atomic<bool> dirty{false};                    ///< something new to write
std::atomic<int> shown{BbPromptSets::PlayStation}; ///< what Apply wrote last

std::vector<std::uint8_t> Gather(std::uintptr_t base, const BbPromptSets::Entry& e) {
    std::vector<std::uint8_t> out(e.offsets.size() * 16);
    for (std::size_t i = 0; i < e.offsets.size(); ++i) {
        runtime_memory_read_backing(base + e.offsets[i], out.data() + i * 16, 16);
    }
    return out;
}

void Add(std::uintptr_t base, int entry, std::uint64_t hash) {
    std::scoped_lock lock{mutex};
    for (const Found& f : found) {
        if (f.base == base && f.entry == entry) {
            return;
        }
    }
    found.push_back({base, entry, hash});
    dirty = true;
    static int reported = 0;
    if (reported < 32) {
        ++reported;
        std::printf("Button icons: picture %d (%s) found at %#llx (%zu in memory)\n", entry,
                    Load()->entries[entry].name.c_str(), (unsigned long long)base, found.size());
    }
}

Set Want() {
    const int used = device.load(std::memory_order_relaxed);
    return used < 0 ? BbPromptSets::Keyboard : Set(used); // nothing used yet: keyboard and mouse
}

} // namespace

bool Active() {
    return Load() != nullptr;
}

void OnInput(Set set) {
    if (device.exchange(set, std::memory_order_relaxed) != set) {
        dirty = true;
    }
}

void OnControllerConnected(Set set) {
    int unknown = -1;
    if (device.compare_exchange_strong(unknown, set, std::memory_order_relaxed)) {
        dirty = true;
    }
}

Set Shown() {
    return Active() ? Set(shown.load(std::memory_order_relaxed)) : BbPromptSets::PlayStation;
}

std::span<const std::uint8_t> Glyph(std::string_view name, Set set) {
    if (const Sets* sets = Load()) {
        for (const auto& e : sets->entries) {
            if (e.name == name && e.IsGlyph()) {
                return e.pieces[set];
            }
        }
    }
    return {};
}

void OnUpload(std::uintptr_t address, std::size_t size, std::uint32_t width, std::uint32_t height,
              std::uint32_t levels, std::uint32_t bits) {
    const Sets* sets = Load();
    if (!sets || levels != 1 || bits != 128 || address == 0) {
        return;
    }
    if (width == 32 && height == 32 && size == 1024) {
        // A KG_* glyph: the menus' key guides and the buttons in their messages.
        std::uint8_t bytes[1024];
        runtime_memory_read_backing(address, bytes, sizeof(bytes));
        const std::uint64_t hash = XXH3_64bits(bytes, sizeof(bytes));
        const auto it = sets->glyph_by_hash.find(hash);
        if (it != sets->glyph_by_hash.end()) {
            Add(address, it->second.first, sets->hashes[it->second.first][it->second.second]);
        } else {
            // Other 32x32 pictures (or glyphs a mod drew): named once each, the first few.
            static std::mutex seen_mutex;
            static std::set<std::uint64_t> seen;
            std::scoped_lock lock{seen_mutex};
            if (seen.size() < 24 && seen.insert(hash).second) {
                std::printf("Button icons: a 32x32 picture at %#llx is none of the game's (hash %016llx)\n",
                            (unsigned long long)address, (unsigned long long)hash);
                // BB_PROMPT_TRACE=<folder>: its bytes saved there (to compare with the game's file).
                if (const char* folder = std::getenv("BB_PROMPT_TRACE"); folder && *folder) {
                    char name[512];
                    std::snprintf(name, sizeof(name), "%s/prompt-%016llx.bin", folder,
                                  (unsigned long long)hash);
                    if (FILE* f = std::fopen(name, "wb")) {
                        std::fwrite(bytes, 1, sizeof(bytes), f);
                        std::fclose(f);
                    }
                }
            }
        }
        return;
    }
    // A menu atlas with buttons baked into it (the prompts in the world: "Pick Up Item").
    for (std::size_t i = 0; i < sets->entries.size(); ++i) {
        const auto& e = sets->entries[i];
        if (e.IsGlyph() || e.width != width || e.height != height) {
            continue;
        }
        {
            std::scoped_lock lock{mutex};
            if (!searched.insert({address, int(i)}).second) {
                continue;
            }
        }
        const auto bytes = Gather(address, e);
        const std::uint64_t hash = XXH3_64bits(bytes.data(), bytes.size());
        for (int s = 0; s < SetCount; ++s) {
            if (sets->hashes[i][s] == hash) {
                Add(address, int(i), hash);
                break;
            }
        }
    }
}

void Apply() {
    const Sets* sets = Load();
    if (!sets) {
        return;
    }
    const Set want = Want();
    if (!dirty.exchange(false) && int(want) == shown.load(std::memory_order_relaxed)) {
        return;
    }
    shown = want;
    std::vector<Found> snapshot;
    {
        std::scoped_lock lock{mutex};
        snapshot = found;
    }
    std::vector<std::tuple<std::uintptr_t, int, std::uint64_t>> written; // hash 0: gone
    for (const Found& f : snapshot) {
        const auto& e = sets->entries[f.entry];
        const std::uintptr_t start = f.base + e.First();
        if (!runtime_memory_is_mapped(start, e.Span())) {
            written.emplace_back(f.base, f.entry, 0);
            continue;
        }
        const auto now = Gather(f.base, e);
        if (XXH3_64bits(now.data(), now.size()) != f.written) {
            written.emplace_back(f.base, f.entry, 0); // the game put something else there
            continue;
        }
        const std::uint64_t target = sets->hashes[f.entry][want];
        if (target == f.written) {
            continue;
        }
        // Written as the game writes its memory: a page the texture cache watches tells it.
        const auto& pieces = e.pieces[want];
        for (std::size_t i = 0; i < e.offsets.size(); ++i) {
            std::memcpy(reinterpret_cast<void*>(f.base + e.offsets[i]), pieces.data() + i * 16, 16);
        }
        runtime_memory_note_write(start, e.Span());
        written.emplace_back(f.base, f.entry, target);
    }
    std::scoped_lock lock{mutex};
    for (const auto& [base, entry, hash] : written) {
        for (std::size_t i = 0; i < found.size(); ++i) {
            if (found[i].base != base || found[i].entry != entry) {
                continue;
            }
            if (hash) {
                found[i].written = hash;
            } else {
                found.erase(found.begin() + i);
                searched.erase({base, entry});
            }
            break;
        }
    }
}

} // namespace BbPrompts
