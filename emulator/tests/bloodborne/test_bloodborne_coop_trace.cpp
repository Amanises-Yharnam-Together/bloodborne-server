// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include <fmt/format.h>
#include <gtest/gtest.h>

#include "coop_trace_replay.h"
#include "core/bloodborne_party.h"

using namespace Core::Bloodborne;
namespace fs = std::filesystem;

namespace {

std::string Read(const fs::path& path) {
    std::ifstream file{path, std::ios::binary};
    return {std::istreambuf_iterator<char>{file}, {}};
}

// An expected-decisions file's lines, without its notes ('#').
std::vector<std::string> Lines(const std::string& text) {
    std::vector<std::string> lines;
    std::string line;
    const auto add = [&] {
        if (!line.empty() && line.front() != '#') {
            lines.push_back(line);
        }
        line.clear();
    };
    for (const char c : text) {
        if (c == '\n') {
            add();
        } else if (c != '\r') {
            line += c;
        }
    }
    add();
    return lines;
}

} // namespace

TEST(CoopTrace, OnUnlessSwitchedOff) {
    EXPECT_TRUE(CoopTraceEnabled(nullptr));
    EXPECT_TRUE(CoopTraceEnabled(""));
    EXPECT_TRUE(CoopTraceEnabled("1"));
    EXPECT_FALSE(CoopTraceEnabled("0"));
    EXPECT_FALSE(CoopTraceEnabled("off"));
}

TEST(CoopTrace, EscapedTextHasNoSpacesOrLineEndsAndComesBack) {
    std::string every_byte;
    for (int byte = 0; byte < 256; ++byte) {
        every_byte += static_cast<char>(byte);
    }
    const std::string escaped = TraceEscape(every_byte);
    EXPECT_EQ(escaped.find(' '), std::string::npos);
    EXPECT_EQ(escaped.find('\n'), std::string::npos);
    EXPECT_EQ(escaped.find('\r'), std::string::npos);
    EXPECT_EQ(TraceUnescape(escaped), every_byte);
    EXPECT_EQ(TraceEscape("1 0 abc\n"), "1\\s0\\sabc\\n");
    EXPECT_EQ(TraceUnescape("bad\\q"), std::nullopt);
    EXPECT_EQ(TraceUnescape("cut\\x4"), std::nullopt);
}

TEST(CoopTrace, RecordLineRoundTrip) {
    const TraceRecord record{1760000000123,
                             "board-in",
                             {{"endpoint", "/bloodborne/party/kills"},
                              {"status", "200"},
                              {"body", "42\n10014066 1 aa=bb\n"},
                              {"empty", ""}}};
    const std::string line = EncodeTraceRecord(record);
    EXPECT_EQ(line, "1760000000123 board-in endpoint=/bloodborne/party/kills status=200 "
                    "body=42\\n10014066\\s1\\saa=bb\\n empty=");
    EXPECT_EQ(ParseTraceRecord(line), record);
    EXPECT_EQ(ParseTraceRecord(line)->Field("status"), "200");
    EXPECT_EQ(ParseTraceRecord(line)->Field("missing"), std::nullopt);
    EXPECT_EQ(ParseTraceRecord("1 life"), (TraceRecord{1, "life", {}}));
    EXPECT_EQ(ParseTraceRecord("x life"), std::nullopt);
    EXPECT_EQ(ParseTraceRecord("1 life novalue"), std::nullopt);
    EXPECT_EQ(ParseTraceRecord(""), std::nullopt);
}

TEST(CoopTrace, BodiesAreCappedWithTheirSize) {
    TraceRecord record{1, "board-out", {}};
    AddTraceBody(record, "0123456789", 4);
    EXPECT_EQ(record.Field("size"), "10");
    EXPECT_EQ(record.Field("body"), "0123");
    TraceRecord whole{1, "board-out", {}};
    AddTraceBody(whole, "short", 4096);
    EXPECT_EQ(whole.Field("size"), "5");
    EXPECT_EQ(whole.Field("body"), "short");
}

TEST(CoopTrace, HexHandlesAndTokensRoundTrip) {
    const std::vector<u8> bytes{0x00, 0x7F, 0xAB, 0xFF};
    EXPECT_EQ(TraceHex(bytes), "007fabff");
    EXPECT_EQ(ParseTraceHex("007fabff"), bytes);
    EXPECT_EQ(ParseTraceHex("abc"), std::nullopt);
    EXPECT_EQ(ParseTraceHex("zz"), std::nullopt);

    const std::vector<u32> handles{0x10014066, 0x2A};
    EXPECT_EQ(TraceHandles(handles), "10014066,2a");
    EXPECT_EQ(ParseTraceHandles("10014066,2a"), handles);
    EXPECT_EQ(ParseTraceHandles(""), std::vector<u32>{});
    EXPECT_EQ(ParseTraceHandles("1,,2"), std::nullopt);

    const SeenCharacter seen{0x10014066, 450, 0x7FF012345678, true};
    EXPECT_EQ(TraceToken(seen), "10014066:1:7ff012345678:1");
    const auto back = ParseSeenToken(TraceToken(seen));
    ASSERT_TRUE(back);
    EXPECT_EQ(back->handle, seen.handle);
    EXPECT_EQ(back->hp, 1); // alive: VisitKills reads no more of the HP
    EXPECT_EQ(back->instance, seen.instance);
    EXPECT_TRUE(back->settling);
    EXPECT_EQ(ParseSeenToken("10014066:0:a000:0")->hp, 0);
    EXPECT_EQ(ParseSeenToken("10014066:0:a000"), std::nullopt);
    EXPECT_FALSE(back->untouched_boss);
    const SeenCharacter boss{0x1001409c, 2031, 0xA000, false, true};
    EXPECT_EQ(TraceToken(boss), "1001409c:1:a000:0:1") << "the boss mark only when set";
    EXPECT_TRUE(ParseSeenToken("1001409c:1:a000:0:1")->untouched_boss);
    const SeenCharacter theirs{0x1001400b, 0, 0xB000, false, false, true};
    EXPECT_EQ(TraceToken(theirs), "1001400b:0:b000:0:2");
    const auto back_theirs = ParseSeenToken("1001400b:0:b000:0:2");
    ASSERT_TRUE(back_theirs);
    EXPECT_TRUE(back_theirs->others_kill);
    EXPECT_FALSE(back_theirs->untouched_boss);
    EXPECT_EQ(ParseSeenToken("1001409c:1:a000:0:4"), std::nullopt);
    EXPECT_EQ(ParseSeenToken("1001409c:1:a000:0:0"), std::nullopt) << "flags only when set";

    const EnemyCopy copy{0x10014066, 401, 450, 49};
    EXPECT_EQ(TraceToken(copy), "10014066:401:450:49");
    const auto copy_back = ParseCopyToken("10014066:401:450:49");
    ASSERT_TRUE(copy_back);
    EXPECT_EQ(copy_back->handle, copy.handle);
    EXPECT_EQ(copy_back->hp, 401);
    EXPECT_EQ(copy_back->max, 450);
    EXPECT_EQ(copy_back->own_hits, 49);
    EXPECT_EQ(TraceToken(EnemyCopy{0x2A, 5, 10, std::nullopt}), "2a:5:10:-");
    EXPECT_EQ(ParseCopyToken("2a:5:10:-")->own_hits, std::nullopt);
    EXPECT_EQ(ParseCopyToken("2a:5:10"), std::nullopt);

    const EnemyHpReport report{0x5555000100000001, {{0x10014066, 49, 450}, {0x2A, 1, 2}}};
    EXPECT_EQ(TraceToken(report), "5555000100000001:10014066/49/450+2a/1/2");
    EXPECT_EQ(ParseReportToken(TraceToken(report)), report);
    EXPECT_EQ(ParseReportToken("5555:"), (EnemyHpReport{0x5555, {}}));
    EXPECT_EQ(ParseReportToken("5555:1/2"), std::nullopt);
}

TEST(CoopTrace, ListDeltasNameOnlyWhatChanged) {
    TraceListDelta delta;
    std::vector<std::string> list;
    const auto step = [&](std::vector<std::string> tokens) {
        const std::string text = delta.Next(tokens);
        EXPECT_TRUE(ApplyTraceListDelta(list, text)) << text;
        EXPECT_EQ(list, tokens) << text;
        return text;
    };
    EXPECT_EQ(step({"a:1", "b:2"}), "=a:1,b:2");
    EXPECT_EQ(step({"a:1", "b:2"}), "");
    EXPECT_EQ(step({"a:1", "b:3"}), "b:3");
    EXPECT_EQ(step({"b:3"}), "-a");
    EXPECT_EQ(step({"b:3", "c:1"}), "c:1");
    EXPECT_EQ(step({}), "-b,-c");
    delta.Reset();
    EXPECT_EQ(step({}), "=");
    EXPECT_EQ(step({"d:4"}), "d:4");
    delta.Reset();
    EXPECT_EQ(step({"d:4"}), "=d:4");
    // A delta that removes what the list does not have (a file cut mid-way) is taken as it is.
    std::vector<std::string> partial;
    EXPECT_TRUE(ApplyTraceListDelta(partial, "-zz,e:5"));
    EXPECT_EQ(partial, std::vector<std::string>{"e:5"});
    EXPECT_FALSE(ApplyTraceListDelta(partial, "nokey"));
}

TEST(CoopTrace, SamplerGivesOnlyChangesAndAllAgainInANewFile) {
    TraceSampler sampler;
    const TraceSampler::Fields values{{"visit", "1"}, {"mode", "apart"}};
    const std::vector<std::pair<std::string, std::vector<std::string>>> lists{{"alive", {"a:1"}}};
    EXPECT_EQ(sampler.Changes(0, values, lists),
              (TraceSampler::Fields{{"visit", "1"}, {"mode", "apart"}, {"alive", "=a:1"}}));
    EXPECT_EQ(sampler.Changes(0, values, lists), TraceSampler::Fields{});
    EXPECT_EQ(sampler.Changes(0, {{"visit", "2"}, {"mode", "apart"}}, {{"alive", {"a:2"}}}),
              (TraceSampler::Fields{{"visit", "2"}, {"alive", "a:2"}}));
    EXPECT_EQ(sampler.Changes(1, {{"visit", "2"}, {"mode", "apart"}}, {{"alive", {"a:2"}}}),
              (TraceSampler::Fields{{"visit", "2"}, {"mode", "apart"}, {"alive", "=a:2"}}));
}

TEST(CoopTrace, QueueNeverWaitsAndCountsWhatItDrops) {
    TraceQueue queue{2};
    EXPECT_TRUE(queue.Push("one"));
    EXPECT_TRUE(queue.Push("two"));
    EXPECT_FALSE(queue.Push("three"));
    EXPECT_EQ(queue.TakeDropped(), 1u);
    EXPECT_EQ(queue.TakeDropped(), 0u);
    EXPECT_EQ(queue.Take(std::chrono::milliseconds{0}), (std::vector<std::string>{"one", "two"}));
    EXPECT_EQ(queue.Take(std::chrono::milliseconds{0}), std::vector<std::string>{});
    EXPECT_TRUE(queue.Push("four"));
    std::thread writer{[&] { queue.Push("five"); }};
    writer.join();
    EXPECT_EQ(queue.Take(std::chrono::milliseconds{100}),
              (std::vector<std::string>{"four", "five"}));
}

TEST(CoopTrace, FilesAreNamedInLocalTimeAndTheNewestKept) {
    using namespace std::chrono;
    const sys_seconds when = sys_days{2026y / October / 12} + hours{23} + minutes{50} + seconds{1};
    EXPECT_EQ(CoopTraceFileName(when, 4321), "coop-trace-20261012-235001-4321.txt");

    const fs::path dir =
        fs::temp_directory_path() /
        ("bb-coop-trace-" + std::to_string(::testing::UnitTest::GetInstance()->random_seed()));
    fs::remove_all(dir);
    fs::create_directories(dir);
    for (const char* name :
         {"coop-trace-20261010-100000-1.txt", "coop-trace-20261011-100000-1.txt",
          "coop-trace-20261012-100000-1.txt", "shad_log.txt", "coop-trace-notes.md"}) {
        std::ofstream{dir / name} << "x";
    }
    EXPECT_EQ(PruneCoopTraces(dir, 2), 1u);
    EXPECT_FALSE(fs::exists(dir / "coop-trace-20261010-100000-1.txt"));
    EXPECT_TRUE(fs::exists(dir / "coop-trace-20261011-100000-1.txt"));
    EXPECT_TRUE(fs::exists(dir / "coop-trace-20261012-100000-1.txt"));
    EXPECT_TRUE(fs::exists(dir / "shad_log.txt"));
    EXPECT_TRUE(fs::exists(dir / "coop-trace-notes.md"));
    fs::remove_all(dir);
}

// What the game records (TraceSampler over each call's inputs) replays to the decisions the
// classes made live, though only changes were written.
TEST(CoopTraceReplay, RecordedInputsReplayToTheLiveDecisions) {
    VisitKills live;
    TraceSampler sampler;
    CoopTraceReplay replay;
    std::vector<std::string> live_decisions;
    std::vector<std::string> replayed;
    const u64 visit = 0x2101000000000001;
    const std::vector<u32> others{0x21010000};
    const auto tick = [&](s64 at, std::vector<SeenCharacter> characters) {
        for (const auto& [handle, killed] : live.Observe(visit, characters, others)) {
            live_decisions.push_back(
                fmt::format("kill {} {:#x}", killed ? "posted" : "withdrawn", handle));
        }
        std::vector<std::string> tokens;
        for (const SeenCharacter& character : characters) {
            tokens.push_back(TraceToken(character));
        }
        const auto fields = sampler.Changes(
            0, {{"visit", fmt::format("{:x}", visit)}, {"others", TraceHandles(others)}},
            {{"chars", tokens}});
        if (!fields.empty()) {
            for (std::string& decision : replay.Feed(EncodeTraceRecord({at, "kills-in", fields}))) {
                if (decision.starts_with("kill ")) {
                    replayed.push_back(std::move(decision));
                }
            }
        }
    };
    tick(1, {{0x10, 450, 0xA0}, {0x11, 300, 0xB0}});
    tick(2, {{0x10, 400, 0xA0}, {0x11, 300, 0xB0}});
    tick(3, {{0x10, 0, 0xA0}, {0x11, 300, 0xB0}});
    tick(4, {{0x10, 0, 0xA0}, {0x11, 300, 0xB0}});
    tick(5, {{0x10, 0, 0xA0}, {0x11, 0, 0xC0}}); // a new object, dead already: no kill
    tick(6, {{0x10, 0, 0xA0}, {0x11, 0, 0xC0}, {0x12, 0, 0xD0, true}});
    EXPECT_EQ(live_decisions, (std::vector<std::string>{"kill posted 0x10"}));
    EXPECT_EQ(replayed, live_decisions);
}

TEST(CoopTrace, PlaceChangesRoundTrip) {
    const PlaceChange change{{PlaceState::Playing, 0x21010000, 3, 1000},
                             {PlaceState::Playing, 0x21000000, 4, 2500},
                             PlaceCause::Border};
    const TraceRecord record{1760000000000, "place", TracePlaceFields(change)};
    EXPECT_EQ(EncodeTraceRecord(record),
              "1760000000000 place from-state=Playing from-map=21010000 from-visit=3 "
              "from-since=1000 to-state=Playing to-map=21000000 to-visit=4 to-since=2500 "
              "cause=Border");
    EXPECT_EQ(ParsePlaceFields(*ParseTraceRecord(EncodeTraceRecord(record))), change);
    TraceRecord broken = record;
    broken.fields.back().second = "Flying";
    EXPECT_EQ(ParsePlaceFields(broken), std::nullopt);
    broken = record;
    broken.fields.front().second = "Asleep";
    EXPECT_EQ(ParsePlaceFields(broken), std::nullopt);
}

TEST(CoopTraceReplay, PlaceChangesAreShownAsTheGameLogsThem) {
    const PlaceChange change{{PlaceState::Loading, 0x15000000, 2, 100},
                             {PlaceState::Playing, 0x15000000, 2, 900},
                             PlaceCause::Load};
    CoopTraceReplay replay;
    EXPECT_EQ(replay.Feed(EncodeTraceRecord({1, "place", TracePlaceFields(change)})),
              std::vector<std::string>{FormatPlaceChange(change)});
    EXPECT_EQ(replay.Feed("2 place cause=Load"),
              std::vector<std::string>{"place: unreadable record"});
}

// The world log's records (wlog-in, wlog-path) replay through WorldLogHost and WorldLogMember to
// the decisions the game made live.
TEST(CoopTraceReplay, WorldLogRecordsReplayToTheLiveDecisions) {
    const u64 epoch = WorldLogEpoch(0x77, 1);
    const auto record = [](s64 at, std::string_view role, std::string_view op,
                           std::span<const u8> payload) {
        return EncodeTraceRecord({at, "wlog-in", WorldLogTraceFields(role, op, payload)});
    };
    // The host's side: its start and two proposals, one of them late.
    WorldLogHost host;
    host.Start(epoch, false);
    const WorldLogProposal kill{"6775657374", epoch, 5, 2, {{0x18010000, {0x10014066}}}};
    const WorldLogProposal late{"6775657374", epoch, 5, 1, {}};
    CoopTraceReplay replay;
    std::vector<std::string> out;
    const auto feed = [&](const std::string& line) {
        for (std::string& decision : replay.Feed(line)) {
            out.push_back(std::move(decision));
        }
    };
    feed(EncodeTraceRecord({1, "wlog-path", {{"path", "log"}, {"why", "1 peer has it"}}}));
    feed(record(2, "host", "start", EncodeWorldLogStart(epoch, false)));
    feed(record(3, "host", "propose", EncodeWorldLogProposal(kill)));
    feed(record(4, "host", "propose", EncodeWorldLogProposal(late)));
    EXPECT_EQ(host.Propose(kill).entries.size(), 1u);
    EXPECT_TRUE(host.Propose(late).stale);
    // The member's side: a batch past a gap, then the snapshot that mends it.
    const WorldLogBatch ahead{
        "686f7374", epoch, 3, {{3, epoch, 0x18010000, WorldLogKind::Unkill, 0x10014066}}};
    feed(record(5, "member", "entries", EncodeWorldLogBatch(ahead)));
    feed(record(6, "member", "shown-snapshot",
                EncodeWorldLogShownSnapshot({"686f7374", epoch, 2, 0, 1, {}})));
    feed(record(6, "member", "map-state-snapshot",
                EncodeWorldLogMapStateSnapshot({"686f7374", epoch, 2, {}})));
    feed(record(6, "member", "flags-snapshot",
                EncodeWorldLogFlagsSnapshot({"686f7374", epoch, 2, {}})));
    feed(record(6, "member", "hp-snapshot",
                EncodeWorldLogHpSnapshot({"686f7374", epoch, 2, 0, 1, {}})));
    feed(
        record(6, "member", "snapshot",
               EncodeWorldLogSnapshot({"686f7374", epoch, 2, 0, 1, {{0x18010000, {0x10014066}}}})));
    feed(record(7, "member", "reset", {}));
    feed(record(8, "member", "entries", std::vector<u8>{1, 2}));
    EXPECT_EQ(out, (std::vector<std::string>{
                       "world log path: log (1 peer has it)",
                       "wlog host start epoch 7700000001",
                       "wlog host appended seq 1 reset every map",
                       "wlog host appended seq 2 kill 0x10014066 on 0x18010000",
                       "wlog host dropped a late proposal from 6775657374",
                       "wlog member follows epoch 7700000001",
                       "wlog member gap: has seq 0, host at 3",
                       "wlog member gap: has seq 0, host at 2",
                       "wlog member gap: has seq 0, host at 2",
                       "wlog member gap: has seq 0, host at 2",
                       "wlog member gap: has seq 0, host at 2",
                       "wlog member snapshot to seq 2",
                       "wlog member shown on 0x0: 0 characters",
                       "wlog member map state of 0x0",
                       "wlog member applied seq 3 unkill 0x10014066 on 0x18010000",
                       "wlog member reset",
                       "wlog-in: unreadable member entries",
                   }));
}

// The shown characters on the world log (step 2): the host's Show, and a member's snapshot that
// waits for its shown parts.
TEST(CoopTraceReplay, WorldLogShownRecordsReplay) {
    const u64 epoch = WorldLogEpoch(0x77, 1);
    const auto record = [](s64 at, std::string_view role, std::string_view op,
                           std::span<const u8> payload) {
        return EncodeTraceRecord({at, "wlog-in", WorldLogTraceFields(role, op, payload)});
    };
    const CharacterShown iosefka{0x10014000, false, true, false, false};
    CoopTraceReplay replay;
    std::vector<std::string> out;
    const auto feed = [&](const std::string& line) {
        for (std::string& decision : replay.Feed(line)) {
            out.push_back(std::move(decision));
        }
    };
    feed(record(1, "host", "start", EncodeWorldLogStart(epoch, false)));
    feed(record(2, "host", "show",
                EncodeWorldLogShow(0x18000000, std::vector<CharacterShown>{iosefka})));
    feed(record(3, "member", "map-state-snapshot",
                EncodeWorldLogMapStateSnapshot({"h", epoch, 3, {}})));
    feed(record(3, "member", "flags-snapshot", EncodeWorldLogFlagsSnapshot({"h", epoch, 3, {}})));
    feed(record(3, "member", "hp-snapshot", EncodeWorldLogHpSnapshot({"h", epoch, 3, 0, 1, {}})));
    feed(record(4, "member", "snapshot", EncodeWorldLogSnapshot({"h", epoch, 3, 0, 1, {}})));
    feed(record(
        5, "member", "shown-snapshot",
        EncodeWorldLogShownSnapshot({"h", epoch, 3, 0, 1, {0x18000000, {{0x10014000, 2}}}})));
    feed(record(6, "member", "show", {}));
    EXPECT_EQ(out, (std::vector<std::string>{
                       "wlog host start epoch 7700000001",
                       "wlog host appended seq 1 reset every map",
                       "wlog host appended seq 2 shown map 0x18000000",
                       "wlog host appended seq 3 shown 0x10014000 on 0x18000000 bits 2",
                       "wlog member follows epoch 7700000001",
                       "wlog member gap: has seq 0, host at 3",
                       "wlog member gap: has seq 0, host at 3",
                       "wlog member gap: has seq 0, host at 3",
                       "wlog member gap: has seq 0, host at 3",
                       "wlog member snapshot to seq 3",
                       "wlog member shown on 0x18000000: 1 characters",
                       "wlog member map state of 0x0",
                       "wlog-in: unreadable member show",
                   }));
}

// The host's return point on the log (protocol 8): its input replays to the same entry.
TEST(CoopTraceReplay, WorldLogReturnPointRecordsReplay) {
    const u64 epoch = WorldLogEpoch(0x77, 1);
    const auto record = [](s64 at, std::string_view role, std::string_view op,
                           std::span<const u8> payload) {
        return EncodeTraceRecord({at, "wlog-in", WorldLogTraceFields(role, op, payload)});
    };
    CoopTraceReplay replay;
    std::vector<std::string> out;
    for (const std::string& line :
         {record(1, "host", "start", EncodeWorldLogStart(epoch, false)),
          record(2, "host", "return-point", EncodeWorldLogReturnPointInput({2412951, 0x18010000})),
          record(3, "host", "return-point", std::vector<u8>{1, 2, 3})}) {
        for (std::string& decision : replay.Feed(line)) {
            out.push_back(std::move(decision));
        }
    }
    EXPECT_EQ(out, (std::vector<std::string>{
                       "wlog host start epoch 7700000001",
                       "wlog host appended seq 1 reset every map",
                       "wlog host appended seq 2 return point 2412951 on 0x18010000",
                       "wlog-in: unreadable host return-point",
                   }));
}

TEST(CoopTraceReplay, CheckedInTracesKeepTheirDecisions) {
    const fs::path dir{BLOODBORNE_TRACES};
    size_t traces = 0;
    for (const auto& entry : fs::directory_iterator(dir)) {
        if (entry.path().extension() != ".txt") {
            continue;
        }
        ++traces;
        fs::path expected = entry.path();
        expected.replace_extension(".expected");
        CoopTraceReplay replay;
        EXPECT_EQ(replay.FeedAll(Read(entry.path())), Lines(Read(expected)))
            << entry.path().filename().string();
    }
    EXPECT_GE(traces, 3u);
}

// Replays a player's trace: BLOODBORNE_REPLAY_TRACE=<coop-trace file> prints each decision after
// its record's UTC ms (see docs/WORLD_SYNC.md, Stage B).
TEST(CoopTraceReplay, ReplaysTheTraceNamedInTheEnvironment) {
    const char* path = std::getenv("BLOODBORNE_REPLAY_TRACE");
    if (path == nullptr || *path == '\0') {
        GTEST_SKIP() << "BLOODBORNE_REPLAY_TRACE is not set";
    }
    ASSERT_TRUE(fs::exists(path)) << path;
    CoopTraceReplay replay;
    for (const std::string& decision : replay.FeedAll(Read(path), true)) {
        std::cout << decision << '\n';
    }
}

TEST(NativePacketTrace, OffUnlessAskedFor) {
    EXPECT_EQ(ParsePacketTraceMode(nullptr), PacketTraceMode::Off);
    EXPECT_EQ(ParsePacketTraceMode(""), PacketTraceMode::Off);
    EXPECT_EQ(ParsePacketTraceMode("0"), PacketTraceMode::Off);
    EXPECT_EQ(ParsePacketTraceMode("off"), PacketTraceMode::Off);
    EXPECT_EQ(ParsePacketTraceMode("1"), PacketTraceMode::On);
    EXPECT_EQ(ParsePacketTraceMode("on"), PacketTraceMode::On);
    EXPECT_EQ(ParsePacketTraceMode("full"), PacketTraceMode::Full);
    EXPECT_EQ(ParsePacketTraceMode("loud"), PacketTraceMode::Off);
}

TEST(NativePacketTrace, OnlyTheUnknownChannelsCarryPayloadsUnlessFull) {
    for (const u32 channel : {0x29u, 0x34u, 0x3Du, 0x41u, 0x43u}) {
        EXPECT_TRUE(NativePayloadUnknown(channel)) << channel;
        EXPECT_EQ(TracedPayloadBytes(PacketTraceMode::On, channel, 0x64), 0x64u);
    }
    for (const u32 channel : {0x04u, 0x06u, 0x10u, 0x14u, 0x15u, 0x42u}) {
        EXPECT_FALSE(NativePayloadUnknown(channel)) << channel;
        EXPECT_EQ(TracedPayloadBytes(PacketTraceMode::On, channel, 0x64), 0u);
        EXPECT_EQ(TracedPayloadBytes(PacketTraceMode::Full, channel, 0x64), 0x64u);
    }
    EXPECT_EQ(TracedPayloadBytes(PacketTraceMode::Full, 0x11, 0x6000), NativePayloadTraceCap);
    EXPECT_EQ(TracedPayloadBytes(PacketTraceMode::On, 0x34, 0x6000), NativePayloadTraceCap);
    EXPECT_EQ(TracedPayloadBytes(PacketTraceMode::Off, 0x34, 0x64), 0u);
    EXPECT_TRUE(IsReliableNativeChannel(7));
    EXPECT_FALSE(IsReliableNativeChannel(6));
}

TEST(NativePacketTrace, PacketsDecodeOnlyWhole) {
    const std::vector<u8> packet{0x15, 0, 0, 0, 3, 0, 0, 0, 0xAA, 0xBB, 0xCC};
    const auto decoded = DecodeNativePacket(packet);
    ASSERT_TRUE(decoded);
    EXPECT_EQ(decoded->channel, 0x15u);
    EXPECT_EQ(std::vector<u8>(decoded->payload.begin(), decoded->payload.end()),
              (std::vector<u8>{0xAA, 0xBB, 0xCC}));
    // Short of its payload, short of a header, or a channel the game does not have.
    EXPECT_FALSE(DecodeNativePacket(std::span<const u8>{packet}.first(10)));
    EXPECT_FALSE(DecodeNativePacket(std::span<const u8>{packet}.first(7)));
    std::vector<u8> bad = packet;
    bad[0] = 0x44;
    EXPECT_FALSE(DecodeNativePacket(bad));
    // A size past the game's buffer.
    bad = packet;
    bad[5] = 0x64;
    EXPECT_FALSE(DecodeNativePacket(bad));
}

TEST(NativePacketTrace, OnlineIdsAreWholeNames) {
    const auto bytes = [](std::string_view text) {
        return std::vector<u8>(text.begin(), text.end());
    };
    EXPECT_EQ(NpOnlineIdAt(bytes(std::string{"rig-host_1\0\0\0garbage", 21})),
              std::optional<std::string>{"rig-host_1"});
    EXPECT_EQ(NpOnlineIdAt(bytes(std::string{"ab\0", 3})), std::nullopt);       // too short
    EXPECT_EQ(NpOnlineIdAt(bytes("abcdefghijklmnopq")), std::nullopt);          // no NUL
    EXPECT_EQ(NpOnlineIdAt(bytes(std::string{"bad name\0", 9})), std::nullopt); // a space
    EXPECT_EQ(NpOnlineIdAt(bytes(std::string{"abcdefghijklmnop\0", 17})),
              std::optional<std::string>{"abcdefghijklmnop"});
    EXPECT_EQ(NpOnlineIdAt({}), std::nullopt);
}

TEST(NativePacketTrace, RecordsRoundTrip) {
    const NativePacketRecord packet{false,      0x34, 0x20, "rig-guest",
                                    0x18000000, 4,    6,    std::vector<u8>{0x01, 0xFE}};
    const TraceRecord record{1760000000000, NativePacketTraceKind(packet.sent),
                             NativePacketFields(packet)};
    EXPECT_EQ(EncodeTraceRecord(record),
              "1760000000000 net-in ch=34 size=32 peer=rig-guest map=18000000 sync=4 match=6 "
              "payload=01fe");
    EXPECT_EQ(ParseNativePacketRecord(*ParseTraceRecord(EncodeTraceRecord(record))), packet);
    // No payload, no field; a sent one.
    NativePacketRecord quiet = packet;
    quiet.sent = true;
    quiet.payload.clear();
    const TraceRecord out{1, NativePacketTraceKind(true), NativePacketFields(quiet)};
    EXPECT_EQ(EncodeTraceRecord(out),
              "1 net-out ch=34 size=32 peer=rig-guest map=18000000 sync=4 match=6");
    EXPECT_EQ(ParseNativePacketRecord(out), quiet);
    TraceRecord broken = record;
    broken.fields.front().second = "zz";
    EXPECT_EQ(ParseNativePacketRecord(broken), std::nullopt);
    broken = record;
    broken.kind = "place";
    EXPECT_EQ(ParseNativePacketRecord(broken), std::nullopt);
}

TEST(CoopTraceReplay, NativePacketsMakeNoDecisions) {
    CoopTraceReplay replay;
    EXPECT_TRUE(replay
                    .FeedAll("1 net-out ch=06 size=120 peer=rig-guest map=18000000 sync=4 "
                             "match=3\n2 net-in ch=34 size=2 peer=rig-guest map=18000000 sync=4 "
                             "match=3 payload=0102\n")
                    .empty());
}
