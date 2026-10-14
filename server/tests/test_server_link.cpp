// SPDX-FileCopyrightText: Copyright 2026 shadNet Project
// SPDX-License-Identifier: GPL-2.0-or-later
// The server link's datagrams as the emulator writes them
// (tests/contracts/server_link_frames.txt, which the emulator's tests read
// too), and the hub that forwards the games' frames between them.
#include <fstream>
#include <iostream>
#include <map>
#include <string>

#include "server_link.h"

using namespace ServerLink;

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

std::map<std::string, QByteArray> ReadContracts(const char *path) {
  std::map<std::string, QByteArray> datagrams;
  std::ifstream file(path);
  std::string line;
  while (std::getline(file, line)) {
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    if (line.empty() || line[0] == '#') {
      continue;
    }
    const auto tab = line.find('\t');
    if (tab == std::string::npos) {
      continue;
    }
    datagrams[line.substr(0, tab)] =
        QByteArray::fromHex(QByteArray::fromStdString(line.substr(tab + 1)));
  }
  return datagrams;
}

const Endpoint Ada127{0x7F000001, 0x1234};
const QByteArray GameFrame = QByteArray::fromHex("75307531070000000200000"
                                                 "0abcd");
const QByteArray Build = "92829ac4 events 1f protocol 9";

const Endpoint Kaniel{0x7F000001, 40001};
const Endpoint Ada{0x7F000001, 40002};
const Endpoint Eileen{0xC0A80105, 40003};

QByteArray HelloFrom(const char *who, const QByteArray &build = Build) {
  return Encode(Hello{LinkVersion, who, build});
}

// The sends of `result` to `to`.
QList<QByteArray> SentTo(const Hub::Result &result, const Endpoint &to) {
  QList<QByteArray> sent;
  for (const auto &send : result.sends) {
    if (send.to == to) {
      sent.append(send.datagram);
    }
  }
  return sent;
}

int Contracts() {
  const auto datagrams = ReadContracts(BLOODBORNE_SERVER_LINK_CONTRACTS);
  CHECK(datagrams.size() == 9);
  const auto datagram = [&](const char *name) { return datagrams.at(name); };

  CHECK(Encode(Hello{LinkVersion, "Kaniel", Build}) == datagram("hello"));
  const auto hello = DecodeHello(datagram("hello"));
  CHECK(hello && hello->version == 1 && hello->online_id == "Kaniel" &&
        hello->build == Build);

  CHECK(Encode(Welcome{true, {}}) == datagram("welcome.ok"));
  CHECK(Encode(Welcome{false, "another build: 13ddd14d"}) ==
        datagram("welcome.refused"));
  const auto refused = DecodeWelcome(datagram("welcome.refused"));
  CHECK(refused && !refused->accepted &&
        refused->text == "another build: 13ddd14d");

  CHECK(Encode(Data{Ada127, GameFrame}) == datagram("data"));
  const auto data = DecodeData(datagram("data"));
  CHECK(data && data->peer == Ada127 && data->frame == GameFrame);

  CHECK(Encode(Kind::Ping, Ping{7, 1000}) == datagram("ping"));
  CHECK(Encode(Kind::Pong, Ping{7, 1000}) == datagram("pong"));
  const auto ping = DecodePing(Kind::Ping, datagram("ping"));
  CHECK(ping && ping->seq == 7 && ping->sent_ms == 1000);
  CHECK(!DecodePing(Kind::Ping, datagram("pong")));

  CHECK(Encode(Member{MemberState::Quiet, 3500, Ada127, "Ada"}) ==
        datagram("member.quiet"));
  CHECK(Encode(Member{MemberState::Back, 4200, Ada127, "Ada"}) ==
        datagram("member.back"));
  CHECK(Encode(Member{MemberState::Gone, 60000, Ada127, "Ada"}) ==
        datagram("member.gone"));
  const auto gone = DecodeMember(datagram("member.gone"));
  CHECK(gone && gone->state == MemberState::Gone && gone->gap_ms == 60000 &&
        gone->endpoint == Ada127 && gone->online_id == "Ada");

  for (const auto &[name, bytes] : datagrams) {
    CHECK(KindOf(bytes).has_value());
    for (int size = 0; size < bytes.size(); ++size) {
      const QByteArray cut = bytes.left(size);
      if (name == "data" && size >= 15) {
        continue; // a shorter frame is still a frame
      }
      CHECK(!DecodeHello(cut) && !DecodeWelcome(cut) && !DecodeData(cut) &&
            !DecodePing(Kind::Ping, cut) && !DecodePing(Kind::Pong, cut) &&
            !DecodeMember(cut));
    }
  }
  // The STUN ping shares the header.
  CHECK(!KindOf(QByteArray::fromHex("ffffffff014b616e69656c")));
  std::cout << "server link contracts: ok\n";
  return 0;
}

int HelloAndBuilds() {
  Hub hub;
  auto result = hub.Receive(Kaniel, HelloFrom("Kaniel"), 0);
  CHECK(SentTo(result, Kaniel).size() == 1);
  CHECK(DecodeWelcome(SentTo(result, Kaniel)[0])->accepted);
  CHECK(hub.Members() == 1 && hub.Build() == Build);

  result = hub.Receive(Ada, HelloFrom("Ada"), 10);
  CHECK(DecodeWelcome(SentTo(result, Ada).value(0))->accepted);
  CHECK(hub.Members() == 2);

  // One build for everyone: the 0x60 record carries a raw vtable pointer.
  result = hub.Receive(Eileen, HelloFrom("Eileen", "13ddd14d"), 20);
  const auto refused = DecodeWelcome(SentTo(result, Eileen).value(0));
  CHECK(refused && !refused->accepted);
  CHECK(refused->text.contains("13ddd14d") && refused->text.contains(Build));
  CHECK(hub.Members() == 2);

  Hello later{2, "Eileen", Build};
  result = hub.Receive(Eileen, Encode(later), 30);
  CHECK(!DecodeWelcome(SentTo(result, Eileen).value(0))->accepted);

  // A hello again (the game restarted on the same port) is welcomed again.
  result = hub.Receive(Kaniel, HelloFrom("Kaniel"), 40);
  CHECK(DecodeWelcome(SentTo(result, Kaniel).value(0))->accepted);
  CHECK(hub.Members() == 2);
  std::cout << "server link hello: ok\n";
  return 0;
}

int Relays() {
  Hub hub;
  hub.Receive(Kaniel, HelloFrom("Kaniel"), 0);
  hub.Receive(Ada, HelloFrom("Ada"), 0);

  auto result = hub.Receive(Kaniel, Encode(Data{Ada, GameFrame}), 100);
  const auto forwarded = SentTo(result, Ada);
  CHECK(forwarded.size() == 1 && result.sends.size() == 1);
  const auto data = DecodeData(forwarded[0]);
  CHECK(data && data->peer == Kaniel && data->frame == GameFrame);
  CHECK(!result.log.isEmpty() && result.log[0].contains("Kaniel") &&
        result.log[0].contains("Ada"));

  // Nobody at the destination: dropped, and said.
  result = hub.Receive(Kaniel, Encode(Data{Eileen, GameFrame}), 110);
  CHECK(result.sends.isEmpty());
  CHECK(!result.log.isEmpty() && result.log[0].contains("no game"));

  // A game that never said hello is not relayed.
  result = hub.Receive(Eileen, Encode(Data{Kaniel, GameFrame}), 120);
  CHECK(result.sends.isEmpty());

  // Only the first frames of a pair are logged.
  Hub quiet_log(3000, 60000, 10000, 2);
  quiet_log.Receive(Kaniel, HelloFrom("Kaniel"), 0);
  quiet_log.Receive(Ada, HelloFrom("Ada"), 0);
  int lines = 0;
  for (int index = 0; index < 5; ++index) {
    lines += static_cast<int>(
        quiet_log.Receive(Kaniel, Encode(Data{Ada, GameFrame}), index)
            .log.size());
  }
  CHECK(lines == 2);

  // A ping comes back as a pong with the same body.
  result = hub.Receive(Ada, Encode(Kind::Ping, Ping{3, 777}), 130);
  const auto pong = DecodePing(Kind::Pong, SentTo(result, Ada).value(0));
  CHECK(pong && pong->seq == 3 && pong->sent_ms == 777);
  std::cout << "server link relay: ok\n";
  return 0;
}

int Liveness() {
  Hub hub;
  hub.Receive(Kaniel, HelloFrom("Kaniel"), 0);
  hub.Receive(Ada, HelloFrom("Ada"), 0);
  hub.Receive(Kaniel, Encode(Kind::Ping, Ping{1, 1000}), 1000);

  auto result = hub.Tick(3000);
  CHECK(result.sends.isEmpty());

  // Ada's link quiet for longer than 3 s: Kaniel is told, once.
  hub.Receive(Kaniel, Encode(Kind::Ping, Ping{2, 3000}), 3000);
  result = hub.Tick(3500);
  const auto told = SentTo(result, Kaniel);
  CHECK(told.size() == 1 && SentTo(result, Ada).isEmpty());
  const auto quiet = DecodeMember(told[0]);
  CHECK(quiet && quiet->state == MemberState::Quiet && quiet->endpoint == Ada &&
        quiet->online_id == "Ada" && quiet->gap_ms == 3500);
  CHECK(!result.log.isEmpty() && result.log[0].contains("Ada"));
  hub.Receive(Kaniel, Encode(Kind::Ping, Ping{3, 4000}), 4000);
  CHECK(SentTo(hub.Tick(4500), Kaniel).isEmpty());

  // Heard again.
  result = hub.Receive(Ada, Encode(Kind::Ping, Ping{1, 6000}), 6000);
  const auto back = DecodeMember(SentTo(result, Kaniel).value(0));
  CHECK(back && back->state == MemberState::Back && back->gap_ms == 6000);

  // Kaniel's game is gone after a minute without a word; the build is free
  // again once nobody is linked.
  hub.Receive(Ada, Encode(Kind::Ping, Ping{2, 64000}), 64000);
  result = hub.Tick(64001);
  CHECK(SentTo(result, Ada).size() == 1); // gone, not quiet first
  const auto gone = DecodeMember(SentTo(result, Ada).value(0));
  CHECK(gone && gone->state == MemberState::Gone &&
        gone->online_id == "Kaniel");
  CHECK(hub.Members() == 1);
  hub.Tick(200000);
  CHECK(hub.Members() == 0 && hub.Build().isEmpty());
  result = hub.Receive(Eileen, HelloFrom("Eileen", "13ddd14d"), 200001);
  CHECK(DecodeWelcome(SentTo(result, Eileen).value(0))->accepted);

  // The counters every 10 s.
  Hub counted;
  counted.Receive(Kaniel, HelloFrom("Kaniel"), 0);
  counted.Receive(Ada, HelloFrom("Ada"), 0);
  counted.Receive(Kaniel, Encode(Data{Ada, GameFrame}), 1);
  counted.Receive(Kaniel, Encode(Kind::Ping, Ping{1, 1}), 9000);
  counted.Receive(Ada, Encode(Kind::Ping, Ping{1, 1}), 9000);
  CHECK(counted.Tick(9999).log.isEmpty());
  const auto counters = counted.Tick(10000).log;
  CHECK(counters.size() == 2);
  CHECK(counters.join('\n').contains("Kaniel") &&
        counters.join('\n').contains("Ada"));
  std::cout << "server link liveness: ok\n";
  return 0;
}

} // namespace

int main() {
  if (Contracts() != 0 || HelloAndBuilds() != 0 || Relays() != 0 ||
      Liveness() != 0) {
    return 1;
  }
  return 0;
}
