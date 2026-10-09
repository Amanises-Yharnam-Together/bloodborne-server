// SPDX-FileCopyrightText: Copyright 2026 shadNet Project
// SPDX-License-Identifier: GPL-2.0-or-later
// The party boards' parsers take whatever a client posts: random bodies must
// never crash them, and the boards must keep working whatever was posted.
#include <iostream>
#include <random>
#include <string>
#include <vector>

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

QByteArray RandomBody(std::mt19937 &random) {
  // Characters the formats are built from, or whole tokens (members, numbers,
  // flag and echo pairs) so a body gets past the member check.
  static const std::string alphabet = "0123456789abcdefAZ -:\n\t.e";
  static const std::vector<std::string> tokens{"guest",
                                               "4b616e69656c",
                                               "0",
                                               "7",
                                               "-5",
                                               "4294967296",
                                               "402718720",
                                               "18010000",
                                               "1:1",
                                               "7116:0",
                                               "7116:2",
                                               "120:",
                                               "120:402718720",
                                               "1:2:3",
                                               "00ff10",
                                               "0g",
                                               "-12.5",
                                               "nan",
                                               "",
                                               ":"};
  std::uniform_int_distribution<int> coin(0, 1);
  std::uniform_int_distribution<int> any(0, 255);
  QByteArray body;
  if (coin(random)) {
    std::uniform_int_distribution<size_t> length(0, 48);
    std::uniform_int_distribution<size_t> pick(0, alphabet.size() - 1);
    for (size_t index = length(random); index > 0; --index) {
      body += (any(random) < 8) ? static_cast<char>(any(random))
                                : alphabet[pick(random)];
    }
    return body;
  }
  std::uniform_int_distribution<size_t> count(1, 6);
  std::uniform_int_distribution<size_t> pick(0, tokens.size() - 1);
  for (size_t token = count(random); token > 0; --token) {
    body += QByteArray::fromStdString(tokens[pick(random)]);
    if (token > 1) {
      body += coin(random) ? " " : "  ";
    }
  }
  return body;
}

} // namespace

int main() {
  std::mt19937 random{2026};
  Bloodborne::PartyFlagBoard flags;
  Bloodborne::PartyEchoBoard echoes;
  Bloodborne::PartyMapStateBoard states;
  Bloodborne::PartyMapBoard maps;
  for (int round = 0; round < 20000; ++round) {
    const QByteArray body = RandomBody(random);
    (void)Bloodborne::ParseRestUpdate(body);
    (void)Bloodborne::ParseMapUpdate(body);
    (void)Bloodborne::ParseTrophyUpdate(body);
    if (const auto update = Bloodborne::ParseFlagUpdate(body)) {
      (void)Bloodborne::FormatKillBoard(
          flags.Update("p", update->member, update->since, update->changes));
    }
    if (const auto update = Bloodborne::ParseEchoUpdate(body)) {
      CHECK(update->echoes.size() == update->maps.size());
      (void)Bloodborne::FormatEchoBoard(echoes.Update(
          "p", update->member, update->since, update->echoes, update->maps));
    }
    if (const auto update = Bloodborne::ParseMapStateUpdate(body)) {
      (void)Bloodborne::FormatMapStateBoard(
          states.Update("p", update->member, update->map, update->bits));
    }
    if (const auto update = Bloodborne::ParseMapUpdate(body)) {
      (void)Bloodborne::FormatMapBoard(
          maps.Update("p", update->member, update->map, round));
    }
  }
  // Whatever was posted, a well-formed post still works.
  const auto answer =
      echoes.Update("p", "reader", 0, {}, {}); // everything after 0
  CHECK(answer.echoes.size() == answer.maps.size());
  CHECK(Bloodborne::ParseEchoUpdate("guest 0 120:402718720").has_value());

  std::cout << "bloodborne party fuzz: ok\n";
  return 0;
}
