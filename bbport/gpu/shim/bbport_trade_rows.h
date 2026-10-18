// SPDX-License-Identifier: GPL-2.0-or-later
// bbport: which of the inventory's item commands are trading's own (bbport_trade_menu.cpp), and the
// game's check that lets a picked command run.
//
// The commands dialog's select (0x01BA2600, static RE of the 1.09 eboot, 2026-10-18) runs the
// picked row (0x01BEAAD0) only when the game allows it: Discard unless the player can't change
// equipment now (0x014D0310's check 0x014D0430(EquipGameData, inventory index) answers 3), Use
// Multiple with two or more held, and every other row only when 0x016E4F00(inventory index) says
// the item can be used now: never for weapons, armor, gems or runes, so their Give rows did nothing
// (play test 2026-10-18). The port calls `check` there instead of 0x016E4F00 (the call at
// 0x01BA2D81) with the select's frame: the picked row's label is a text (0x40 bytes) at rbp-0xA8.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace BbTradeRows {

constexpr std::uint64_t SelectCheckCall = 0x01BA2D81; // call 0x016E4F00 (rdi: inventory index)
constexpr std::uint64_t UsableCheck = 0x016E4F00;     // bool (int index)
constexpr std::uint64_t EquipCheck = 0x014D0430;      // int (EquipGameData*, int index)
constexpr std::int64_t PickedLabelAt = -0xA8;          // in the select's frame (rbp)
constexpr int EquipBusy = 3;                           // EquipCheck: no equipment change now
constexpr std::size_t ThunkSize = 16;

/// The text of a game text (0x40 bytes: +0 its cached text or null, +0x10 the characters inline
/// or a pointer to them once its capacity at +0x28 is 8 or more), as the select compares labels.
const char16_t* LabelText(const unsigned char* text);

/// UTF-16 to UTF-8.
std::string Utf8(const char16_t* text);

enum class Row { Game, GiveItem, GiveEchoes };

/// The Give rows of the item commands open now (the game's thread).
class GiveRows {
public:
    void Clear();
    void AddItem(std::string label);
    void AddEchoes(std::string label);
    Row Find(const char16_t* label) const;

private:
    std::vector<std::string> items, echoes;
};

/// Whether the select runs the picked row: an item's Give row as the game's Discard (not while the
/// player can't change equipment: taking a weapon out unequips it), Give Blood Echoes always, the
/// game's rows by the game's own check.
template <typename Usable, typename EquipState>
bool Allowed(Row row, Usable usable, EquipState equip_state) {
    switch (row) {
    case Row::GiveEchoes:
        return true;
    case Row::GiveItem:
        return equip_state() != EquipBusy;
    case Row::Game:
        break;
    }
    return usable();
}

/// Points the select's call of UsableCheck at `check` (bool GAME_ABI (int index, u8* frame))
/// through a thunk written at `thunk` (ThunkSize bytes, within a rel32 of the call): false, and
/// nothing changed, when the call isn't the 1.09 eboot's.
bool PatchSelectCheck(unsigned char* image, std::uint64_t size, unsigned char* thunk,
                      const void* check);

} // namespace BbTradeRows
