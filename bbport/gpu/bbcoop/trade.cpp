// SPDX-License-Identifier: GPL-2.0-or-later
// bbport co-op: trading's C side for the port's trade menus (bbcoop.h, bbcoop_trade_*), on top of
// the co-op code's bloodborne_trade.h.
#include <algorithm>
#include <cstring>
#include <string>

#include "bbcoop.h"
#include "core/bloodborne_trade.h"

namespace {

using namespace Core::Bloodborne::Coop;
using BloodborneCoop::TradeItemKind;

int Copy(const std::string& text, char* out, size_t size) {
    if (out == nullptr || size == 0) {
        return 0;
    }
    const size_t length = std::min(text.size(), size - 1);
    std::memcpy(out, text.data(), length);
    out[length] = '\0';
    return 1;
}

TradeItem ItemOf(uint32_t kind, uint32_t id, uint32_t count = 0) {
    return {static_cast<TradeItemKind>(kind), id, count};
}

} // namespace

extern "C" {

int bbcoop_trade_on(void) {
    return TradesOn() ? 1 : 0;
}

int bbcoop_trade_tradeable(uint32_t kind, uint32_t id) {
    return BloodborneCoop::FindTradeGood(static_cast<TradeItemKind>(kind), id) != nullptr ? 1 : 0;
}

uint32_t bbcoop_trade_held(uint32_t kind, uint32_t id) {
    return TradeHeld(ItemOf(kind, id));
}

int bbcoop_trade_give_question(uint32_t kind, uint32_t id, char* text, size_t size) {
    const std::vector<std::string> partners = TradePartners();
    if (partners.empty()) {
        return 0;
    }
    return Copy(BloodborneCoop::TradeGiveQuestion(ItemOf(kind, id), TradeName(partners.front())),
                text, size);
}

int bbcoop_trade_offer(uint32_t kind, uint32_t id, uint32_t count) {
    const std::vector<std::string> partners = TradePartners();
    if (partners.empty() || count == 0) {
        return 0;
    }
    return OfferTrade(partners.front(), {ItemOf(kind, id, count)}, {}) == TradeOfferResult::Sent
               ? 1
               : 0;
}

int bbcoop_trade_ask(uint64_t* id, char* text, size_t size) {
    const auto ask = FrontTradeAsk();
    if (!ask) {
        return 0;
    }
    *id = ask->id();
    return Copy(TradeAskQuestion(*ask), text, size);
}

int bbcoop_trade_ask_open(uint64_t id) {
    const auto ask = FrontTradeAsk();
    return ask && ask->id() == id ? 1 : 0;
}

int bbcoop_trade_answer(uint64_t id, int accept) {
    return AnswerTrade(id, accept != 0) ? 1 : 0;
}

int bbcoop_trade_notice(char* text, size_t size) {
    while (const auto notice = NextTradeNotice()) {
        const std::string line = BloodborneCoop::TradeNoticeText(*notice);
        if (!line.empty()) {
            return Copy(line, text, size);
        }
    }
    return 0;
}

void bbcoop_trade_set_ui_tick(void (*tick)(void)) {
    SetTradeUiTick(tick);
}

} // extern "C"
