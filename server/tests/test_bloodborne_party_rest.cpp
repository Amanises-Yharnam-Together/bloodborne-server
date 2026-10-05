// SPDX-FileCopyrightText: Copyright 2026 shadNet Project
// SPDX-License-Identifier: GPL-2.0-or-later
// Lamps as campfires: each party member posts which lamp it rests at, and gets
// back the members of its party resting right now. Everyone decides on its own
// when the whole party rests.
#include <iostream>

#include "bloodborne_party_rest.h"

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

constexpr qint32 Lamp = 72410100;
constexpr qint32 OtherLamp = 72410101;
const QByteArray Party = "rig:hunters";

} // namespace

int main() {
  using Bloodborne::PartyRestBoard;

  // The update body is "<member> <lamp>".
  {
    const auto update = Bloodborne::ParseRestUpdate("a1b2c3 72410100");
    CHECK(update.has_value());
    CHECK(update->member == "a1b2c3");
    CHECK(update->lamp == Lamp);
    CHECK(Bloodborne::ParseRestUpdate("a1b2c3 0").has_value());
    CHECK(!Bloodborne::ParseRestUpdate("").has_value());
    CHECK(!Bloodborne::ParseRestUpdate("a1b2c3").has_value());
    CHECK(!Bloodborne::ParseRestUpdate("a1b2c3 lamp").has_value());
    CHECK(!Bloodborne::ParseRestUpdate(" 72410100").has_value());
    CHECK(!Bloodborne::ParseRestUpdate("bad\nname 72410100").has_value());
    CHECK(!Bloodborne::ParseRestUpdate(QByteArray(65, 'a') + " 1").has_value());
  }

  // Members see each other resting, and only their own party.
  {
    PartyRestBoard board;
    auto rests = board.Update(Party, "host", Lamp, 1000);
    CHECK(rests.size() == 1);
    CHECK(rests[0].member == "host" && rests[0].lamp == Lamp);

    board.Update("other:party", "stranger", Lamp, 1000);
    rests = board.Update(Party, "guest", Lamp, 1500);
    CHECK(rests.size() == 2);
    CHECK(Bloodborne::FormatRestBoard(rests) ==
          "guest 72410100\nhost 72410100\n");
  }

  // Standing up (lamp 0) and going quiet both take a member off the board.
  {
    PartyRestBoard board;
    board.Update(Party, "host", Lamp, 0);
    board.Update(Party, "guest", OtherLamp, 0);
    auto rests = board.Update(Party, "guest", 0, 1000);
    CHECK(rests.size() == 1 && rests[0].member == "host");

    rests =
        board.Update(Party, "guest", OtherLamp, PartyRestBoard::FreshMs + 1);
    CHECK(rests.size() == 1 && rests[0].member == "guest");
  }

  // A rest can carry where the member rests ("<member> <lamp> <x> <y> <z>"),
  // which the board hands back so the others can join it by standing close.
  {
    const auto update = Bloodborne::ParseRestUpdate("host 72410100 10 -2.5 5");
    CHECK(update.has_value());
    CHECK(update->lamp == Lamp);
    CHECK(update->at == "10 -2.5 5");
    CHECK(!Bloodborne::ParseRestUpdate("host 72410100 10 -2.5").has_value());
    CHECK(!Bloodborne::ParseRestUpdate("host 72410100 10 x 5").has_value());

    PartyRestBoard board;
    const auto rests = board.Update(Party, "host", Lamp, 0, update->at);
    CHECK(Bloodborne::FormatRestBoard(rests) ==
          "host 72410100 10 -2.5 5" + QByteArray(1, 10));
  }

  // No party key: nothing to coordinate.
  {
    PartyRestBoard board;
    CHECK(board.Update({}, "host", Lamp, 0).isEmpty());
    CHECK(board.Update({}, "guest", Lamp, 0).isEmpty());
  }

  std::cout << "bloodborne party rest: ok\n";
  return 0;
}
