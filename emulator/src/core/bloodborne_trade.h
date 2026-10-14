// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

// Trading between party members, the game's side (bloodborne_trade.cpp; the protocol is
// server/coop/trade.h). The co-op runtime's files only.
#pragma once

#include <optional>
#include <span>
#include <string>
#include <vector>

#include "coop/trade.h"

namespace Core::Bloodborne::Coop {

using BloodborneCoop::TradeClient;
using BloodborneCoop::TradeItem;
using BloodborneCoop::TradeItems;

// The port with the server world; on once shadNet said it keeps trades too.
bool TradeSwitchOn();
bool TradesOn();
// The game's functions trading calls are the ones it knows (InstallTrading).
void EnableTrading();
// From the server world's stream thread.
void TakeServerTradeWelcome(bool trades);
void TakeServerTrade(bbcoop::Envelope envelope);
// Queues a message on the server world's stream (bloodborne_server_world.cpp).
void QueueServerWorld(bbcoop::Envelope envelope);

// The player's items (the game's thread).
u32 TradeHeld(const TradeItem& item);
bool TakeTradeItems(std::span<const TradeItem> items);
void AddTradeItems(std::span<const TradeItem> items);

// The other members a trade may go to now (board names), and how a member is called.
std::vector<std::string> TradePartners();
std::string TradeName(const std::string& member);

enum class TradeOfferResult { Sent, Off, Bad, NotEnough };
TradeOfferResult OfferTrade(const std::string& to, TradeItems give, TradeItems want);
std::optional<bbcoop::TradeAsk> FrontTradeAsk();
// Accepting takes what was asked out first; false when it was a no (or could not be given).
bool AnswerTrade(u64 id, bool accept);
u32 TradeNoticesSeen(TradeClient::Notice::Kind kind);
std::optional<TradeClient::Notice> NextTradeNotice();
// Every party tick on the game's thread; it runs the port's trade menus' tick too (SetTradeUiTick).
void TickTrades();
void SetTradeUiTick(void (*tick)());
// The question an offer to this player asks (TradeAskText with the giver's name).
std::string TradeAskQuestion(const bbcoop::TradeAsk& ask);

} // namespace Core::Bloodborne::Coop
