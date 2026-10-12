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
    if (record->kind == "place") {
        const auto change = ParsePlaceFields(*record);
        out.push_back(change ? FormatPlaceChange(*change) : "place: unreadable record");
    } else if (record->kind == "life") {
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
    } else if (record->kind == "wlog-in") {
        WorldLogIn(*record, out);
    } else if (record->kind == "wlog-path") {
        const auto what = record->Field("what");
        out.push_back(fmt::format("world log path{}: {} ({})", what ? " (" + *what + ")" : "",
                                  record->Field("path").value_or("?"),
                                  record->Field("why").value_or("?")));
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
    // The role matters at a switch between one world and worlds apart, which passes through a
    // one-world mode that names it.
    if (mode == "one-guest" || mode == "one-host") {
        hp_role = mode == "one-guest" ? PartyRole::Guest : PartyRole::Host;
    }
    SharedDamage::Step step =
        damage.Update(mode == "off" ? 0 : visit, mode == "apart", alive, reports, now, hp_role);
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

// The world log's inputs (WorldLogTraceFields): the host's starts and proposals, a member's
// batches, snapshots and resets.
void CoopTraceReplay::WorldLogIn(const TraceRecord& record, std::vector<std::string>& out) {
    const std::string role = record.Field("role").value_or("");
    const std::string op = record.Field("op").value_or("");
    const auto payload = ParseTraceHex(record.Field("payload").value_or(""));
    if (!payload) {
        out.push_back(fmt::format("wlog-in: unreadable {} {}", role, op));
    } else if (role == "host") {
        WorldLogHostIn(op, *payload, record.utc_ms, out);
    } else {
        WorldLogMemberIn(op, *payload, record.utc_ms, out);
    }
}

void CoopTraceReplay::WorldLogHostIn(std::string_view op, std::span<const u8> payload, s64 at,
                                     std::vector<std::string>& out) {
    std::vector<WorldLogEntry> entries;
    if (const auto start = op == "start" ? DecodeWorldLogStart(payload) : std::nullopt) {
        out.push_back(fmt::format("wlog host start epoch {:x}{}", start->first,
                                  start->second ? " after a wipe" : ""));
        entries = wlog_host.Start(start->first, start->second);
    } else if (const auto proposal =
                   op == "propose" ? DecodeWorldLogProposal(payload) : std::nullopt) {
        WorldLogHost::Taken taken = wlog_host.Propose(*proposal);
        if (taken.stale) {
            out.push_back(
                fmt::format("wlog host dropped a late proposal from {}", proposal->member));
        }
        entries = std::move(taken.entries);
    } else if (const auto show = op == "show" ? DecodeWorldLogShow(payload) : std::nullopt) {
        entries = wlog_host.Show(show->first, show->second);
    } else if (const auto flags =
                   op == "propose-flags" ? DecodeWorldLogFlagProposal(payload) : std::nullopt) {
        WorldLogHost::Taken taken = wlog_host.ProposeFlags(*flags, at);
        if (taken.stale) {
            out.push_back(
                fmt::format("wlog host dropped flags of another log from {}", flags->member));
        }
        entries = std::move(taken.entries);
    } else if (const auto state =
                   op == "map-state" ? DecodeWorldLogMapStateInput(payload) : std::nullopt) {
        entries = wlog_host.MapState(state->map, state->bits);
    } else if (const auto point =
                   op == "return-point" ? DecodeWorldLogReturnPointInput(payload) : std::nullopt) {
        entries = wlog_host.ReturnPoint(point->point, point->map);
    } else if (const auto hp =
                   op == "propose-hp" ? DecodeWorldLogHpProposal(payload) : std::nullopt) {
        WorldLogHost::Taken taken = wlog_host.ProposeHp(*hp);
        if (taken.stale) {
            out.push_back(fmt::format("wlog host dropped a late HP report from {}", hp->member));
        }
        entries = std::move(taken.entries);
    } else {
        out.push_back(fmt::format("wlog-in: unreadable host {}", op));
    }
    for (const WorldLogEntry& entry : entries) {
        out.push_back("wlog host appended " + FormatWorldLogEntry(entry));
    }
}

void CoopTraceReplay::WorldLogMemberIn(std::string_view op, std::span<const u8> payload, s64 at,
                                       std::vector<std::string>& out) {
    WorldLogMember::Step step;
    u64 host_last = 0;
    if (op == "reset") {
        wlog_member.Reset();
        out.push_back("wlog member reset");
        return;
    }
    if (const auto batch = op == "entries" ? DecodeWorldLogBatch(payload) : std::nullopt) {
        step = wlog_member.Take(*batch, at);
        host_last = batch->last;
    } else if (const auto part =
                   op == "snapshot" ? DecodeWorldLogSnapshot(payload) : std::nullopt) {
        step = wlog_member.Take(*part, at);
        host_last = part->last;
    } else if (const auto shown =
                   op == "shown-snapshot" ? DecodeWorldLogShownSnapshot(payload) : std::nullopt) {
        step = wlog_member.Take(*shown, at);
        host_last = shown->last;
    } else if (const auto flags =
                   op == "flags-snapshot" ? DecodeWorldLogFlagsSnapshot(payload) : std::nullopt) {
        step = wlog_member.Take(*flags, at);
        host_last = flags->last;
    } else if (const auto hp =
                   op == "hp-snapshot" ? DecodeWorldLogHpSnapshot(payload) : std::nullopt) {
        step = wlog_member.Take(*hp, at);
        host_last = hp->last;
    } else if (const auto state = op == "map-state-snapshot"
                                      ? DecodeWorldLogMapStateSnapshot(payload)
                                      : std::nullopt) {
        step = wlog_member.Take(*state, at);
        host_last = state->last;
    } else {
        out.push_back(fmt::format("wlog-in: unreadable member {}", op));
        return;
    }
    if (step.stale) {
        out.push_back("wlog member dropped an older log's frame");
    }
    if (step.new_epoch) {
        out.push_back(fmt::format("wlog member follows epoch {:x}", wlog_member.Epoch()));
    }
    if (step.snapshot) {
        out.push_back(fmt::format("wlog member snapshot to seq {}", host_last));
        if (wlog_member.ShownKnown()) {
            out.push_back(fmt::format("wlog member shown on {:#x}: {} characters",
                                      wlog_member.Shown().map, wlog_member.Shown().bits.size()));
        }
        if (wlog_member.MapStateKnown()) {
            out.push_back(
                fmt::format("wlog member map state of {:#x}", wlog_member.MapState().map));
        }
    }
    for (const WorldLogEntry& entry : step.applied) {
        out.push_back("wlog member applied " + FormatWorldLogEntry(entry));
    }
    if (step.gap) {
        out.push_back(fmt::format("wlog member gap: has seq {}, host at {}", wlog_member.Applied(),
                                  host_last));
    }
}

} // namespace Core::Bloodborne
