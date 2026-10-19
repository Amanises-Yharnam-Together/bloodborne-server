// SPDX-License-Identifier: GPL-2.0-or-later
// bbport co-op: the map screen's area pictures and names (bbport_map_charts.h).
#include "bbport_map_charts.h"

#include <algorithm>
#include <condition_variable>
#include <cstdio>
#include <deque>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <set>
#include <thread>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include "../bbcoop.h"
#include "bbport_game_art.h"
#include "bbport_save_editor.h"
#include "bbport_settings.h"

namespace BbMapCharts {

namespace {

std::vector<std::uint8_t> ReadFile(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), {}};
}

std::vector<std::uint8_t> ReadGameFile(const std::string& relative) {
#ifdef _WIN32
    // The drawing threads (the worker's and ChartArea's) all read through here: each yields to
    // the game's own threads.
    thread_local bool lowered = false;
    if (!lowered) {
        lowered = true;
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
    }
#endif
    char path[1024];
    if (!bbcoop_game_file(("dvdroot_ps4/" + relative).c_str(), path, sizeof(path))) {
        return {};
    }
    return ReadFile(std::filesystem::u8path(path));
}

/// Bytes [offset, offset + size) of a file of the game's (a texture archive's piece).
std::vector<std::uint8_t> ReadGameRange(const std::string& relative, std::uint64_t offset, std::uint64_t size) {
    char path[1024];
    if (!bbcoop_game_file(("dvdroot_ps4/" + relative).c_str(), path, sizeof(path))) {
        return {};
    }
    std::ifstream file(std::filesystem::u8path(path), std::ios::binary);
    std::vector<std::uint8_t> bytes(size);
    file.seekg(std::streamoff(offset));
    file.read(reinterpret_cast<char*>(bytes.data()), std::streamsize(size));
    bytes.resize(std::size_t(std::max<std::streamsize>(0, file.gcount())));
    return bytes;
}

std::filesystem::path CachePath(std::uint32_t key) {
    const int view = int(key & 7u);
    const std::string suffix = view > 0 ? "_v" + std::to_string(view) : "";
    return std::filesystem::path("user") / "map" / (BbMapArt::MapNameOf(key & ~7u) + suffix + ".bbmap");
}

std::mutex mutex;
std::condition_variable wake;
std::map<std::uint32_t, std::shared_ptr<const BbMapArt::Chart>> ready;
std::set<std::uint32_t> failed, asked;
std::deque<std::uint32_t> queue;
bool worker_started = false;

std::shared_ptr<const BbMapArt::Chart> Make(std::uint32_t key) {
    const std::uint32_t packed_map = key & ~7u;
    const int view = int(key & 7u);
    const auto cache = CachePath(key);
    if (auto kept = BbMapArt::Load(ReadFile(cache))) {
        std::printf("Map: read %s%s from %s\n", BbMapArt::MapNameOf(packed_map).c_str(),
                    view > 0 ? (" view " + std::to_string(view)).c_str() : "", cache.string().c_str());
        return std::make_shared<const BbMapArt::Chart>(std::move(*kept));
    }
    const auto start = std::chrono::steady_clock::now();
    const int threads = int(std::clamp(std::thread::hardware_concurrency() / 2, 1u, 4u));
    // Kaniel 2026-10-18 ("sharper close-up"), 2026-10-19 ("it generally should be sharper"): 6
    // pixels a metre.
    // Kaniel 2026-10-19 ("it looks much better this way"): in the game's own textures.
    // Kaniel 2026-10-19 ("can we prerender it?"): the 3D view's sides at 4 pixels a metre.
    auto drawn = view > 0 ? BbMapArt::ChartArea(packed_map, ReadGameFile, 4.0f, 4096, threads, ReadGameRange, true, view)
                          : BbMapArt::ChartArea(packed_map, ReadGameFile, 6.0f, 6144, threads, ReadGameRange, true);
    if (!drawn) {
        std::printf("Map: no picture of %s in the game's files\n", BbMapArt::MapNameOf(packed_map).c_str());
        return nullptr;
    }
    std::printf("Map: drew %s%s (%dx%d) in %.1f s\n", BbMapArt::MapNameOf(packed_map).c_str(),
                view > 0 ? (" view " + std::to_string(view)).c_str() : "", drawn->width, drawn->height,
                std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count());
    std::error_code error;
    std::filesystem::create_directories(cache.parent_path(), error);
    const auto file = BbMapArt::Save(*drawn);
    std::ofstream(cache, std::ios::binary).write(reinterpret_cast<const char*>(file.data()), std::streamsize(file.size()));
    return std::make_shared<const BbMapArt::Chart>(std::move(*drawn));
}

void Work() {
#ifdef _WIN32
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
#endif
    for (;;) {
        std::uint32_t next = 0;
        {
            std::unique_lock lock{mutex};
            wake.wait(lock, [] { return !queue.empty(); });
            next = queue.front();
            queue.pop_front();
        }
        auto chart = Make(next);
        std::scoped_lock lock{mutex};
        if (chart) {
            ready[next] = std::move(chart);
        } else {
            failed.insert(next);
        }
    }
}

/// The game's text folder (msg/<folder>) for the menus' language.
const char* TextFolder() {
    static const std::map<std::string, const char*> folders{
        {"en", "engus"},  {"ru", "rusru"},  {"ja", "jpnjp"},  {"fr", "frafr"},  {"es", "spaes"},
        {"es_419", "spaar"}, {"de", "deude"}, {"it", "itait"}, {"nl", "nldnl"}, {"pt", "porpt"},
        {"ko", "korkr"},  {"zh_TW", "zhotw"}, {"zh_CN", "zhocn"}, {"fi", "finfi"}, {"sv", "swese"},
        {"da", "dandk"},  {"no", "norno"},  {"pl", "polpl"},  {"pt_BR", "porbr"}, {"tr", "turtr"}};
    const auto found = folders.find(BbSettings::LanguageCode(BbSettings::Get().menu_language.load()));
    return found != folders.end() ? found->second : "engus";
}

/// The game's text of one FMG, in the menus' language (loaded once each).
const std::map<std::uint32_t, std::string>& Text(const char* bnd, std::int32_t fmg) {
    static std::mutex text_mutex;
    static std::map<std::string, std::map<std::uint32_t, std::string>> loaded;
    const std::string folder = TextFolder();
    const std::string key = folder + "/" + bnd + "#" + std::to_string(fmg);
    std::scoped_lock lock{text_mutex};
    auto [it, fresh] = loaded.try_emplace(key);
    if (fresh) {
        auto file = ReadGameFile("msg/" + folder + "/" + bnd);
        if (file.empty()) {
            file = ReadGameFile(std::string("msg/engus/") + bnd);
        }
        if (const auto inflated = BbGameArt::InflateDcx(file)) {
            it->second = BbSaveEditor::GameText(*inflated, fmg);
        }
    }
    return it->second;
}

std::string Named(const char* bnd, std::int32_t fmg, std::int32_t id) {
    const auto& text = Text(bnd, fmg);
    const auto found = text.find(std::uint32_t(id));
    if (found == text.end()) {
        return {};
    }
    // The game pads some names with spaces.
    std::string name = found->second;
    while (!name.empty() && (name.back() == ' ' || name.front() == ' ')) {
        name.erase(name.back() == ' ' ? name.size() - 1 : 0, 1);
    }
    return name;
}

} // namespace

std::shared_ptr<const BbMapArt::Chart> Get(std::uint32_t packed_map) {
    std::scoped_lock lock{mutex};
    if (const auto found = ready.find(packed_map); found != ready.end()) {
        return found->second;
    }
    if (packed_map != 0 && !failed.contains(packed_map) && asked.insert(packed_map).second) {
        queue.push_back(packed_map);
        if (!worker_started) {
            worker_started = true;
            std::thread(Work).detach();
        }
        wake.notify_one();
    }
    return nullptr;
}

bool Failed(std::uint32_t packed_map) {
    std::scoped_lock lock{mutex};
    return failed.contains(packed_map);
}

std::string LampName(std::int32_t name_id) {
    return Named("menu.msgbnd.dcx", 30, name_id);
}

std::string AreaName(std::int32_t name_id) {
    return Named("item.msgbnd.dcx", 19, name_id);
}

} // namespace BbMapCharts
