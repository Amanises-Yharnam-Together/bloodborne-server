// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

// Trading between party members, the game's side (server/coop/trade.h has the protocol): with
// SHADPS4_BLOODBORNE_TRADE=1 (the port only, with the server world) a member offers another
// Blood Echoes, Blood Vials or Quicksilver Bullets and may ask for some back; shadNet holds the
// offer until the other answers. This file takes items out of the player's inventory and puts
// them in (with the game's own "item obtained" popup), keeps the trades' client and answers to
// the rig's verbs; the native menus are in bloodborne_trade_ui.cpp.
//
// The game's functions (1.09, static RE 2026-10-14, docs/TRADING.md):
//   PlayerGameData = [GameDataMan 0x0553B130] +0x08: +0x94 Blood Echoes (0..999,999,999), the
//   inventory at +0x328 (entries of 16 bytes: handle, item id, count), EquipGameData at +0x1D0.
//   Find 0x014D9F60(inventory, &item id) -> index; count at the entry's +8.
//   Remove 0x014D0BE0(EquipGameData, index, -n, force, no_replace, allow 111) -> new count (n
//   clamped to the count first: a count below 0 turns into the max).
//   Give 0x017D89F0(MapItemMan [0x0553D6E0], list {n, {handle, item id, count, -1}[n]}): what a
//   pickup calls, up to the max held, the rest to the storage box, each with the popup.
//   Echoes: the shop's pay writes +0x94 itself; AddSouls (player vtable +0x3D0) adds them.

#include "core/bloodborne_re_internal.h"
#include "core/bloodborne_trade.h"

#include <deque>

namespace Core::Bloodborne {
namespace {

constexpr u64 GameDataManPointerOffset = 0x0553B130;
constexpr u64 PlayerGameDataOffset = 0x08;
constexpr u64 EchoesOffset = 0x94;
constexpr u64 EquipGameDataOffset = 0x1D0;
constexpr u64 InventoryOffset = 0x328;
constexpr u64 InventoryFindOffset = 0x014D9F60;
constexpr u64 InventoryChangeCountOffset = 0x014D0BE0;
constexpr u64 GiveItemsOffset = 0x017D89F0;
constexpr u32 GoodsCategory = 0x40000000;
constexpr u32 GoodsHandle = 0xB0000000;

const bool trade_switch = [] {
#ifdef BLOODBORNE_NATIVE_PORT
    const char* env = std::getenv("SHADPS4_BLOODBORNE_TRADE");
    return env != nullptr && std::string_view{env} == "1";
#else
    return false; // the port only (docs/SERVER_COOP_PLAN.md)
#endif
}();

std::atomic<bool> server_trades{false};
std::atomic<bool> trade_sites{false};
// The port's trade menus (bbport_trade_menu.cpp), run on the game's thread with the trades.
std::atomic<void (*)()> trade_ui_tick{nullptr};

// The client and what came from the server for it: the stream's thread queues, the game's
// thread takes.
std::mutex trade_mutex;
TradeClient client;
std::deque<bbcoop::Envelope> trade_inbox;
// The notices seen this run, for the rig (each kind's count) and the UI (the queue).
std::map<TradeClient::Notice::Kind, u32> notices_seen;
std::deque<TradeClient::Notice> notices_to_show;

u64 PlayerGameData() {
    const u64 manager = ReadValue<u64>(image_base + GameDataManPointerOffset, 0);
    return manager >= 0x10000 ? ReadValue<u64>(manager, PlayerGameDataOffset) : 0;
}

s32 GoodsIndex(u64 data, u32 goods) {
    const u32 item_id = GoodsCategory | goods;
    using Find = s32 PS4_SYSV_ABI (*)(u64 inventory, const u32* item_id);
    return reinterpret_cast<Find>(image_base + InventoryFindOffset)(data + InventoryOffset,
                                                                    &item_id);
}

// The inventory entry of a goods (its address), or 0 when the player has none.
u64 GoodsEntry(u64 data, u32 goods) {
    const s32 index = GoodsIndex(data, goods);
    if (index < 0) {
        return 0;
    }
    const u64 inventory = data + InventoryOffset;
    const u32 split = ReadValue<u32>(inventory, 0x24);
    const bool first = static_cast<u32>(index) < split;
    const u64 entries = ReadValue<u64>(inventory, first ? 0x58 : 0x48);
    const u64 entry =
        entries + 16ull * (first ? static_cast<u32>(index) : static_cast<u32>(index) - split);
    return entries >= 0x10000 && ReadValue<u32>(entry, 4) == (GoodsCategory | goods) ? entry : 0;
}

} // namespace

namespace Coop {

bool TradeSwitchOn() {
    return trade_switch && ServerWorldOn();
}

bool TradesOn() {
    return TradeSwitchOn() && trade_sites.load() && server_trades.load();
}

void EnableTrading() {
    trade_sites.store(true);
}

void TakeServerTradeWelcome(bool trades) {
    server_trades.store(trades);
}

void TakeServerTrade(bbcoop::Envelope envelope) {
    std::scoped_lock lock{trade_mutex};
    trade_inbox.push_back(std::move(envelope));
}

// What the player holds of an item: its echoes, or the goods in its inventory (not the storage).
u32 TradeHeld(const TradeItem& item) {
    const u64 data = PlayerGameData();
    if (data < 0x10000) {
        return 0;
    }
    if (item.kind == TradeItemKind::Echoes) {
        return static_cast<u32>(std::max(ReadValue<s32>(data, EchoesOffset), 0));
    }
    const u64 entry = GoodsEntry(data, item.id);
    return entry != 0 ? static_cast<u32>(std::max(ReadValue<s32>(entry, 8), 0)) : 0;
}

// Takes the items out of the inventory, all or none (the game's thread).
bool TakeTradeItems(std::span<const TradeItem> items) {
    const u64 data = PlayerGameData();
    if (!trade_sites.load() || data < 0x10000 || TradeItemsProblem(items, true)) {
        return false;
    }
    for (const TradeItem& item : items) {
        if (TradeHeld(item) < item.count) {
            return false;
        }
    }
    using ChangeCount = s32 PS4_SYSV_ABI (*)(u64 equip, u32 index, s32 delta, bool force,
                                             bool no_replace, bool allow_111);
    const auto change = reinterpret_cast<ChangeCount>(image_base + InventoryChangeCountOffset);
    for (const TradeItem& item : items) {
        if (item.kind == TradeItemKind::Echoes) {
            const s32 echoes = ReadValue<s32>(data, EchoesOffset);
            WriteValue(data, EchoesOffset, std::max(echoes - static_cast<s32>(item.count), 0));
            continue;
        }
        const s32 index = GoodsIndex(data, item.id);
        change(data + EquipGameDataOffset, static_cast<u32>(index), -static_cast<s32>(item.count),
               false, true, false);
    }
    if (!items.empty()) {
        LOG_INFO(Debug, "Bloodborne trade: took {} out of the inventory", TradeItemsText(items));
    }
    return true;
}

// Puts the items in as a pickup does: goods up to the max held, the rest to the storage box,
// each with the game's popup; echoes through AddSouls (the game's thread).
void AddTradeItems(std::span<const TradeItem> items) {
    if (!trade_sites.load()) {
        return;
    }
    struct GiveEntry {
        u32 handle;
        u32 item_id;
        s32 count;
        s32 extra;
    };
    struct GiveList {
        u32 count;
        GiveEntry entries[6];
    };
    GiveList list{};
    for (const TradeItem& item : items) {
        if (item.kind == TradeItemKind::Echoes) {
            const u64 player = GetLocalPlayer();
            if (HasVirtual(player, 0x3D0, AddSoulsOffset, "trade AddSouls")) {
                using AddSouls = void PS4_SYSV_ABI (*)(u64 player, s32 amount, s32, bool rates);
                reinterpret_cast<AddSouls>(image_base + AddSoulsOffset)(
                    player, static_cast<s32>(item.count), 0, false);
            }
            continue;
        }
        if (list.count < std::size(list.entries)) {
            list.entries[list.count++] = {GoodsHandle | item.id, GoodsCategory | item.id,
                                          static_cast<s32>(item.count), -1};
        }
    }
    if (list.count > 0) {
        const u64 item_man = ReadValue<u64>(image_base + MapItemManPointerOffset, 0);
        if (HasVtable(item_man, MapItemManVtable, "trade give")) {
            using Give = void PS4_SYSV_ABI (*)(u64 item_man, GiveList* list);
            reinterpret_cast<Give>(image_base + GiveItemsOffset)(item_man, &list);
        } else {
            LOG_ERROR(Debug, "Bloodborne trade: no item man to add {}", TradeItemsText(items));
        }
    }
    LOG_INFO(Debug, "Bloodborne trade: added {}", TradeItemsText(items));
}

// The other members a trade may go to: on the vitals board, on this map, standing, the nearest
// first.
std::vector<std::string> TradePartners() {
    const VitalsView view = ReadVitalsBoard();
    const auto self = std::ranges::find(view.board.members, view.member, &MemberVitals::member);
    std::vector<std::pair<float, std::string>> partners;
    for (const MemberVitals& member : view.board.members) {
        if (member.member == view.member || member.vital != Vital::Up ||
            (self != view.board.members.end() && self->map != member.map)) {
            continue;
        }
        float distance = 0.0f;
        if (self != view.board.members.end()) {
            for (size_t axis = 0; axis < 3; ++axis) {
                const float delta = member.at[axis] - self->at[axis];
                distance += delta * delta;
            }
        }
        partners.emplace_back(distance, member.member);
    }
    std::ranges::sort(partners);
    std::vector<std::string> names;
    for (auto& [distance, name] : partners) {
        names.push_back(std::move(name));
    }
    return names;
}

std::string TradeName(const std::string& member) {
    const std::string name = OnlineIdOfBoardMember(member);
    return name.empty() ? member : name;
}

TradeOfferResult OfferTrade(const std::string& to, TradeItems give, TradeItems want) {
    if (!TradesOn()) {
        return TradeOfferResult::Off;
    }
    if (TradeItemsProblem(give, false) || TradeItemsProblem(want, true)) {
        return TradeOfferResult::Bad;
    }
    if (!TakeTradeItems(give)) {
        return TradeOfferResult::NotEnough;
    }
    bbcoop::Envelope envelope;
    {
        std::scoped_lock lock{trade_mutex};
        envelope = client.Offer(to, TradeName(to), LocalOnlineId(), give, want);
    }
    QueueServerWorld(std::move(envelope));
    LOG_INFO(Debug, "Bloodborne trade: offered {} {}{}", TradeName(to), TradeItemsText(give),
             want.empty() ? "" : " for " + TradeItemsText(want));
    return TradeOfferResult::Sent;
}

std::optional<bbcoop::TradeAsk> FrontTradeAsk() {
    std::scoped_lock lock{trade_mutex};
    if (client.Asked().empty()) {
        return std::nullopt;
    }
    return client.Asked().front();
}

bool AnswerTrade(u64 id, bool accept) {
    TradeItems taken;
    if (accept) {
        const auto ask = FrontTradeAsk();
        if (!ask || ask->id() != id) {
            return false;
        }
        taken = TradeItemsOf(ask->want());
        if (!TakeTradeItems(taken)) {
            accept = false; // can't give what was asked: a no
            taken.clear();
        }
    }
    bbcoop::Envelope envelope;
    {
        std::scoped_lock lock{trade_mutex};
        envelope = client.Answer(id, accept, taken);
    }
    QueueServerWorld(std::move(envelope));
    LOG_INFO(Debug, "Bloodborne trade: {} offer {}", accept ? "accepted" : "declined", id);
    return accept;
}

u32 TradeNoticesSeen(TradeClient::Notice::Kind kind) {
    std::scoped_lock lock{trade_mutex};
    const auto found = notices_seen.find(kind);
    return found == notices_seen.end() ? 0 : found->second;
}

std::optional<TradeClient::Notice> NextTradeNotice() {
    std::scoped_lock lock{trade_mutex};
    if (notices_to_show.empty()) {
        return std::nullopt;
    }
    TradeClient::Notice notice = std::move(notices_to_show.front());
    notices_to_show.pop_front();
    return notice;
}

// The game's thread, every party tick: what came from the server, grants added.
void TickTrades() {
    if (!TradeSwitchOn() || !trade_sites.load()) {
        return;
    }
    std::vector<TradeItems> add;
    std::vector<bbcoop::Envelope> replies;
    {
        std::scoped_lock lock{trade_mutex};
        // Grants wait while the player is not in the world (a load): the popup needs it.
        const bool can_add = GetLocalPlayer() >= 0x10000 && PlayerGameData() >= 0x10000;
        std::deque<bbcoop::Envelope> kept;
        for (bbcoop::Envelope& envelope : trade_inbox) {
            if (envelope.has_trade_grant() && !can_add) {
                kept.push_back(std::move(envelope));
                continue;
            }
            TradeClient::Taken taken = client.Take(envelope);
            for (TradeItems& items : taken.add) {
                add.push_back(std::move(items));
            }
            for (bbcoop::Envelope& reply : taken.replies) {
                replies.push_back(std::move(reply));
            }
        }
        trade_inbox = std::move(kept);
        for (TradeClient::Notice& notice : client.TakeNotices()) {
            ++notices_seen[notice.kind];
            notices_to_show.push_back(std::move(notice));
            if (notices_to_show.size() > 16) {
                notices_to_show.pop_front(); // nobody shows them (no port menus)
            }
        }
    }
    // Added before the acks go: a crash in between hands the grant again (once more at worst).
    for (const TradeItems& items : add) {
        AddTradeItems(items);
    }
    for (bbcoop::Envelope& reply : replies) {
        QueueServerWorld(std::move(reply));
    }
    if (const auto tick = trade_ui_tick.load(); tick != nullptr && TradesOn()) {
        tick();
    }
}

void SetTradeUiTick(void (*tick)()) {
    trade_ui_tick.store(tick);
}

std::string TradeAskQuestion(const bbcoop::TradeAsk& ask) {
    const std::string from = ask.from_name().empty() ? TradeName(ask.from()) : ask.from_name();
    return TradeAskText(from, TradeItemsOf(ask.give()), TradeItemsOf(ask.want()));
}

} // namespace Coop

} // namespace Core::Bloodborne
