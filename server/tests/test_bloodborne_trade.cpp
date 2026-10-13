// SPDX-FileCopyrightText: Copyright 2026 shadNet Project
// SPDX-License-Identifier: GPL-2.0-or-later
// Trading between party members (coop/trade.h): the server's desk and the
// games' clients, wired together as shadNet and the games run them, with fake
// inventories that must never gain or lose anything a trade did not move.
#include <algorithm>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "coop/trade.h"

using namespace BloodborneCoop;

namespace {

bool Check(bool condition, const char *expression, int line) {
  if (!condition) {
    std::cerr << "check failed at line " << line << ": " << expression << '\n';
  }
  return condition;
}

#define CHECK(expression)                                                      \
  do {                                                                         \
    if (!Check((expression), #expression, __LINE__))                           \
      return 1;                                                                \
  } while (false)

TradeItem Echoes(u32 count) { return {TradeItemKind::Echoes, 0, count}; }
TradeItem Vials(u32 count) {
  return {TradeItemKind::Goods, BloodVialGoods, count};
}
TradeItem Bullets(u32 count) {
  return {TradeItemKind::Goods, QuicksilverBulletGoods, count};
}

// A game: its client and its inventory (what it holds of each item).
struct Game {
  std::string member;
  std::string name;
  TradeClient client;
  std::map<std::pair<u32, u32>, u32> held;
  std::vector<TradeClient::Notice> seen; // the notices shown so far

  u32 Held(const TradeItem &item) const {
    const auto found = held.find({static_cast<u32>(item.kind), item.id});
    return found == held.end() ? 0 : found->second;
  }
  bool Has(const TradeItems &items) const {
    return std::ranges::all_of(
        items, [&](const TradeItem &item) { return Held(item) >= item.count; });
  }
  void Take(const TradeItems &items) {
    for (const TradeItem &item : items) {
      held[{static_cast<u32>(item.kind), item.id}] -= item.count;
    }
  }
  void Add(const TradeItems &items) {
    for (const TradeItem &item : items) {
      held[{static_cast<u32>(item.kind), item.id}] += item.count;
    }
  }
};

// The desk and the games, messages delivered at once (the stream's job).
struct Party {
  TradeDesk desk;
  std::map<std::string, Game *> games;
  std::set<std::string> online;
  s64 now_ms = 1'000;
  int changes = 0;

  void Add(Game &game) {
    games[game.member] = &game;
    online.insert(game.member);
  }
  void Deliver(TradeDesk::Result result) {
    changes += result.changed ? 1 : 0;
    for (const TradeDesk::Send &send : result.sends) {
      const auto found = games.find(send.to);
      if (found == games.end() || !online.contains(send.to)) {
        continue; // its game is gone: the desk keeps what it owes
      }
      Game &game = *found->second;
      TradeClient::Taken taken = game.client.Take(send.envelope);
      for (const TradeItems &items : taken.add) {
        game.Add(items);
      }
      for (const bbcoop::Envelope &reply : taken.replies) {
        FromGame(game, reply);
      }
    }
  }
  void FromGame(Game &game, const bbcoop::Envelope &envelope) {
    if (envelope.has_trade_offer()) {
      Deliver(desk.Offer(game.member, envelope.trade_offer(), online, now_ms));
    } else if (envelope.has_trade_answer()) {
      Deliver(desk.Answer(game.member, envelope.trade_answer()));
    } else if (envelope.has_trade_cancel()) {
      Deliver(desk.Cancel(game.member, envelope.trade_cancel().id()));
    } else if (envelope.has_trade_granted()) {
      Deliver(desk.Granted(game.member, envelope.trade_granted().id()));
    }
  }
  // The giver's side: take the items out, then offer.
  void Offer(Game &from, Game &to, TradeItems give, TradeItems want = {}) {
    from.Take(give);
    FromGame(from,
             from.client.Offer(to.member, to.name, from.name, give, want));
  }
  // The other's side: answer its oldest ask, taking out what was asked of it.
  void Answer(Game &game, bool accept) {
    const bbcoop::TradeAsk ask = game.client.Asked().front();
    TradeItems taken;
    if (accept) {
      taken = TradeItemsOf(ask.want());
      game.Take(taken);
    }
    FromGame(game, game.client.Answer(ask.id(), accept, taken));
  }
};

bool HasNotice(Game &game, TradeClient::Notice::Kind kind) {
  for (TradeClient::Notice &notice : game.client.TakeNotices()) {
    game.seen.push_back(std::move(notice));
  }
  return std::ranges::any_of(game.seen, [&](const TradeClient::Notice &notice) {
    return notice.kind == kind;
  });
}

int TestItems() {
  CHECK(FindTradeGood(TradeItemKind::Echoes, 0) != nullptr);
  CHECK(FindTradeGood(TradeItemKind::Goods, BloodVialGoods) != nullptr);
  CHECK(FindTradeGood(TradeItemKind::Goods, QuicksilverBulletGoods) != nullptr);
  CHECK(FindTradeGood(TradeItemKind::Goods, 4242) == nullptr);
  CHECK(FindTradeGood(TradeItemKind::Echoes, BloodVialGoods) == nullptr);

  const TradeItems fine{Vials(5), Echoes(1000)};
  CHECK(!TradeItemsProblem(fine, false).has_value());
  CHECK(TradeItemsProblem(TradeItems{}, false).has_value()); // nothing offered
  CHECK(
      !TradeItemsProblem(TradeItems{}, true).has_value()); // nothing asked for
  CHECK(TradeItemsProblem(TradeItems{Vials(0)}, false).has_value());
  CHECK(TradeItemsProblem(TradeItems{Vials(MaxTradeGoods + 1)}, false)
            .has_value());
  CHECK(!TradeItemsProblem(TradeItems{Echoes(MaxTradeEchoes)}, false)
             .has_value());
  CHECK(TradeItemsProblem(TradeItems{Echoes(MaxTradeEchoes + 1)}, false)
            .has_value());
  CHECK(TradeItemsProblem(TradeItems{Vials(1), Vials(2)}, false).has_value());
  CHECK(TradeItemsProblem(TradeItems{{TradeItemKind::Goods, 4242, 1}}, false)
            .has_value());

  CHECK(TradeItemsText(TradeItems{Vials(1)}) == "1 Blood Vial");
  CHECK(TradeItemsText(TradeItems{Vials(5), Echoes(12345)}) ==
        "5 Blood Vials, 12,345 Blood Echoes");
  CHECK(TradeItemsText(TradeItems{Bullets(20)}) == "20 Quicksilver Bullets");

  // Round trip through the messages.
  google::protobuf::RepeatedPtrField<bbcoop::TradeItem> out;
  PutTradeItems(fine, &out);
  CHECK(TradeItemsOf(out) == fine);
  return 0;
}

int TestGiveAccepted() {
  Party party;
  Game ada{"a1", "Ada"}, bo{"b2", "Bo"};
  ada.held[{2, BloodVialGoods}] = 10;
  party.Add(ada);
  party.Add(bo);

  party.Offer(ada, bo, {Vials(4)});
  CHECK(ada.Held(Vials(0)) == 6); // taken out at once
  CHECK(ada.client.OpenOffers() == 1);
  CHECK(bo.client.Asked().size() == 1);
  CHECK(bo.client.Asked().front().from_name() == "Ada");
  CHECK(TradeItemsOf(bo.client.Asked().front().give()) == TradeItems{Vials(4)});
  CHECK(party.desk.OpenTrades() == 1);

  party.Answer(bo, true);
  CHECK(bo.Held(Vials(0)) == 4);
  CHECK(ada.Held(Vials(0)) == 6);
  CHECK(bo.client.Asked().empty());
  CHECK(ada.client.OpenOffers() == 0);
  CHECK(party.desk.OpenTrades() == 0);
  CHECK(party.desk.PendingGrants() == 0); // added and acked
  CHECK(HasNotice(ada, TradeClient::Notice::Kind::Accepted));
  CHECK(HasNotice(bo, TradeClient::Notice::Kind::Received));
  CHECK(party.changes > 0);
  return 0;
}

int TestSwap() {
  Party party;
  Game ada{"a1", "Ada"}, bo{"b2", "Bo"};
  ada.held[{1, 0}] = 5000;
  bo.held[{2, QuicksilverBulletGoods}] = 20;
  party.Add(ada);
  party.Add(bo);

  party.Offer(ada, bo, {Echoes(3000)}, {Bullets(10)});
  party.Answer(bo, true);
  CHECK(ada.Held(Echoes(0)) == 2000);
  CHECK(ada.Held(Bullets(0)) == 10);
  CHECK(bo.Held(Echoes(0)) == 3000);
  CHECK(bo.Held(Bullets(0)) == 10);
  CHECK(party.desk.PendingGrants() == 0);
  return 0;
}

int TestDeclinedAndExpired() {
  Party party;
  Game ada{"a1", "Ada"}, bo{"b2", "Bo"};
  ada.held[{2, BloodVialGoods}] = 10;
  party.Add(ada);
  party.Add(bo);

  party.Offer(ada, bo, {Vials(3)});
  party.Answer(bo, false);
  CHECK(ada.Held(Vials(0)) == 10); // back
  CHECK(bo.Held(Vials(0)) == 0);
  CHECK(HasNotice(ada, TradeClient::Notice::Kind::Declined));
  CHECK(HasNotice(ada, TradeClient::Notice::Kind::Returned));

  party.Offer(ada, bo, {Vials(2)});
  party.Deliver(party.desk.Expire(party.now_ms + TradeExpiryMs - 1));
  CHECK(bo.client.Asked().size() == 1); // not yet
  party.Deliver(party.desk.Expire(party.now_ms + TradeExpiryMs));
  CHECK(bo.client.Asked().empty()); // closed for the other
  CHECK(ada.Held(Vials(0)) == 10);
  CHECK(HasNotice(ada, TradeClient::Notice::Kind::Expired));
  CHECK(party.desk.OpenTrades() == 0);
  return 0;
}

int TestLateAcceptGivesBack() {
  // The other member accepts as the offer expires: what it took to accept
  // comes back, the giver gets its own back.
  Party party;
  Game ada{"a1", "Ada"}, bo{"b2", "Bo"};
  ada.held[{2, BloodVialGoods}] = 5;
  bo.held[{1, 0}] = 800;
  party.Add(ada);
  party.Add(bo);

  party.Offer(ada, bo, {Vials(5)}, {Echoes(500)});
  const bbcoop::TradeAsk ask = bo.client.Asked().front();
  party.Deliver(party.desk.Expire(party.now_ms + TradeExpiryMs));
  bo.Take({Echoes(500)});
  party.FromGame(bo, bo.client.Answer(ask.id(), true, {Echoes(500)}));
  CHECK(bo.Held(Echoes(0)) == 800);
  CHECK(bo.Held(Vials(0)) == 0);
  CHECK(ada.Held(Vials(0)) == 5);
  CHECK(ada.Held(Echoes(0)) == 0);
  CHECK(party.desk.PendingGrants() == 0);
  return 0;
}

int TestCancel() {
  Party party;
  Game ada{"a1", "Ada"}, bo{"b2", "Bo"};
  ada.held[{2, QuicksilverBulletGoods}] = 20;
  party.Add(ada);
  party.Add(bo);

  party.Offer(ada, bo, {Bullets(20)});
  const auto cancel = ada.client.Cancel(1);
  CHECK(cancel.has_value());
  party.FromGame(ada, *cancel);
  CHECK(ada.Held(Bullets(0)) == 20);
  CHECK(bo.client.Asked().empty());
  CHECK(!ada.client.Cancel(1).has_value()); // over
  // Only the giver may withdraw it.
  party.Offer(ada, bo, {Bullets(5)});
  const u64 id = bo.client.Asked().front().id();
  party.Deliver(party.desk.Cancel("b2", id));
  CHECK(bo.client.Asked().size() == 1);
  return 0;
}

int TestRefused() {
  Party party;
  Game ada{"a1", "Ada"}, bo{"b2", "Bo"};
  ada.held[{2, BloodVialGoods}] = 10;
  ada.held[{2, 4242}] = 1;
  party.Add(ada);
  party.Add(bo);
  party.online.erase("b2");

  // The other member is not online: straight back.
  party.Offer(ada, bo, {Vials(2)});
  CHECK(ada.Held(Vials(0)) == 10);
  CHECK(HasNotice(ada, TradeClient::Notice::Kind::Refused));
  party.online.insert("b2");

  // Not a tradeable item: back too.
  party.Offer(ada, bo, {{TradeItemKind::Goods, 4242, 1}});
  CHECK(ada.Held({TradeItemKind::Goods, 4242, 0}) == 1);
  CHECK(bo.client.Asked().empty());
  // Nor to itself.
  ada.Take({Vials(1)});
  party.FromGame(ada, ada.client.Offer("a1", "Ada", "Ada", {Vials(1)}, {}));
  CHECK(ada.Held(Vials(0)) == 10);

  // At most MaxOpenTradesPerMember open at once.
  for (size_t index = 0; index < MaxOpenTradesPerMember + 1; ++index) {
    party.Offer(ada, bo, {Vials(1)});
  }
  CHECK(party.desk.OpenTrades() == MaxOpenTradesPerMember);
  CHECK(ada.Held(Vials(0)) == 10 - MaxOpenTradesPerMember);
  return 0;
}

int TestGrantKeptForAGameGone() {
  // The giver's game is gone when the other accepts: its part waits on the
  // server and comes with its next hello, once.
  Party party;
  Game ada{"a1", "Ada"}, bo{"b2", "Bo"};
  ada.held[{2, BloodVialGoods}] = 4;
  bo.held[{2, QuicksilverBulletGoods}] = 8;
  party.Add(ada);
  party.Add(bo);

  party.Offer(ada, bo, {Vials(4)}, {Bullets(8)});
  party.online.erase("a1");
  party.Answer(bo, true);
  CHECK(bo.Held(Vials(0)) == 4);
  CHECK(ada.Held(Bullets(0)) == 0);
  CHECK(party.desk.PendingGrants() == 1);

  // Kept on disk across a restart of the server.
  TradeDesk restarted;
  restarted.Restore(party.desk.Stored(), party.now_ms);
  CHECK(restarted.PendingGrants() == 1);
  party.desk = restarted;

  Game ada_again{"a1", "Ada"}; // a new run: a new client
  ada_again.held = ada.held;
  party.games["a1"] = &ada_again;
  party.online.insert("a1");
  party.Deliver(party.desk.Resume("a1", party.now_ms));
  CHECK(ada_again.Held(Bullets(0)) == 8);
  CHECK(party.desk.PendingGrants() == 0);
  party.Deliver(party.desk.Resume("a1", party.now_ms));
  CHECK(ada_again.Held(Bullets(0)) == 8);
  return 0;
}

int TestAskResumedAndRestored() {
  // The other member's game restarts while asked: the ask comes again; the
  // server restarts: open offers stay open with a fresh time.
  Party party;
  Game ada{"a1", "Ada"}, bo{"b2", "Bo"};
  ada.held[{2, BloodVialGoods}] = 3;
  party.Add(ada);
  party.Add(bo);
  party.Offer(ada, bo, {Vials(3)});

  Game bo_again{"b2", "Bo"};
  party.games["b2"] = &bo_again;
  party.Deliver(party.desk.Resume("b2", party.now_ms));
  CHECK(bo_again.client.Asked().size() == 1);

  TradeDesk restarted;
  restarted.Restore(party.desk.Stored(), party.now_ms + 50'000);
  party.desk = restarted;
  party.Deliver(party.desk.Expire(party.now_ms + 50'000 + TradeExpiryMs - 1));
  CHECK(party.desk.OpenTrades() == 1);
  party.Answer(bo_again, true);
  CHECK(bo_again.Held(Vials(0)) == 3);
  CHECK(ada.Held(Vials(0)) == 0);
  return 0;
}

int TestGrantOnce() {
  // A grant sent twice (a resume racing its ack) is added once.
  TradeClient client;
  bbcoop::Envelope envelope;
  bbcoop::TradeGrant &grant = *envelope.mutable_trade_grant();
  grant.set_id(7);
  grant.set_from_name("Ada");
  PutTradeItems(TradeItems{Vials(2)}, grant.mutable_items());
  const TradeClient::Taken first = client.Take(envelope);
  const TradeClient::Taken second = client.Take(envelope);
  CHECK(first.add.size() == 1);
  CHECK(second.add.empty());
  CHECK(first.replies.size() == 1 && second.replies.size() == 1); // acked again
  CHECK(second.replies.front().trade_granted().id() == 7);
  return 0;
}

int TestTexts() {
  using Kind = TradeClient::Notice::Kind;
  CHECK(TradeAskText("Kaniel", TradeItems{Vials(4)}, {}) ==
        "Kaniel offers you 4 Blood Vials.\nAccept?");
  CHECK(TradeAskText("Kaniel", TradeItems{Echoes(3000)},
                     TradeItems{Bullets(5)}) ==
        "Kaniel offers you 3,000 Blood Echoes\nfor 5 Quicksilver "
        "Bullets.\nAccept?");
  CHECK(TradeGiveQuestion({TradeItemKind::Goods, BloodVialGoods, 0}, "Ada") ==
        "Select quantity of Blood Vials to give to Ada.");
  CHECK(TradeGiveQuestion(Echoes(0), "Ada") ==
        "Select quantity of Blood Echoes to give to Ada.");
  CHECK(TradeNoticeText({Kind::Accepted, "Ada", {Vials(4)}, {}}) ==
        "Ada accepted your 4 Blood Vials.");
  CHECK(TradeNoticeText({Kind::Declined, "Ada", {Vials(4)}, {}}) ==
        "Ada declined your 4 Blood Vials.\nThey were returned to you.");
  CHECK(TradeNoticeText({Kind::Expired, "Ada", {Vials(1)}, {}}) ==
        "Ada did not answer your offer of 1 Blood Vial.\nIt was returned to "
        "you.");
  CHECK(TradeNoticeText({Kind::Refused,
                         "Ada",
                         {Vials(2)},
                         "the other member is not online"}) ==
        "Could not offer Ada 2 Blood Vials: the other member is not online.\n"
        "They were returned to you.");
  // Goods come with the game's own item popup: no box. Echoes have none.
  CHECK(TradeNoticeText({Kind::Received, "Ada", {Vials(4)}, {}}).empty());
  CHECK(TradeNoticeText({Kind::Received, "Ada", {Vials(4), Echoes(10)}, {}}) ==
        "Received 10 Blood Echoes from Ada.");
  CHECK(TradeNoticeText({Kind::Returned, "Ada", {Echoes(10)}, {}}).empty());
  return 0;
}

} // namespace

int main() {
  int failed = 0;
  failed += TestItems();
  failed += TestGiveAccepted();
  failed += TestSwap();
  failed += TestDeclinedAndExpired();
  failed += TestLateAcceptGivesBack();
  failed += TestCancel();
  failed += TestRefused();
  failed += TestGrantKeptForAGameGone();
  failed += TestAskResumedAndRestored();
  failed += TestGrantOnce();
  failed += TestTexts();
  if (failed != 0) {
    std::cerr << failed << " trade test(s) failed\n";
    return 1;
  }
  std::cout << "trade tests passed\n";
  return 0;
}
