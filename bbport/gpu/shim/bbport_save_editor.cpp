// SPDX-License-Identifier: GPL-2.0-or-later
#include "bbport_save_editor.h"

#include "../bbcoop.h"
#include "bbport_game_art.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <mutex>
#include <sstream>

namespace BbSaveEditor {
namespace {

using u64 = std::uint64_t;

#define GAME_ABI __attribute__((sysv_abi))

constexpr u64 GameDataManPointer = 0x553B130; ///< +0x08: the local PlayerGameData
constexpr u64 PlayerPointer = 0x553E878;      ///< +0x60: the local PlayerIns
constexpr u64 SetStatsFunction = 0x1c13d00;   ///< (request): level, attributes, recompute
constexpr u64 GiveItemFunction = 0x131cb70;   ///< (_, category, id, quantity), silent
constexpr std::uint32_t AttributeOffsets[AttributeCount] = {0x40, 0x48, 0x58, 0x60, 0x68, 0x70};
constexpr std::uint32_t InsightOffsets[] = {0x80, 0x84}; // the level-up commit writes both
constexpr std::uint32_t LevelOffset = 0x90, EchoesOffset = 0x94;
constexpr std::uint32_t HpOffset = 0x14, HpMaxOffset = 0x18;

u64 image_base = 0;
std::mutex pending_mutex;
std::vector<std::function<void()>> pending;

template <typename T>
T Load(u64 address) {
    T value;
    std::memcpy(&value, reinterpret_cast<const void*>(address), sizeof(T));
    return value;
}
template <typename T>
void StoreValue(u64 address, T value) {
    std::memcpy(reinterpret_cast<void*>(address), &value, sizeof(T));
}

/// The pointer at `base + offset` when `base` is one; 0 otherwise.
u64 Follow(u64 base, u64 offset) {
    return base >= 0x10000 ? Load<u64>(base + offset) : 0;
}

u64 PlayerGameData() {
    return image_base ? Follow(Follow(image_base + GameDataManPointer, 0), 0x08) : 0;
}

/// The character is in the world: its game data and its PlayerIns exist.
bool InWorld() {
    return PlayerGameData() >= 0x10000 &&
           Follow(Follow(image_base + PlayerPointer, 0), 0x60) >= 0x10000;
}

void Queue(std::function<void()> job) {
    std::scoped_lock lock{pending_mutex};
    pending.push_back(std::move(job));
}

std::string ReadFile(const std::filesystem::path& path) {
    std::ifstream in{path, std::ios::binary};
    std::ostringstream text;
    text << in.rdbuf();
    return text.str();
}

std::string items_source;

} // namespace

void SetImage(unsigned char* image) {
    image_base = reinterpret_cast<u64>(image);
}

std::optional<Character> Read() {
    if (!InWorld()) {
        return std::nullopt;
    }
    const u64 data = PlayerGameData();
    Character c;
    c.level = Load<std::int32_t>(data + LevelOffset);
    c.echoes = Load<std::int32_t>(data + EchoesOffset);
    c.insight = Load<std::int32_t>(data + InsightOffsets[1]);
    for (int i = 0; i < AttributeCount; ++i) {
        c.attributes[i] = Load<std::int32_t>(data + AttributeOffsets[i]);
    }
    c.hp = Load<std::int32_t>(data + HpOffset);
    c.hp_max = Load<std::int32_t>(data + HpMaxOffset);
    return c;
}

void SetEchoes(std::int32_t echoes) {
    Queue([echoes] {
        if (const u64 data = PlayerGameData(); data >= 0x10000) {
            StoreValue(data + EchoesOffset, ClampEchoes(echoes));
            std::printf("Save editor: echoes %d\n", ClampEchoes(echoes));
        }
    });
}

void SetInsight(std::int32_t insight) {
    Queue([insight] {
        if (const u64 data = PlayerGameData(); data >= 0x10000) {
            for (const std::uint32_t offset : InsightOffsets) {
                StoreValue(data + offset, ClampInsight(insight));
            }
            std::printf("Save editor: insight %d\n", ClampInsight(insight));
        }
    });
}

void SetAttributes(const std::int32_t attributes[AttributeCount]) {
    std::array<std::int32_t, AttributeCount> wanted;
    for (int i = 0; i < AttributeCount; ++i) {
        wanted[i] = ClampAttribute(attributes[i]);
    }
    Queue([wanted] {
        const auto now = Read();
        if (!now) {
            return;
        }
        const std::int32_t level = LevelAfter(*now, wanted.data());
        auto request = StatsRequest(level, wanted.data());
        using SetStats = void GAME_ABI (*)(const void*);
        reinterpret_cast<SetStats>(image_base + SetStatsFunction)(request.data());
        std::printf("Save editor: level %d, attributes %d %d %d %d %d %d\n", level, wanted[0],
                    wanted[1], wanted[2], wanted[3], wanted[4], wanted[5]);
    });
}

void GiveItem(std::uint32_t category, std::uint32_t id, std::int32_t quantity) {
    Queue([category, id, quantity] {
        if (!InWorld() || quantity == 0) {
            return;
        }
        using Give = void GAME_ABI (*)(void*, std::uint32_t, std::uint32_t, std::int32_t);
        reinterpret_cast<Give>(image_base + GiveItemFunction)(nullptr, category, id, quantity);
        std::printf("Save editor: gave item %#x:%u x%d\n", category, id, quantity);
    });
}

void RunPending() {
    std::vector<std::function<void()>> jobs;
    {
        std::scoped_lock lock{pending_mutex};
        if (pending.empty()) {
            return;
        }
        jobs.swap(pending);
    }
    if (!InWorld()) {
        return; // dropped: the character is not there (title screen, loading)
    }
    for (auto& job : jobs) {
        job();
    }
}

const std::vector<Item>& Items() {
    static const std::vector<Item> items = [] {
        // The game's own item names (English, as the page is).
        char path[1024];
        if (bbcoop_game_file("dvdroot_ps4/msg/engus/item.msgbnd.dcx", path, sizeof(path))) {
            const std::string file = ReadFile(path);
            const auto bnd = BbGameArt::InflateDcx(
                {reinterpret_cast<const std::uint8_t*>(file.data()), file.size()});
            if (auto list = bnd ? ItemsFromGameText(*bnd) : std::vector<Item>{}; !list.empty()) {
                items_source = path;
                std::printf("Save editor: %zu items from the game's text (%s)\n", list.size(),
                            path);
                return list;
            }
        }
        std::vector<std::filesystem::path> candidates;
        if (const char* env = std::getenv("BB_SAVE_EDITOR_DATA"); env && *env) {
            candidates.emplace_back(env);
        }
        // The kit lives in Bloodborne.PC.Online\SeamlessCoopPort; the shadPS4 install's save
        // editor in Bloodborne.PC.Online\DATA.
        std::error_code error;
        auto dir = std::filesystem::current_path(error);
        for (int up = 0; up < 4 && !dir.empty(); ++up, dir = dir.parent_path()) {
            candidates.push_back(dir / "DATA" / "Bloodborne Save Editor" / "resources");
            if (dir == dir.parent_path()) {
                break;
            }
        }
        for (const auto& folder : candidates) {
            if (std::filesystem::exists(folder / "items.json", error)) {
                items_source = folder.string();
                auto list = ParseItems(ReadFile(folder / "items.json"),
                                       ReadFile(folder / "weapons.json"),
                                       ReadFile(folder / "armors.json"));
                std::printf("Save editor: %zu items from %s\n", list.size(), items_source.c_str());
                return list;
            }
        }
        std::printf("Save editor: no item list (BB_SAVE_EDITOR_DATA or DATA/Bloodborne Save "
                    "Editor/resources)\n");
        return std::vector<Item>{};
    }();
    return items;
}

std::string ItemName(std::uint32_t item_id) {
    static const std::map<std::uint32_t, std::string> names = [] {
        // The game's own English text (as the boxes' words are).
        const auto read = [](const char* file) {
            char path[1024];
            if (!bbcoop_game_file(file, path, sizeof(path))) {
                return std::vector<std::uint8_t>{};
            }
            const std::string bytes = ReadFile(path);
            auto bnd = BbGameArt::InflateDcx(
                {reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size()});
            return bnd ? std::move(*bnd) : std::vector<std::uint8_t>{};
        };
        auto list = ItemNamesFromGameText(read("dvdroot_ps4/msg/engus/item.msgbnd.dcx"),
                                          read("dvdroot_ps4/msg/engus/menu.msgbnd.dcx"));
        std::printf("Save editor: %zu item names for trading\n", list.size());
        return list;
    }();
    const auto found = names.find(item_id);
    return found == names.end() ? std::string{} : found->second;
}

const std::string& ItemsSource() {
    Items();
    return items_source;
}

} // namespace BbSaveEditor

extern "C" void bbgpu_game_thread_tick(void) {
    // Once per character in the world: what the page will show (checks the offsets in logs).
    static int logged_level = -1;
    if (const auto c = BbSaveEditor::Read(); c && c->level != logged_level) {
        logged_level = c->level;
        std::printf("Save editor: level %d, echoes %d, insight %d, attributes %d %d %d %d %d %d, "
                    "HP %d/%d\n",
                    c->level, c->echoes, c->insight, c->attributes[0], c->attributes[1],
                    c->attributes[2], c->attributes[3], c->attributes[4], c->attributes[5], c->hp,
                    c->hp_max);
    }
    BbSaveEditor::RunPending();
}
