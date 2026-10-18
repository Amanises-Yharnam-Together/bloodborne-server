// SPDX-License-Identifier: GPL-2.0-or-later
// bbport: trading's menus (bbport_trade_menu.h), built from the game's own dialogs (static RE of
// the 1.09 eboot, 2026-10-14):
//   - the inventory's item commands (Use, Use Multiple, Discard) are a command list opened by
//     `call 0x01BEA630(Job** out, model, Function* factory)` at 0x01BA152A; the factory (vtable
//     0x0533FE00) holds a copy of the item's record at +8, its item id at +0x4C, its handle at +0x70
//     (an item of its own: which one). The call goes to OpenItemCommands, which adds the "Give"
//     rows with AddRow 0x01B4C9A0(model, name+help texts, Function* action, 0) first;
//   - a row's action is a std::function whose call is Do_call(Job** out, self, ctx), ctx + 0x48 the
//     owner for its dialogs; ours opens the quantity picker 0x01C203C0(Job** out, owner,
//     L"NumSelect", text, min, max, Function* on_ok, Function* extra), whose on_ok gets the number;
//   - in the world: InGameTop 0x0176BAB0() (null without one), its +0x48 the owner, the yes/no box
//     0x01C1ED10 and the OK box 0x01C1F500 (Job** out, owner, text), pushed with 0x01BB27A0(Handle*
//     out, root, Job** job, Function* abort) unless MenuMan [0x05562878] +0x2FC says the menus are
//     busy; the yes/no box's answer lands in MenuMan +0x1CC (1 yes, 2 no). An abort function's call
//     (self, ctx) -> bool runs every frame the job lives, true closes it.
// Jobs are reference counted (+8), released as the game does (vtable slot 0 at the last).
// A picked row runs only when the dialog's select lets it: the port's Give rows are let through
// for any item, weapons too (bbport_trade_rows.h).
#include "bbport_trade_menu.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>

#include "../bbcoop.h"
#include "bbport_game_menu.h"
#include "bbport_save_editor.h"
#include "bbport_trade_rows.h"

namespace BbTradeMenu {
namespace {

using u8 = std::uint8_t;
using u32 = std::uint32_t;
using u64 = std::uint64_t;

#define GAME_ABI __attribute__((sysv_abi))

constexpr u64 ItemCommandsOpenCall = 0x01BA152A;
constexpr u64 OpenCommandList = 0x01BEA630;
constexpr u64 ItemCommandsFactoryVtable = 0x0533FE00;
constexpr u64 FactoryItemIdAt = 8 + 0x4C;
constexpr u64 FactoryHandleAt = 8 + 0x70;
constexpr u64 AddRow = 0x01B4C9A0;
constexpr u64 MakeText = 0x01AE8CC0;
constexpr u64 NumSelect = 0x01C203C0;
constexpr u64 NumSelectMovie = 0x049B4736; // L"NumSelect"
constexpr u64 LambdaJob = 0x0195DB60;      // (Job** out, Function* void(), mode)
constexpr u64 VoidFunctionVtable = 0x0533AED0; // {vtable, void(*)()}: calls the pointer
constexpr u64 YesNoBox = 0x01C1ED10;
constexpr u64 OkBox = 0x01C1F500;
constexpr u64 InGameTop = 0x0176BAB0;
constexpr u64 PushJob = 0x01BB27A0;
constexpr u64 MenuManPointer = 0x05562878;
constexpr u64 MenuAnswerAt = 0x1CC;
constexpr u64 MenuBusyAt = 0x2FC;
constexpr u64 OperatorNew = 0x02BBE9A8;
constexpr u64 OperatorDelete = 0x02BBE588;
constexpr u64 PlayerGameDataPointer = 0x0553B130; // [+8]: PlayerGameData
constexpr u64 EquipGameDataAt = 0x1D0;
constexpr u32 RowNameCategory = 200, RowHelpCategory = 201, DialogCategory = 204;
constexpr u32 GoodsItem = 0x40000000;
constexpr u32 EchoesKind = 1, GoodsKind = 2, ItemKind = 3;
constexpr std::size_t TextSize = 0x40, FunctionSize = 0x28;
constexpr u32 MaxGoods = 99, MaxEchoes = 999'999'999;

const u8 ItemCommandsCall[] = {0xE8, 0x01, 0x91, 0x04, 0x00};
// NumSelect fills the picker's numbers on its stack ({lowest 1, max, first number, Up/Down step 1,
// other step 10 (inputs 0x3C/0x3D, 0x42/0x43; not L1/R1 or L2/R2, the rig found)}, read by the
// picker's input 0x01ADD210: Up/Down wrap at the ends) with these instructions; the port calls a
// stub there that takes the lowest and the steps from `picker_steps`: the game's own for its
// pickers, sized to the echoes held for ours.
constexpr u64 NumSelectNumbers = 0x01C2040E;
const u8 NumSelectNumbersCode[] = {
    0xC7, 0x85, 0x38, 0xFF, 0xFF, 0xFF, 0x01, 0x00, 0x00, 0x00, // mov [rbp-0xC8], 1
    0x44, 0x89, 0xB5, 0x3C, 0xFF, 0xFF, 0xFF,                   // mov [rbp-0xC4], r14d (max)
    0x89, 0x9D, 0x40, 0xFF, 0xFF, 0xFF,                         // mov [rbp-0xC0], ebx (first)
    0xC7, 0x85, 0x44, 0xFF, 0xFF, 0xFF, 0x01, 0x00, 0x00, 0x00, // mov [rbp-0xBC], 1
    0xC7, 0x85, 0x48, 0xFF, 0xFF, 0xFF, 0x0A, 0x00, 0x00, 0x00, // mov [rbp-0xB8], 10
};
struct PickerSteps {
    u32 lowest, step, big_step;
};
constexpr PickerSteps GamePickerSteps{1, 1, 10};
PickerSteps picker_steps = GamePickerSteps;
// The inventory's row filter (functor call 0x01BD8150(self, item record) -> keep) leaves out Blood
// Vials, Quicksilver Bullets and Blood Bullets (goods 1000, 900, 901), shown on the HUD only. The port
// keeps them: their rows carry the Give commands (and the game's own Use/Discard).
constexpr u64 InventoryRowFilter = 0x01BD8150;
const u8 InventoryRowFilterPrologue[] = {0x55, 0x48, 0x89, 0xE5, 0x53, 0x50, 0x48, 0x89};
const u8 KeepEveryRow[] = {0xB0, 0x01, 0xC3}; // mov al, 1; ret

u64 image = 0;
template <typename F>
F Game(u64 va) {
    return reinterpret_cast<F>(image + va);
}

u32 give_name = 0, give_help = 0, give_echoes_name = 0, give_echoes_help = 0;
// The Give rows of the item commands open now (their labels), for the select's check.
BbTradeRows::GiveRows give_rows;

// ---- std::function implementations of our own (the game copies and destroys them) ----
struct Impl {
    const void* const* vtable;
    u32 kind;
    u32 id;
    u32 handle;  // an item of its own: which one
    u32 partner; // Kaniel 2026-10-16: who gets it (an index into `partners`)
};

// The members a give row goes to, as the item's commands last opened (the game's thread).
constexpr int MaxPartners = 3;
char partners[MaxPartners][48] = {};

GAME_ABI Impl* ImplCopy(Impl* self, Impl* where) {
    using New = void* (GAME_ABI *)(u64);
    Impl* copy = where ? where : static_cast<Impl*>(Game<New>(OperatorNew)(sizeof(Impl)));
    *copy = *self;
    return copy;
}
GAME_ABI void* ImplTargetType(Impl*) {
    return nullptr;
}
GAME_ABI void ImplDelete(Impl* self, bool heap) {
    if (heap) {
        using Delete = void (GAME_ABI *)(void*);
        Game<Delete>(OperatorDelete)(self);
    }
}
GAME_ABI void ImplDestroy(Impl*) {}
GAME_ABI void ImplDeleting(Impl* self) {
    ImplDelete(self, true);
}
GAME_ABI void* ImplGet(Impl* self) {
    return reinterpret_cast<u8*>(self) + 8;
}

GAME_ABI void NoopCall() {}

// A job that does nothing, for an action with nothing to open.
void** NothingJob(void** out) {
    alignas(16) u8 function[FunctionSize] = {};
    *reinterpret_cast<u64*>(function) = image + VoidFunctionVtable;
    *reinterpret_cast<void**>(function + 8) = reinterpret_cast<void*>(&NoopCall);
    *reinterpret_cast<void**>(function + 0x20) = function;
    using Make = void (GAME_ABI *)(void**, void*, int);
    Game<Make>(LambdaJob)(out, function, 2);
    BbGameMenu::DestroyGameFunction(function);
    return out;
}

void MakeTextOf(u8* text, u32 category, u32 id) {
    using Make = void* (GAME_ABI *)(void*, u32, u32);
    Game<Make>(MakeText)(text, category, id);
}

// The quantity picker's OK: the offer.
GAME_ABI void** OkCall(void** out, Impl* self, int* chosen) {
    const u32 count = chosen ? u32(*chosen) : 0;
    const char* to = self->partner < MaxPartners ? partners[self->partner] : "";
    const bool sent = count > 0 && bbcoop_trade_offer_to(to, self->kind, self->id, count,
                                                         self->handle);
    std::printf("Trade menu: %s %u of %u/%u to %s\n", sent ? "offered" : "could not offer", count,
                self->kind, self->id, to);
    std::fflush(stdout);
    return NothingJob(out);
}
const void* const ok_vtable[8] = {
    reinterpret_cast<const void*>(&ImplCopy),       reinterpret_cast<const void*>(&ImplCopy),
    reinterpret_cast<const void*>(&OkCall),         reinterpret_cast<const void*>(&ImplTargetType),
    reinterpret_cast<const void*>(&ImplDelete),     reinterpret_cast<const void*>(&ImplDestroy),
    reinterpret_cast<const void*>(&ImplDeleting),   reinterpret_cast<const void*>(&ImplGet)};

// A "Give" row: the quantity picker, up to what the player holds.
const char* BoxState(); // below, with the boxes

GAME_ABI void** GiveCall(void** out, Impl* self, u8* context) {
    char question[256] = {};
    const u32 held = bbcoop_trade_held(self->kind, self->id);
    const u32 max = std::min(held, self->kind == EchoesKind ? MaxEchoes
                                   : self->kind == ItemKind ? 1u
                                                            : MaxGoods);
    const char* to = self->partner < MaxPartners ? partners[self->partner] : "";
    const bool asked =
        max > 0 && bbcoop_trade_give_question_to(to, self->kind, self->id, question, sizeof question);
    // Kaniel 2026-10-16: after the first give every Give row did nothing, silently. Said now.
    std::printf("Trade menu: Give %u/%u to %s: held %u, max %u, question %s, a box %s\n", self->kind,
                self->id, to, held, max, asked ? "ok" : "none",
                BoxState());
    std::fflush(stdout);
    if (!asked) {
        return NothingJob(out);
    }
    alignas(16) u8 text[TextSize] = {};
    MakeTextOf(text, DialogCategory, BbGameMenu::SetDynamicText(0, question));
    alignas(16) u8 on_ok[FunctionSize] = {};
    *reinterpret_cast<Impl*>(on_ok) = {ok_vtable, self->kind, self->id, self->handle,
                                        self->partner};
    *reinterpret_cast<void**>(on_ok + 0x20) = on_ok;
    alignas(16) u8 extra[FunctionSize] = {};
    u32 first = 1;
    if (self->kind == EchoesKind) {
        // Echoes in round steps sized to what the player holds, from one step, its lowest (the
        // picker reads them as it opens): past the last step and below the first comes `max`.
        u32 step = 1, big_step = 10;
        bbcoop_trade_echoes_picker(&step, &big_step);
        first = std::min(step, max);
        picker_steps = {first, step, big_step};
        std::printf("Trade menu: echoes picker of %u, step %u\n", max, step);
        std::fflush(stdout);
    }
    using Open = void (GAME_ABI *)(void**, void*, const char16_t*, void*, int, int, void*, void*);
    Game<Open>(NumSelect)(out, context + 0x48, Game<const char16_t*>(NumSelectMovie), text,
                          int(first), int(max), on_ok, extra);
    if (!*out) {
        std::printf("Trade menu: the game opened no picker\n");
        std::fflush(stdout);
    }
    picker_steps = GamePickerSteps;
    BbGameMenu::DestroyGameFunction(on_ok);
    BbGameMenu::DestroyGameText(text);
    return out;
}
const void* const give_vtable[8] = {
    reinterpret_cast<const void*>(&ImplCopy),       reinterpret_cast<const void*>(&ImplCopy),
    reinterpret_cast<const void*>(&GiveCall),       reinterpret_cast<const void*>(&ImplTargetType),
    reinterpret_cast<const void*>(&ImplDelete),     reinterpret_cast<const void*>(&ImplDestroy),
    reinterpret_cast<const void*>(&ImplDeleting),   reinterpret_cast<const void*>(&ImplGet)};

void AddGiveRow(void* model, u32 name, u32 help, u32 kind, u32 id, u32 handle, u32 partner) {
    alignas(16) u8 pair[2 * TextSize] = {};
    MakeTextOf(pair, RowNameCategory, name);
    MakeTextOf(pair + TextSize, RowHelpCategory, help);
    alignas(16) u8 action[FunctionSize] = {};
    *reinterpret_cast<Impl*>(action) = {give_vtable, kind, id, handle, partner};
    *reinterpret_cast<void**>(action + 0x20) = action;
    using Add = void* (GAME_ABI *)(void*, void*, void*, int);
    Game<Add>(AddRow)(model, pair, action, 0);
    BbGameMenu::DestroyGameFunction(action);
    BbGameMenu::DestroyGameText(pair);
    BbGameMenu::DestroyGameText(pair + TextSize);
}

// The inventory's item commands: a "Give" row for any item (Kaniel 2026-10-15), when someone is
// near; "Give Blood Echoes" on every item's rows while the player holds any (Kaniel 2026-10-16:
// echoes easy to find, not only on the vials and bullets).
GAME_ABI void* OpenItemCommands(void** out, void* model, u8* factory) {
    give_rows.Clear();
    u8* impl = factory ? *reinterpret_cast<u8**>(factory + 0x20) : nullptr;
    if (impl && *reinterpret_cast<u64*>(impl) == image + ItemCommandsFactoryVtable &&
        bbcoop_trade_on()) {
        const u32 item = *reinterpret_cast<u32*>(impl + FactoryItemIdAt);
        const u32 handle = *reinterpret_cast<u32*>(impl + FactoryHandleAt);
        const bool goods = (item & 0xF0000000) == GoodsItem;
        const u32 kind = goods ? GoodsKind : ItemKind;
        const u32 id = goods ? item & 0xFFFFFFF : item;
        // Kaniel 2026-10-16: one row per member near ("Give to Lisara"), so the giver picks who
        // gets it (dynamic texts 2-4 and 5-7).
        char names[MaxPartners][48] = {};
        int count = std::min(bbcoop_trade_partner_count(), MaxPartners);
        for (int i = 0; i < count; ++i) {
            if (!bbcoop_trade_partner(i, partners[i], sizeof partners[i], names[i],
                                      sizeof names[i])) {
                count = i;
                break;
            }
        }
        const bool give = count > 0 && bbcoop_trade_tradeable(kind, id);
        const bool echoes = count > 0 && bbcoop_trade_held(EchoesKind, 0) > 0;
        std::printf("Trade menu: commands of item %#x (handle %#x), %d to give to%s%s\n", item,
                    handle, count, give ? ", Give" : "", echoes ? ", Give Blood Echoes" : "");
        std::fflush(stdout);
        for (int i = 0; give && i < count; ++i) {
            const std::string row = std::string("Give to ") + names[i];
            give_rows.AddItem(row);
            AddGiveRow(model, BbGameMenu::SetDynamicText(2 + i, row.c_str()), give_help, kind, id,
                       goods ? 0 : handle, u32(i));
        }
        for (int i = 0; echoes && i < count; ++i) {
            const std::string row = std::string("Give Blood Echoes to ") + names[i];
            give_rows.AddEchoes(row);
            AddGiveRow(model, BbGameMenu::SetDynamicText(5 + i, row.c_str()), give_echoes_help,
                       EchoesKind, 0, 0, u32(i));
        }
    }
    using Open = void* (GAME_ABI *)(void**, void*, void*);
    return Game<Open>(OpenCommandList)(out, model, factory);
}

// The item commands' select, in place of the game's "can be used now" (never for a weapon, armor
// or gem: their Give rows did nothing, play test 2026-10-18): our Give rows run as the game's
// Discard does, the game's rows keep its check.
GAME_ABI bool SelectCheck(int index, u8* frame) {
    const BbTradeRows::Row row =
        give_rows.Find(BbTradeRows::LabelText(frame + BbTradeRows::PickedLabelAt));
    const auto usable = [index] {
        using Usable = bool (GAME_ABI *)(int);
        return Game<Usable>(BbTradeRows::UsableCheck)(index);
    };
    const auto equip_state = [index] {
        u8* game_data = *Game<u8**>(PlayerGameDataPointer);
        u8* player = game_data ? *reinterpret_cast<u8**>(game_data + 8) : nullptr;
        if (!player) {
            return BbTradeRows::EquipBusy;
        }
        using Equip = int (GAME_ABI *)(void*, int);
        return Game<Equip>(BbTradeRows::EquipCheck)(player + EquipGameDataAt, index);
    };
    const bool allowed = BbTradeRows::Allowed(row, usable, equip_state);
    if (row != BbTradeRows::Row::Game) {
        std::printf("Trade menu: Give row picked (inventory index %d): %s\n", index,
                    allowed ? "runs" : "not now (the player can't change equipment)");
        std::fflush(stdout);
    }
    return allowed;
}

// ---- In the world: the question for an offer, and the notices ----
void Release(void* object) {
    if (!object) {
        return;
    }
    auto* count = reinterpret_cast<int*>(static_cast<u8*>(object) + 8);
    if (__atomic_fetch_sub(count, 1, __ATOMIC_ACQ_REL) == 1) {
        using Destroy = void (GAME_ABI *)(void*);
        (*reinterpret_cast<Destroy**>(object))[0](object);
    }
}

u8* MenuMan() {
    return *Game<u8**>(MenuManPointer);
}

// The box shown now: for which offer (0: a notice), its job (our reference keeps it; once the
// game let go of it, ours is the last: the box is gone) and whether it got its turn (the abort
// function runs every frame from then; a job pushed behind another waits).
struct Shown {
    void* job = nullptr;
    u64 ask = 0;
    bool running = false;
};
Shown shown;

const char* BoxState() {
    return !shown.job ? "none" : shown.running ? "shown" : "waiting";
}

GAME_ABI bool AbortCall(Impl*, void*) {
    if (!shown.running) {
        // Its turn now: the answer field is ours from here (a dialog before it wrote it too).
        shown.running = true;
        if (u8* menus = MenuMan()) {
            *reinterpret_cast<int*>(menus + MenuAnswerAt) = -1;
        }
    }
    return shown.ask != 0 && !bbcoop_trade_ask_open(shown.ask);
}
const void* const abort_vtable[8] = {
    reinterpret_cast<const void*>(&ImplCopy),       reinterpret_cast<const void*>(&ImplCopy),
    reinterpret_cast<const void*>(&AbortCall),      reinterpret_cast<const void*>(&ImplTargetType),
    reinterpret_cast<const void*>(&ImplDelete),     reinterpret_cast<const void*>(&ImplDestroy),
    reinterpret_cast<const void*>(&ImplDeleting),   reinterpret_cast<const void*>(&ImplGet)};

int References(void* object) {
    return __atomic_load_n(reinterpret_cast<int*>(static_cast<u8*>(object) + 8), __ATOMIC_ACQUIRE);
}

// Opens a box in the world with `text`: false when the game can't show one now.
bool OpenBox(u64 box, const char* utf8) {
    using Root = u8* (GAME_ABI *)();
    u8* root = Game<Root>(InGameTop)();
    u8* menus = MenuMan();
    if (!root || !menus || *reinterpret_cast<int*>(menus + MenuBusyAt) != 0) {
        return false;
    }
    alignas(16) u8 text[TextSize] = {};
    MakeTextOf(text, DialogCategory, BbGameMenu::SetDynamicText(1, utf8));
    void* job = nullptr;
    using Make = void (GAME_ABI *)(void**, void*, void*);
    Game<Make>(box)(&job, root + 0x48, text);
    BbGameMenu::DestroyGameText(text);
    if (!job) {
        return false;
    }
    alignas(16) u8 abort[FunctionSize] = {};
    *reinterpret_cast<Impl*>(abort) = {abort_vtable, 0, 0};
    *reinterpret_cast<void**>(abort + 0x20) = abort;
    void* handle = nullptr;
    shown = {job, 0, false};
    using Push = void (GAME_ABI *)(void**, void*, void**, void*);
    Game<Push>(PushJob)(&handle, root, &job, abort);
    BbGameMenu::DestroyGameFunction(abort);
    const bool pushed = handle != nullptr;
    Release(handle);
    if (!pushed) {
        Release(job);
        shown = {};
    }
    std::printf("Trade menu: box %s: %s\n", pushed ? "pushed" : "not pushed", utf8);
    std::fflush(stdout);
    return pushed;
}

// The co-op code's trades tick (the game's thread).
void Tick() {
    if (shown.job) {
        u8* menus = MenuMan();
        const int answer = menus ? *reinterpret_cast<int*>(menus + MenuAnswerAt) : -1;
        if (shown.running && shown.ask != 0 && (answer == 1 || answer == 2)) {
            bbcoop_trade_answer(shown.ask, answer == 1);
            std::printf("Trade menu: offer %llu answered %s\n", (unsigned long long)shown.ask,
                        answer == 1 ? "yes" : "no");
            shown.ask = 0;
        }
        if (References(shown.job) > 1) {
            return; // still the game's
        }
        std::printf("Trade menu: box gone (%s)\n", shown.running ? "shown" : "never shown");
        std::fflush(stdout);
        if (shown.ask != 0 && bbcoop_trade_ask_open(shown.ask)) {
            bbcoop_trade_answer(shown.ask, 0); // closed without an answer: a no
        }
        Release(shown.job);
        shown = {};
    }
    char text[512];
    u64 ask = 0;
    if (bbcoop_trade_ask(&ask, text, sizeof text)) {
        if (OpenBox(YesNoBox, text)) {
            shown.ask = ask;
        }
        return;
    }
    static std::string notice;
    if (notice.empty() && bbcoop_trade_notice(text, sizeof text)) {
        notice = text;
    }
    if (!notice.empty() && OpenBox(OkBox, notice.c_str())) {
        notice.clear();
    }
}

void TickCall() {
    Tick();
}

int NameItem(std::uint32_t item_id, char* text, std::size_t size) {
    const std::string name = BbSaveEditor::ItemName(item_id);
    if (name.empty() || size == 0) {
        return 0;
    }
    std::snprintf(text, size, "%s", name.c_str());
    return 1;
}

} // namespace

bool PatchImage(unsigned char* image_start, std::uint64_t size, std::uint8_t*& stubs) {
    image = reinterpret_cast<u64>(image_start);
    if (InventoryRowFilter + sizeof InventoryRowFilterPrologue > size ||
        std::memcmp(image_start + InventoryRowFilter, InventoryRowFilterPrologue,
                    sizeof InventoryRowFilterPrologue) != 0 ||
        ItemCommandsOpenCall + sizeof ItemCommandsCall > size ||
        std::memcmp(image_start + ItemCommandsOpenCall, ItemCommandsCall,
                    sizeof ItemCommandsCall) != 0 ||
        NumSelectNumbers + sizeof NumSelectNumbersCode > size ||
        std::memcmp(image_start + NumSelectNumbers, NumSelectNumbersCode,
                    sizeof NumSelectNumbersCode) != 0) {
        std::printf("Trade menu: unexpected code in the inventory, no Give row\n");
        return false;
    }
    // The select's check first (it changes nothing when the code isn't the 1.09 eboot's).
    if (!BbTradeRows::PatchSelectCheck(image_start, size, stubs + 64,
                                       reinterpret_cast<const void*>(&SelectCheck))) {
        std::printf("Trade menu: unexpected code in the item commands' select, no Give row\n");
        return false;
    }
    std::memcpy(image_start + InventoryRowFilter, KeepEveryRow, sizeof KeepEveryRow);
    give_name = BbGameMenu::AddGameText("Give", "Отдать");
    give_help = BbGameMenu::AddGameText("Give some to this party member",
                                        "Отдать этому участнику группы");
    give_echoes_name = BbGameMenu::AddGameText("Give Blood Echoes", "Отдать Отголоски крови");
    give_echoes_help = BbGameMenu::AddGameText(
        "Give some of your Blood Echoes to this party member",
        "Отдать часть своих Отголосков крови этому участнику группы");
    // call rel32 -> a thunk: movabs rax, OpenItemCommands; jmp rax.
    u8* thunk = stubs;
    void* hook = reinterpret_cast<void*>(&OpenItemCommands);
    thunk[0] = 0x48;
    thunk[1] = 0xB8;
    std::memcpy(thunk + 2, &hook, 8);
    thunk[10] = 0xFF;
    thunk[11] = 0xE0;
    // The picker's numbers: call a stub that stores them as the game did, the lowest and the steps
    // from picker_steps (rax and rdx are free there: the game sets both next).
    u8* numbers = stubs + 16;
    const PickerSteps* steps = &picker_steps;
    const u8 numbers_head[] = {
        0x44, 0x89, 0xB5, 0x3C, 0xFF, 0xFF, 0xFF, // mov [rbp-0xC4], r14d
        0x89, 0x9D, 0x40, 0xFF, 0xFF, 0xFF,       // mov [rbp-0xC0], ebx
        0x48, 0xB8};                              // movabs rax, &picker_steps
    const u8 numbers_tail[] = {
        0x8B, 0x10,                               // mov edx, [rax]
        0x89, 0x95, 0x38, 0xFF, 0xFF, 0xFF,       // mov [rbp-0xC8], edx
        0x48, 0x8B, 0x50, 0x04,                   // mov rdx, [rax+4]
        0x48, 0x89, 0x95, 0x44, 0xFF, 0xFF, 0xFF, // mov [rbp-0xBC], rdx (both steps)
        0xC3};                                    // ret
    std::memcpy(numbers, numbers_head, sizeof numbers_head);
    std::memcpy(numbers + sizeof numbers_head, &steps, 8);
    std::memcpy(numbers + sizeof numbers_head + 8, numbers_tail, sizeof numbers_tail);
    const std::int64_t to =
        std::int64_t(thunk) - std::int64_t(image_start + ItemCommandsOpenCall + 5);
    const std::int64_t to_numbers =
        std::int64_t(numbers) - std::int64_t(image_start + NumSelectNumbers + 5);
    if (to != std::int32_t(to) || to_numbers != std::int32_t(to_numbers)) {
        std::printf("Trade menu: stub out of call range, no Give row\n");
        return false;
    }
    const std::int32_t to32 = std::int32_t(to);
    std::memcpy(image_start + ItemCommandsOpenCall + 1, &to32, 4);
    // call numbers; jmp past the rest of the game's instructions.
    u8* site = image_start + NumSelectNumbers;
    std::memset(site, 0xCC, sizeof NumSelectNumbersCode);
    site[0] = 0xE8;
    const std::int32_t numbers32 = std::int32_t(to_numbers);
    std::memcpy(site + 1, &numbers32, 4);
    site[5] = 0xEB;
    site[6] = u8(sizeof NumSelectNumbersCode - 7);
    stubs += 16 + 48 + BbTradeRows::ThunkSize;
    bbcoop_trade_set_ui_tick(&TickCall);
    bbcoop_trade_set_namer(&NameItem);
    std::printf("Trade menu: in (Give rows, offers asked in the world)\n");
    return true;
}

} // namespace BbTradeMenu
