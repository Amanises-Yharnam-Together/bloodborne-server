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

  // Seamless travel: every member posts the map it is on ("<member> <map>",
  // the packed map in hex) and reads back where the others are.
  {
    using Bloodborne::PartyMapBoard;
    const auto update = Bloodborne::ParseMapUpdate("host 18010000");
    CHECK(update.has_value());
    CHECK(update->member == "host");
    CHECK(update->map == 0x18010000u);
    CHECK(!Bloodborne::ParseMapUpdate("host").has_value());
    CHECK(!Bloodborne::ParseMapUpdate("host xyz").has_value());
    CHECK(!Bloodborne::ParseMapUpdate("ho st 18010000").has_value());

    PartyMapBoard board;
    board.Update(Party, "host", 0x18010000, 0);
    const auto maps = board.Update(Party, "guest", 0x15000000, 1);
    const QByteArray newline(1, 10);
    CHECK(Bloodborne::FormatMapBoard(maps) ==
          "guest 15000000" + newline + "host 18010000" + newline);
    // A member that stopped posting is gone.
    const auto later =
        board.Update(Party, "guest", 0x15000000, PartyMapBoard::FreshMs + 1);
    CHECK(later.size() == 1 && later[0].member == "guest");
    CHECK(board.Update({}, "host", 0x18010000, 0).isEmpty());
  }

  // Shared trophies: every member posts the trophies it unlocked in the party
  // ("<member> [<id>...]") and reads back every trophy the party unlocked, one
  // id per line, so the others unlock them too.
  {
    using Bloodborne::PartyTrophyBoard;
    const auto update = Bloodborne::ParseTrophyUpdate("host 12 3");
    CHECK(update.has_value());
    CHECK(update->member == "host");
    CHECK(update->trophies == QList<qint32>({12, 3}));
    const auto empty = Bloodborne::ParseTrophyUpdate("guest");
    CHECK(empty.has_value() && empty->trophies.isEmpty());
    CHECK(!Bloodborne::ParseTrophyUpdate("").has_value());
    CHECK(!Bloodborne::ParseTrophyUpdate("host x").has_value());
    CHECK(!Bloodborne::ParseTrophyUpdate("host -1").has_value());
    CHECK(!Bloodborne::ParseTrophyUpdate("host 128").has_value());

    PartyTrophyBoard board;
    board.Update(Party, "host", {12, 3});
    board.Update("other:party", "stranger", {40});
    const auto trophies = board.Update(Party, "guest", {});
    const QByteArray newline(1, 10);
    CHECK(Bloodborne::FormatTrophyBoard(trophies) ==
          "3" + newline + "12" + newline);
    // Unlocks stay on the board: a member that joins later still gets them.
    CHECK(board.Update(Party, "late", {12}) == QList<qint32>({3, 12}));
    CHECK(board.Update({}, "host", {5}).isEmpty());
  }

  // Shared world changes: a member posts "<member> <since> [<flag>:<0|1>...]",
  // the flags it changed in the session, and reads back the latest sequence
  // number and every other member's change after <since> ("<flag> <0|1>").
  {
    using Bloodborne::PartyFlagBoard;
    const auto update =
        Bloodborne::ParseFlagUpdate("guest 0 12417830:1 12417831:1 7116:0");
    CHECK(update.has_value());
    CHECK(update->member == "guest");
    CHECK(update->since == 0);
    CHECK(update->changes.size() == 3);
    CHECK(update->changes[0].flag == 12417830 && update->changes[0].on);
    CHECK(update->changes[2].flag == 7116 && !update->changes[2].on);
    CHECK(Bloodborne::ParseFlagUpdate("host 4").has_value());
    CHECK(!Bloodborne::ParseFlagUpdate("host").has_value());
    CHECK(!Bloodborne::ParseFlagUpdate("host x").has_value());
    CHECK(!Bloodborne::ParseFlagUpdate("host 0 7116").has_value());
    CHECK(!Bloodborne::ParseFlagUpdate("host 0 7116:2").has_value());

    PartyFlagBoard board;
    const QByteArray newline(1, 10);
    const auto guest = board.Update(Party, "guest", 0, update->changes);
    CHECK(guest.latest == 3);
    CHECK(guest.changes.isEmpty()); // a member doesn't get its own changes back
    board.Update("other:party", "stranger", 0, update->changes);
    const auto host = board.Update(Party, "host", 0, {});
    CHECK(host.latest == 3);
    CHECK(Bloodborne::FormatFlagBoard(host) ==
          "3" + newline + "12417830 1" + newline + "12417831 1" + newline +
              "7116 0" + newline);
    CHECK(board.Update(Party, "host", 3, {}).changes.isEmpty());
    CHECK(board.Update({}, "host", 0, update->changes).latest == 0);
  }

  // Shared kill echoes: a member that killed an enemy with no other member on
  // its map posts
  // "<member> <since> [<echoes>...]" and reads back the latest sequence number
  // and every other member's echoes after <since>, one per line.
  {
    using Bloodborne::PartyEchoBoard;
    const auto update = Bloodborne::ParseEchoUpdate("guest 0 120 4000");
    CHECK(update.has_value());
    CHECK(update->member == "guest");
    CHECK(update->since == 0);
    CHECK(update->echoes.size() == 2);
    CHECK(update->echoes[0] == 120 && update->echoes[1] == 4000);
    CHECK(Bloodborne::ParseEchoUpdate("host 7").has_value());
    CHECK(!Bloodborne::ParseEchoUpdate("host").has_value());
    CHECK(!Bloodborne::ParseEchoUpdate("host 0 -5").has_value());
    CHECK(!Bloodborne::ParseEchoUpdate("host 0 0").has_value());
    CHECK(!Bloodborne::ParseEchoUpdate("host 0 x").has_value());

    PartyEchoBoard board;
    const QByteArray newline(1, 10);
    const auto guest = board.Update(Party, "guest", 0, update->echoes);
    CHECK(guest.latest == 2);
    CHECK(guest.echoes.isEmpty()); // a member doesn't get its own echoes back
    board.Update("other:party", "stranger", 0, update->echoes);
    const auto host = board.Update(Party, "host", 0, {});
    CHECK(host.latest == 2);
    CHECK(Bloodborne::FormatEchoBoard(host) ==
          "2" + newline + "120" + newline + "4000" + newline);
    CHECK(board.Update(Party, "host", 2, {}).echoes.isEmpty());
    CHECK(board.Update({}, "host", 0, update->echoes).latest == 0);
  }

  // Three players: a kill's echoes carry the map they were made on
  // ("<echoes>:<map>"), so a member on that map, who got them from the game,
  // skips them and only a member elsewhere adds them ("<echoes> <map>").
  {
    using Bloodborne::PartyEchoBoard;
    const auto update =
        Bloodborne::ParseEchoUpdate("guest 0 120:402718720 4000");
    CHECK(update.has_value());
    CHECK(update->echoes.size() == 2 && update->maps.size() == 2);
    CHECK(update->echoes[0] == 120 && update->maps[0] == 402718720u);
    CHECK(update->echoes[1] == 4000 && update->maps[1] == 0u);
    CHECK(!Bloodborne::ParseEchoUpdate("host 0 120:x").has_value());
    CHECK(!Bloodborne::ParseEchoUpdate("host 0 0:5").has_value());
    CHECK(!Bloodborne::ParseEchoUpdate("host 0 120:").has_value());
    CHECK(!Bloodborne::ParseEchoUpdate("host 0 1:2:3").has_value());
    const auto no_map = Bloodborne::ParseEchoUpdate("host 0 120:0");
    CHECK(no_map.has_value() && no_map->maps == QList<quint32>({0}));

    PartyEchoBoard board;
    const QByteArray newline(1, 10);
    board.Update(Party, "guest", 0, update->echoes, update->maps);
    CHECK(Bloodborne::FormatEchoBoard(board.Update(Party, "host", 0, {}, {})) ==
          "2" + newline + "120 402718720" + newline + "4000" + newline);
  }

  // Three players: the kill board names who posted each change, so one
  // member's visit ending (its copy respawned) does not take back another
  // member's kill of the same enemy.
  {
    using Bloodborne::FlagChange;
    using Bloodborne::PartyFlagBoard;
    PartyFlagBoard board;
    const QByteArray newline(1, 10);
    board.Update(Party, "guestA", 0, {FlagChange{268517532, true}});
    board.Update(Party, "guestB", 0, {FlagChange{268517532, false}});
    CHECK(Bloodborne::FormatKillBoard(board.Update(Party, "host", 0, {})) ==
          "2" + newline + "268517532 1 guestA" + newline +
              "268517532 0 guestB" + newline);
  }

  // Rig smoke 2026-10-12 (run 153353): a guest's game restarted under the same
  // member name. Its kills from before stood on the board (its new game never
  // knew them to take back), and after the party's wipe the others hid the
  // respawned enemy. A game's first post (since FreshStart) takes back the
  // member's standing kills.
  {
    using Bloodborne::FlagChange;
    using Bloodborne::PartyFlagBoard;
    PartyFlagBoard board;
    const QByteArray newline(1, 10);
    board.Update(Party, "guest", 0,
                 {FlagChange{268517387, true}, FlagChange{268517388, true},
                  FlagChange{268517388, false}});
    board.Update(Party, "host", 0, {FlagChange{268517389, true}});
    const auto host = board.Update(Party, "host", 4, {});
    CHECK(host.latest == 4 && host.changes.isEmpty());
    board.WithdrawMember(Party, "guest");
    CHECK(Bloodborne::FormatKillBoard(board.Update(Party, "host", 4, {})) ==
          "5" + newline + "268517387 0 guest" + newline);
    board.WithdrawMember(Party, "guest"); // nothing standing any more
    CHECK(board.Update(Party, "host", 5, {}).changes.isEmpty());
    board.WithdrawMember({}, "guest");
    CHECK(Bloodborne::ParseFlagUpdate("guest 9223372036854775807")->since ==
          PartyFlagBoard::FreshStart);
  }

  // World state per map: the host posts "<member> <map> <hex flag bits>" for
  // the map it is on (killed-for-good enemies, doors, shortcuts) and every
  // member reads back the others' latest
  // "<map> <hex>" lines, so a guest loading the host's map takes the host's
  // state.
  {
    using Bloodborne::PartyMapStateBoard;
    const auto update =
        Bloodborne::ParseMapStateUpdate("host 402718720 00ff10");
    CHECK(update.has_value());
    CHECK(update->member == "host");
    CHECK(update->map == 402718720u);
    CHECK(update->bits == "00ff10");
    CHECK(
        Bloodborne::ParseMapStateUpdate("guest 0").has_value()); // reading only
    CHECK(!Bloodborne::ParseMapStateUpdate("host").has_value());
    CHECK(!Bloodborne::ParseMapStateUpdate("host x 00").has_value());
    CHECK(!Bloodborne::ParseMapStateUpdate("host 1 0g").has_value());
    CHECK(
        !Bloodborne::ParseMapStateUpdate("host 1 0").has_value()); // odd length

    PartyMapStateBoard board;
    const QByteArray newline(1, 10);
    CHECK(board.Update(Party, "host", 402718720u, "00ff10").isEmpty());
    board.Update("other:party", "stranger", 1, "ff");
    const auto guest = board.Update(Party, "guest", 0, {});
    CHECK(guest.size() == 1);
    CHECK(Bloodborne::FormatMapStateBoard(guest) ==
          "402718720 00ff10" + newline);
    board.Update(Party, "host", 402718720u, "01");
    CHECK(
        Bloodborne::FormatMapStateBoard(board.Update(Party, "guest", 0, {})) ==
        "402718720 01" + newline); // the latest only
    CHECK(board.Update({}, "guest", 0, {}).isEmpty());
  }

  // Enemy HP (the map state board with a freshness window): a member that
  // stopped posting (its game quit) drops out, so the next session's enemies
  // do not start hurt by an old report (rig test 2026-10-09).
  {
    using Bloodborne::PartyMapStateBoard;
    PartyMapStateBoard board(5'000);
    const QByteArray newline(1, 10);
    board.Update(Party, "guest", 402718720u, "00ff10", 1'000);
    CHECK(Bloodborne::FormatMapStateBoard(board.Update(
              Party, "host", 0, {}, 4'000)) == "402718720 00ff10" + newline);
    CHECK(board.Update(Party, "host", 0, {}, 7'000).isEmpty()); // gone stale
    PartyMapStateBoard kept; // the host's map state: kept
    kept.Update(Party, "host", 402718720u, "01", 1'000);
    CHECK(kept.Update(Party, "guest", 0, {}, 999'000).size() == 1);
  }

  // Party death: each member posts its vitals ("<member> <map> <vital> <hp>
  // <max> <down> <landed> <needed> <x> <y> <z>", map in hex) with its revive
  // hits on downed members ("<target>:<down>:<hits>" totals).
  {
    using Bloodborne::ParseVitalsUpdate;
    const auto update = ParseVitalsUpdate(
        "host 18010000 up 900 1000 0 0 0 1 2.5 -3 guest:4:2 third:9:1");
    CHECK(update.has_value());
    CHECK(update->self.member == "host" && update->self.map == 0x18010000u);
    CHECK(update->self.vital == "up" && update->self.hp == 900 &&
          update->self.max == 1000);
    CHECK(update->self.at == "1 2.5 -3");
    CHECK(update->hits.size() == 2);
    CHECK(update->hits[0].target == "guest" && update->hits[0].down == 4 &&
          update->hits[0].hits == 2);
    CHECK(update->hits[1].target == "third" && update->hits[1].down == 9);
    CHECK(ParseVitalsUpdate("guest 18010000 down 1 1000 4 2 3 0 0 0")
              .has_value());
    CHECK(!ParseVitalsUpdate("").has_value());
    CHECK(!ParseVitalsUpdate("host 18010000 sleeping 1 1 0 0 0 1 2 3"));
    CHECK(!ParseVitalsUpdate("host 18010000 up 1 1 0 0 0 1 2"));
    CHECK(!ParseVitalsUpdate("host 18010000 up 1 1 0 0 0 1 2 x"));
    CHECK(!ParseVitalsUpdate("host zz up 1 1 0 0 0 1 2 3"));
    CHECK(!ParseVitalsUpdate("host 18010000 up 1 1 -1 0 0 1 2 3"));
    CHECK(!ParseVitalsUpdate("host 18010000 up 1 1 0 0 0 1 2 3 guest:4"));
    CHECK(!ParseVitalsUpdate("host 18010000 up 1 1 0 0 0 1 2 3 bad-name:4:1"));
    CHECK(!ParseVitalsUpdate("bad\nname 18010000 up 1 1 0 0 0 1 2 3"));
  }

  // The answer: every member's line with the party's revive hits on its
  // current down added, then the wipes. Hits add up per down, from everyone.
  {
    using Bloodborne::ParseVitalsUpdate;
    Bloodborne::PartyVitalsBoard board;
    board.Update(
        Party,
        *ParseVitalsUpdate("host 18010000 up 900 1000 0 0 0 1 0 0 guest:4:2"),
        1'000);
    board.Update(Party,
                 *ParseVitalsUpdate("guest 18010000 down 1 1000 4 0 3 0 0 0"),
                 1'100);
    const auto answer = board.Update(
        Party,
        *ParseVitalsUpdate(
            "third 18010000 up 800 1000 0 0 0 2 0 0 guest:4:1 guest:3:5"),
        1'200);
    CHECK(answer.members.size() == 3);
    CHECK(answer.members[0].member == "guest" &&
          answer.members[0].reviveHits == 3);
    CHECK(answer.wipes.isEmpty());
    CHECK(Bloodborne::FormatVitalsBoard(answer) ==
          "guest 18010000 down 1 1000 4 0 3 0 0 0 3\n"
          "host 18010000 up 900 1000 0 0 0 1 0 0 0\n"
          "third 18010000 up 800 1000 0 0 0 2 0 0 0\n");
  }

  // A map where nobody stands any more is wiped, once: everyone down there.
  // Members on other maps neither stand nor fall with it.
  {
    using Bloodborne::ParseVitalsUpdate;
    using Bloodborne::PartyVitalsBoard;
    PartyVitalsBoard board;
    const auto post = [&](const char *body, qint64 now) {
      return board.Update(Party, *ParseVitalsUpdate(body), now);
    };
    post("host 18010000 up 900 1000 0 0 0 0 0 0", 1'000);
    post("guest 18010000 down 1 1000 4 0 3 0 0 0", 1'100);
    CHECK(
        post("third 15000000 up 900 1000 0 0 0 0 0 0", 1'200).wipes.isEmpty());
    auto answer = post("host 18010000 down 1 1000 7 0 3 0 0 0", 1'300);
    CHECK(answer.wipes.size() == 1);
    CHECK(answer.wipes[0].id == 1 && answer.wipes[0].map == 0x18010000u);
    CHECK(answer.wipes[0].members == QList<QByteArray>({"guest", "host"}));
    CHECK(Bloodborne::FormatVitalsBoard(answer).endsWith(
        "wipe 1 18010000 guest host\n"));
    // The others read it on their next post; it is not declared again.
    answer = post("guest 18010000 down 1 1000 4 0 3 0 0 0", 1'400);
    CHECK(answer.wipes.size() == 1 && answer.wipes[0].id == 1);
    CHECK(post("third 15000000 up 900 1000 0 0 0 0 0 0", 1'500).wipes.size() ==
          1);
    // A member that died for real is not wiped again.
    post("host 18010000 dead 0 1000 7 0 3 0 0 0", 2'000);
    answer = post("guest 18010000 down 1 1000 4 0 3 0 0 0", 2'100);
    CHECK(answer.wipes.size() == 1 && answer.wipes[0].id == 1);
    // A wipe stays for WipeMs; a member still down after that is wiped again.
    answer = post("guest 18010000 down 1 1000 4 0 3 0 0 0",
                  1'300 + PartyVitalsBoard::WipeMs + 1);
    CHECK(answer.wipes.size() == 1 && answer.wipes[0].id == 2);
    CHECK(answer.wipes[0].members == QList<QByteArray>({"guest"}));
  }

  // Down with nobody else standing on the map is a wipe at once. A member that
  // went quiet, plays without party death or is loading (map 0) stands nowhere.
  {
    using Bloodborne::ParseVitalsUpdate;
    Bloodborne::PartyVitalsBoard board;
    board.Update(
        Party, *ParseVitalsUpdate("host 18010000 up 900 1000 0 0 0 0 0 0"), 0);
    board.Update(Party,
                 *ParseVitalsUpdate("third 18010000 off 900 1000 0 0 0 0 0 0"),
                 9'000);
    board.Update(Party, *ParseVitalsUpdate("fourth 0 up 900 1000 0 0 0 0 0 0"),
                 9'000);
    const auto answer = board.Update(
        Party, *ParseVitalsUpdate("guest 18010000 down 1 1000 1 0 3 0 0 0"),
        9'000);
    CHECK(answer.members.size() == 3); // the host went quiet
    CHECK(answer.wipes.size() == 1);
    CHECK(answer.wipes[0].members == QList<QByteArray>({"guest"}));
    // Another party sees none of it.
    CHECK(
        board
            .Update("other:party",
                    *ParseVitalsUpdate("host 18010000 up 900 1000 0 0 0 0 0 0"),
                    9'000)
            .wipes.isEmpty());
  }

  // No party key: nothing to coordinate.
  {
    PartyRestBoard board;
    CHECK(board.Update({}, "host", Lamp, 0).isEmpty());
    CHECK(board.Update({}, "guest", Lamp, 0).isEmpty());
  }

  std::cout << "bloodborne party rest: ok\n";
  // The world snapshot (POST /bloodborne/party/world): the host posts "<member>
  // <hex>", a member reads with "<member>" and gets "<age ms> <hex>" back, or
  // nothing before a post.
  {
    using Bloodborne::PartyWorldSnapshotBoard;
    const auto post = Bloodborne::ParseWorldSnapshotUpdate("4b61 01ab");
    CHECK(post && post->member == "4b61" && post->snapshot == "01ab");
    const auto read = Bloodborne::ParseWorldSnapshotUpdate("4c69");
    CHECK(read && read->member == "4c69" && read->snapshot.isEmpty());
    CHECK(!Bloodborne::ParseWorldSnapshotUpdate("4b61 01a"));
    CHECK(!Bloodborne::ParseWorldSnapshotUpdate("4b61 01AG"));
    CHECK(!Bloodborne::ParseWorldSnapshotUpdate("4b61 01 ab"));
    CHECK(!Bloodborne::ParseWorldSnapshotUpdate(
        "4b61 " +
        QByteArray(PartyWorldSnapshotBoard::MaxSnapshotHex + 2, 'a')));
    PartyWorldSnapshotBoard board;
    CHECK(!board.Update(Party, "4c69", {}, 1000));
    CHECK(Bloodborne::FormatWorldSnapshot(std::nullopt).isEmpty());
    board.Update(Party, "4b61", "01ab", 1000);
    const auto snapshot = board.Update(Party, "4c69", {}, 2500);
    CHECK(snapshot && snapshot->snapshot == "01ab" && snapshot->ageMs == 1500);
    CHECK(Bloodborne::FormatWorldSnapshot(snapshot) ==
          QByteArray("1500 01ab") + '\n');
    board.Update(Party, "4b61", "02cd", 3000);
    CHECK(board.Update(Party, "4c69", {}, 3000)->snapshot == "02cd");
    CHECK(!board.Update("rig:other", "4c69", {}, 3000));
    CHECK(!board.Update({}, "4b61", "01ab", 3000));
  }

  return 0;
}
