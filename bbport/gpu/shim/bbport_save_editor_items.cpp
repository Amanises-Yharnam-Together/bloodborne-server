// SPDX-License-Identifier: GPL-2.0-or-later
#include "bbport_save_editor.h"

#include <algorithm>
#include <cstdlib>
#include <nlohmann/json.hpp>

namespace BbSaveEditor {

std::vector<Item> ParseItems(std::string_view goods, std::string_view weapons,
                             std::string_view armors) {
    std::vector<Item> items;
    const auto add = [&](std::uint32_t category, const nlohmann::json& table) {
        if (!table.is_object()) {
            return;
        }
        for (const auto& [key, entry] : table.items()) {
            char* end = nullptr;
            const unsigned long id = std::strtoul(key.c_str(), &end, 10);
            if (end == key.c_str() || *end != '\0' || !entry.is_object()) {
                continue;
            }
            const auto name = entry.find("item_name");
            items.push_back({category, static_cast<std::uint32_t>(id),
                             name != entry.end() && name->is_string() ? name->get<std::string>()
                                                                      : key});
        }
    };
    const auto parse = [](std::string_view text) {
        return nlohmann::json::parse(text.begin(), text.end(), nullptr, false);
    };
    if (const auto j = parse(goods); j.is_object()) {
        for (const auto& [group, table] : j.items()) {
            add(GoodsCategory, table);
        }
    }
    if (const auto j = parse(weapons); j.is_object()) {
        for (const auto& [hand, table] : j.items()) {
            add(WeaponCategory, table);
        }
    }
    if (const auto j = parse(armors); j.is_object()) {
        add(ArmorCategory, j);
    }
    std::ranges::sort(items, [](const Item& a, const Item& b) {
        return a.category != b.category ? a.category > b.category : a.name < b.name;
    });
    return items;
}

} // namespace BbSaveEditor
