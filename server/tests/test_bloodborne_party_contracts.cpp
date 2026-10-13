// SPDX-FileCopyrightText: Copyright 2026 shadNet Project
// SPDX-License-Identifier: GPL-2.0-or-later
// The party boards' messages as the emulator sends and reads them
// (tests/contracts/bloodborne_party_boards.txt, which the emulator's tests read
// too): every update parses to the values the emulator encoded, and every
// answer formats to the text the emulator parses.
#include <fstream>
#include <iostream>
#include <map>
#include <string>

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

const QByteArray Kaniel = "4b616e69656c";
const QByteArray Ada = "416461";
constexpr quint32 CentralYharnam = 0x18010000;
constexpr quint32 HuntersDream = 0x15000000;

std::map<std::string, QByteArray> ReadContracts(const char *path) {
  std::map<std::string, QByteArray> messages;
  std::ifstream file(path);
  std::string line;
  while (std::getline(file, line)) {
    if (line.empty() || line[0] == '#') {
      continue;
    }
    const auto tab = line.find('\t');
    if (tab == std::string::npos) {
      continue;
    }
    std::string text;
    for (size_t index = tab + 1; index < line.size(); ++index) {
      if (line[index] == '\\' && index + 1 < line.size() &&
          line[index + 1] == 'n') {
        text += '\n';
        ++index;
      } else {
        text += line[index];
      }
    }
    messages[line.substr(0, tab)] = QByteArray::fromStdString(text);
  }
  return messages;
}

} // namespace

int main() {
  const auto messages = ReadContracts(BLOODBORNE_CONTRACTS);
  CHECK(messages.size() == 21);
  const auto message = [&](const char *name) { return messages.at(name); };

  {
    const auto update = Bloodborne::ParseMapUpdate(message("map.update"));
    CHECK(update && update->member == Kaniel && update->map == CentralYharnam);
    CHECK(Bloodborne::FormatMapBoard(
              {{Kaniel, CentralYharnam}, {Ada, HuntersDream}}) ==
          message("map.answer"));
  }
  {
    const auto update = Bloodborne::ParseRestUpdate(message("rest.update"));
    CHECK(update && update->member == Kaniel && update->lamp == 72410100 &&
          update->at == "-12.5 3 40.25");
    CHECK(Bloodborne::FormatRestBoard(
              {{Kaniel, 72410100, "-12.5 3 40.25"}, {Ada, 72410100, {}}}) ==
          message("rest.answer"));
  }
  {
    const auto update = Bloodborne::ParseTrophyUpdate(message("trophy.update"));
    CHECK(update && update->member == Kaniel &&
          update->trophies == QList<qint32>({3, 12}));
    CHECK(Bloodborne::FormatTrophyBoard({3, 12}) == message("trophy.answer"));
  }
  {
    const auto update = Bloodborne::ParseFlagUpdate(message("flags.update"));
    CHECK(update && update->member == Kaniel && update->since == 0 &&
          update->changes.size() == 2);
    CHECK(update->changes[0].flag == 12417830 && update->changes[0].on);
    CHECK(update->changes[1].flag == 7116 && !update->changes[1].on);
    Bloodborne::FlagBoardAnswer answer{3, update->changes, {}};
    CHECK(Bloodborne::FormatFlagBoard(answer) == message("flags.answer"));
  }
  {
    const auto update = Bloodborne::ParseFlagUpdate(message("kills.update"));
    CHECK(update && update->member == Kaniel && update->since == 7 &&
          update->changes.size() == 1);
    CHECK(update->changes[0].flag == 268517532 && update->changes[0].on);
    Bloodborne::FlagBoardAnswer answer{
        9, {{268517532, true}, {268517532, false}}, {Kaniel, Ada}};
    CHECK(Bloodborne::FormatKillBoard(answer) == message("kills.answer"));
  }
  for (const char *board : {"mapstate", "hp"}) {
    const std::string name = board;
    const auto update =
        Bloodborne::ParseMapStateUpdate(message((name + ".update").c_str()));
    CHECK(update && update->member == Kaniel && update->map == CentralYharnam);
    CHECK(Bloodborne::FormatMapStateBoard({{update->map, update->bits}}) ==
          message((name + ".answer").c_str()));
  }
  {
    const auto update = Bloodborne::ParseEchoUpdate(message("echoes.update"));
    CHECK(update && update->member == Kaniel && update->since == 3);
    CHECK(update->echoes == QList<qint32>({120, 4000}));
    CHECK(update->maps == QList<quint32>({CentralYharnam, 0}));
    Bloodborne::EchoBoardAnswer answer{5, {120, 4000}, {CentralYharnam, 0}};
    CHECK(Bloodborne::FormatEchoBoard(answer) == message("echoes.answer"));
  }

  {
    const auto update = Bloodborne::ParseVitalsUpdate(message("vitals.update"));
    CHECK(update && update->self.member == Kaniel &&
          update->self.map == CentralYharnam && update->self.vital == "up");
    CHECK(update->self.hp == 1200 && update->self.max == 1520 &&
          update->self.at == "-12.5 3 40.25");
    CHECK(update->hits.size() == 1 && update->hits[0].target == Ada &&
          update->hits[0].down == 4 && update->hits[0].hits == 2);
    Bloodborne::VitalsBoardAnswer answer;
    answer.members.append(update->self);
    answer.members.append(
        {Ada, CentralYharnam, "down", 1, 1380, 4, 2, 3, "-11 3 41", 2});
    answer.wipes.append({1, HuntersDream, {Kaniel, Ada}});
    CHECK(Bloodborne::FormatVitalsBoard(answer) == message("vitals.answer"));
  }

  std::cout << "bloodborne party contracts: ok\n";
  {
    const auto post =
        Bloodborne::ParseWorldSnapshotUpdate(message("world.update"));
    CHECK(post && post->member == Kaniel && post->snapshot == "01ab");
    const auto read =
        Bloodborne::ParseWorldSnapshotUpdate(message("world.read"));
    CHECK(read && read->member == Kaniel && read->snapshot.isEmpty());
    CHECK(Bloodborne::FormatWorldSnapshot(Bloodborne::WorldSnapshot{
              "01ab", 1500}) == message("world.answer"));
  }

  return 0;
}
