// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "coop_trace_replay.h"

#include <algorithm>
#include <charconv>
#include <optional>

#include <fmt/format.h>

namespace Core::Bloodborne {

namespace {

std::optional<u64> Hex(std::string_view text) {
    u64 value = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value, 16);
    if (error != std::errc{} || end != text.data() + text.size()) {
        return std::nullopt;
    }
    return value;
}

std::optional<s64> Number(std::string_view text) {
    s64 value = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || end != text.data() + text.size()) {
        return std::nullopt;
    }
    return value;
}

std::string Handles(const std::vector<u32>& handles) {
    std::string text;
    for (const u32 handle : handles) {
        text += fmt::format("{}{:#x}", text.empty() ? "" : ", ", handle);
    }
    return text;
}

std::string Value(const std::map<std::string, std::string>& values, const std::string& key) {
    const auto it = values.find(key);
    return it != values.end() ? it->second : std::string{};
}

std::vector<std::string> Split(std::string_view text, char separator) {
    std::vector<std::string> parts;
    while (!text.empty()) {
        const size_t end = text.find(separator);
        parts.emplace_back(text.substr(0, end));
        if (end == std::string_view::npos) {
            break;
        }
        text.remove_prefix(end + 1);
    }
    return parts;
}

} // namespace

bool CoopTraceReplay::Inputs::Take(const TraceRecord& record,
                                   const std::set<std::string>& list_keys) {
    for (const auto& [key, value] : record.fields) {
        if (list_keys.contains(key)) {
            if (!ApplyTraceListDelta(lists[key], value)) {
                return false;
            }
        } else {
            values[key] = value;
        }
    }
    return true;
}

std::vector<std::string> CoopTraceReplay::Feed(std::string_view line) {
    std::vector<std::string> out;
    const auto record = ParseTraceRecord(line);
    if (!record) {
        if (!line.empty()) {
            out.push_back(fmt::format("unreadable record: {}", line.substr(0, 80)));
        }
        return out;
    }
    if (record->kind == "life") {
        out.push_back(fmt::format("life {}", record->Field("event").value_or("?")));
    } else if (record->kind == "kills-in") {
        Kills(*record, out);
    } else if (record->kind == "hp-in") {
        Hp(*record, out);
    } else if (record->kind == "shown-in") {
        Shown(*record, out);
    } else if (record->kind == "kill-apply") {
        KillApply(*record, out);
    } else if (record->kind == "p2p-in") {
        FrameIn(*record, out);
    } else if (record->kind == "board-in") {
        BoardIn(*record, out);
    } else if (record->kind == "trace-dropped") {
        out.push_back(fmt::format("trace dropped {} lines", record->Field("count").value_or("?")));
    }
    return out;
}

std::vector<std::string> CoopTraceReplay::FeedAll(std::string_view text, bool stamped) {
    std::vector<std::string> out;
    for (std::string line : Split(text, '\n')) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.empty() || line.front() == '#') {
            continue; // a test trace's notes
        }
        const auto record = ParseTraceRecord(line);
        for (std::string& decision : Feed(line)) {
            out.push_back(stamped && record ? fmt::format("{} {}", record->utc_ms, decision)
                                            : std::move(decision));
        }
    }
    return out;
}

// VisitKills: a death that resets the map (forget) first, then the characters seen.
void CoopTraceReplay::Kills(const TraceRecord& record, std::vector<std::string>& out) {
    if (!kills_in.Take(record, {"chars"})) {
        out.push_back("kills-in: unreadable list");
        return;
    }
    const u64 visit = Hex(Value(kills_in.values, "visit")).value_or(0);
    const auto others =
        ParseTraceHandles(Value(kills_in.values, "others")).value_or(std::vector<u32>{});
    std::vector<SeenCharacter> characters;
    for (const std::string& token : kills_in.lists["chars"]) {
        if (const auto character = ParseSeenToken(token)) {
            characters.push_back(*character);
        }
    }
    std::vector<std::pair<s32, bool>> changes;
    if (record.Field("forget") == "1") {
        changes = visit_kills.Forget();
    }
    const auto observed = visit_kills.Observe(visit, characters, others);
    changes.insert(changes.end(), observed.begin(), observed.end());
    for (const auto& [handle, killed] : changes) {
        out.push_back(fmt::format("kill {} {:#x}", killed ? "posted" : "withdrawn",
                                  static_cast<u32>(handle)));
    }
    visit_maps.insert(static_cast<u32>(visit >> 32));
    for (const u32 map : visit_maps) {
        const std::vector<u32> now = visit_kills.Held(map);
        if (now != held[map]) {
            out.push_back(now.empty()
                              ? fmt::format("no kills held on map {:#x}", map)
                              : fmt::format("kills held on map {:#x}: {}", map, Handles(now)));
            held[map] = now;
        }
    }
}

// SharedDamage (worlds apart), or a guest's copies lowered to the host's HP (one world).
void CoopTraceReplay::Hp(const TraceRecord& record, std::vector<std::string>& out) {
    if (!hp_in.Take(record, {"alive", "reports"})) {
        out.push_back("hp-in: unreadable list");
        return;
    }
    const u64 visit = Hex(Value(hp_in.values, "visit")).value_or(0);
    const std::string mode = Value(hp_in.values, "mode");
    const s64 now = Number(Value(hp_in.values, "now")).value_or(0);
    std::vector<EnemyCopy> alive;
    for (const std::string& token : hp_in.lists["alive"]) {
        if (const auto copy = ParseCopyToken(token)) {
            alive.push_back(*copy);
        }
    }
    std::vector<EnemyHpReport> reports;
    for (const std::string& token : hp_in.lists["reports"]) {
        if (const auto report = ParseReportToken(token)) {
            reports.push_back(*report);
        }
    }
    if (record.Field("forget") == "1") {
        damage.ForgetDamageBefore(reports);
        out.push_back("hp: damage reported before the reset is left behind");
    }
    SharedDamage::Step step = damage.Update(visit, mode == "apart", alive, reports, now);
    if (mode == "one-guest") {
        step.writes = LowerToHostHp(alive, reports);
    }
    for (const EnemyWrite& write : step.writes) {
        out.push_back(fmt::format("hp {:#x} -> {}", write.handle, write.hp));
    }
    if (step.post != posted) {
        std::string which;
        for (const EnemyHp& enemy : step.post) {
            which += fmt::format(" {:#x}:{}", enemy.handle, enemy.hp);
        }
        out.push_back(fmt::format("hp posts{}", which.empty() ? " none" : which));
        posted = step.post;
    }
}

void CoopTraceReplay::Shown(const TraceRecord& record, std::vector<std::string>& out) {
    shown_in.Take(record, {});
    const auto bytes_of = [&](const char* key) {
        return ParseTraceHex(Value(shown_in.values, key)).value_or(std::vector<u8>{});
    };
    const auto host = DecodeShownCharacters(bytes_of("host"));
    const auto mine = DecodeShownCharacters(bytes_of("mine"));
    if (!host || !mine) {
        out.push_back("shown-in: unreadable characters");
        return;
    }
    const auto set_of = [&](const char* key) {
        const auto handles =
            ParseTraceHandles(Value(shown_in.values, key)).value_or(std::vector<u32>{});
        return std::unordered_set<u32>(handles.begin(), handles.end());
    };
    for (const CharacterShown& change :
         ShownLikeTheHost(*host, *mine, set_of("kill"), set_of("here"))) {
        out.push_back(fmt::format("{} {:#x}", change.hidden ? "hide" : "show", change.handle));
    }
}

void CoopTraceReplay::KillApply(const TraceRecord& record, std::vector<std::string>& out) {
    for (const std::string& candidate : Split(record.Field("cands").value_or(""), ',')) {
        const size_t colon = candidate.find(':');
        const auto handle = Hex(std::string_view{candidate}.substr(0, colon));
        const auto hp = colon == std::string::npos
                            ? std::nullopt
                            : Number(std::string_view{candidate}.substr(colon + 1));
        if (!handle || !hp) {
            out.push_back(fmt::format("kill-apply: unreadable {}", candidate));
            continue;
        }
        out.push_back(AppliesMemberKill(true, static_cast<s32>(*hp))
                          ? fmt::format("member kill {:#x} applied", *handle)
                          : fmt::format("member kill {:#x} not applied (hp {})", *handle, *hp));
    }
}

// The peers' standing kills (DirectKillsType), when a member's set changes.
void CoopTraceReplay::FrameIn(const TraceRecord& record, std::vector<std::string>& out) {
    if (record.Field("type") != fmt::format("{:02x}", DirectKillsType) ||
        record.Field("from-peer") == "0") {
        return;
    }
    const auto payload = ParseTraceHex(record.Field("payload").value_or(""));
    const auto kills = payload ? DecodeDirectKills(*payload) : std::nullopt;
    if (!kills) {
        out.push_back("p2p-in: unreadable kills frame");
        return;
    }
    std::vector<u32>& before = peer_kills[kills->member];
    if (kills->handles != before) {
        out.push_back(fmt::format("peer kills {}: {}", kills->member,
                                  kills->handles.empty() ? "none" : Handles(kills->handles)));
        before = kills->handles;
    }
}

// The kill board's answers (PartyKills), when the standing kills change.
void CoopTraceReplay::BoardIn(const TraceRecord& record, std::vector<std::string>& out) {
    if (record.Field("endpoint") != "/bloodborne/party/kills" || record.Field("same") == "1") {
        return;
    }
    const std::string body = record.Field("body").value_or("");
    if (std::to_string(body.size()) != record.Field("size").value_or("")) {
        out.push_back("board-in: kill board answer cut short in the trace");
        return;
    }
    const auto answer = ParseKillBoard(body);
    if (!answer) {
        return;
    }
    party_kills.Apply(answer->changes);
    const auto all = party_kills.All();
    std::set<u32> now(all.begin(), all.end());
    if (now != board_kills) {
        out.push_back(
            fmt::format("board kills: {}",
                        now.empty() ? "none" : Handles(std::vector<u32>(now.begin(), now.end()))));
        board_kills = std::move(now);
    }
}

} // namespace Core::Bloodborne
