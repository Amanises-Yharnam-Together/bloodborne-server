// SPDX-FileCopyrightText: Copyright 2026 shadNet Project
// SPDX-License-Identifier: GPL-2.0-or-later
// Phase 11, one port: a game's TCP calls through the server link's Rpc stream
// (coop/rpc_tunnel.h).
#include <iostream>
#include <string>
#include <vector>

#include "coop/rpc_tunnel.h"

namespace {

using BloodborneCoop::RpcTunnel;
using Event = RpcTunnel::Event;

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

std::vector<u8> Bytes(const std::string &text) {
  return {text.begin(), text.end()};
}

// Everything `from` has to send, delivered to `to`: the events there.
std::vector<Event> Carry(RpcTunnel &from, RpcTunnel &to, s64 now_ms) {
  std::vector<Event> events;
  for (const auto &body : from.Flush(now_ms)) {
    for (Event &event : to.Receive(body, now_ms)) {
      events.push_back(std::move(event));
    }
  }
  // The acks back.
  for (const auto &body : to.Flush(now_ms)) {
    from.Receive(body, now_ms);
  }
  return events;
}

int AConnectionGoesThrough() {
  RpcTunnel game{"game", 7};
  RpcTunnel server{"shadnet", 9};
  game.Open(0);
  game.Write(Bytes("login"), 0);
  std::vector<Event> events = Carry(game, server, 0);
  CHECK(events.size() == 2);
  CHECK(events[0].kind == Event::Kind::Opened);
  CHECK(events[1] == (Event{Event::Kind::Data, Bytes("login")}));
  CHECK(server.IsOpen() && game.IsOpen());

  // A large answer comes whole and in order.
  std::string reply(5000, 'r');
  reply[4999] = 'z';
  server.Write(Bytes(reply), 10);
  events = Carry(server, game, 10);
  std::string got;
  for (const Event &event : events) {
    CHECK(event.kind == Event::Kind::Data);
    got.append(event.bytes.begin(), event.bytes.end());
  }
  CHECK(got == reply);
  CHECK(game.Unacked() == 0 && server.Unacked() == 0);

  // Nothing to say: Write of nothing sends nothing.
  server.Write({}, 20);
  CHECK(Carry(server, game, 20).empty());

  game.Close(30);
  events = Carry(game, server, 30);
  CHECK(events.size() == 1 && events[0].kind == Event::Kind::Closed);
  CHECK(!server.IsOpen() && !game.IsOpen());
  std::cout << "rpc tunnel: a connection goes through\n";
  return 0;
}

int ALostDatagramIsSentAgain() {
  RpcTunnel game{"game", 7};
  RpcTunnel server{"shadnet", 9};
  game.Open(0);
  game.Write(Bytes("hello"), 0);
  (void)game.Flush(0); // lost on the way
  CHECK(Carry(game, server, 50).empty());
  const std::vector<Event> events = Carry(game, server, 400);
  CHECK(events.size() == 2 && events[1].bytes == Bytes("hello"));
  std::cout << "rpc tunnel: a lost datagram is sent again\n";
  return 0;
}

// The server restarted (a new instance): the game's connection through it is
// over, and the game's next one starts on the new server.
int ARestartedServerEndsTheConnection() {
  RpcTunnel game{"game", 7};
  RpcTunnel server{"shadnet", 9};
  game.Open(0);
  Carry(game, server, 0);
  server.Write(Bytes("hi"), 5);
  Carry(server, game, 5);
  CHECK(game.IsOpen());

  RpcTunnel restarted{"shadnet", 10};
  restarted.Write({},
                  100); // nothing; its keepalive is what the game hears first
  std::vector<Event> events;
  for (s64 now = 1100; now < 3000 && events.empty(); now += 100) {
    events = Carry(restarted, game, now);
  }
  CHECK(events.size() == 1 && events[0].kind == Event::Kind::Closed);
  CHECK(!game.IsOpen());

  game.Open(4000);
  game.Write(Bytes("login"), 4000);
  events = Carry(game, restarted, 4000);
  CHECK(events.size() == 2 && events[0].kind == Event::Kind::Opened);
  std::cout << "rpc tunnel: a restarted server ends the connection\n";
  return 0;
}

} // namespace

int main() {
  if (AConnectionGoesThrough() != 0 || ALostDatagramIsSentAgain() != 0 ||
      ARestartedServerEndsTheConnection() != 0) {
    return 1;
  }
  return 0;
}
