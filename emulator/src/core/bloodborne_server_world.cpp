// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

// The party's world on the server (docs/SERVER_COOP_PLAN.md phase 3): with
// SHADPS4_BLOODBORNE_SERVER_WORLD=1 (the port only, and with the server link) shadNet keeps the
// party's world log, not the host's game. Every member, the host too, proposes to it and follows
// its log as a WorldLogMember (server/coop/world_log.h). Step 3a: the kills (the kill board is not
// used then).
//
// The messages (server/coop/coop.proto) go in the server link's Coop datagrams, in their reliable
// stream (server/coop/coop_stream.h). A thread of its own runs the stream (receives, resends,
// acks) every StreamTickMs; the game's thread only queues messages and takes what came (the kill
// step). No hook on the game's per-frame network update (phase 2: a hook there stalled joins).

#include "core/bloodborne_re_internal.h"

#include <deque>
#include <mutex>
#include <optional>
#include <random>
#include <thread>

#include <fmt/format.h>
#include "common/logging/log.h"
#include "coop/coop_messages.h"
#include "coop/coop_stream.h"
#include "core/libraries/network/server_link.h"

namespace Core::Bloodborne {
namespace {

constexpr s64 StreamTickMs = 20;
// A member's standing kills go again this often while they stay the same (a restart of the
// server drops what was in flight).
constexpr s64 KillsResendMs = 2'000;

const bool server_world_switch = [] {
#ifdef BLOODBORNE_NATIVE_PORT
    const char* env = std::getenv("SHADPS4_BLOODBORNE_SERVER_WORLD");
    const bool on = env != nullptr && std::string_view{env} == "1";
    if (on && !Libraries::Net::ServerLinkSwitchOn()) {
        LOG_ERROR(Debug, "Co-op server world: SHADPS4_BLOODBORNE_SERVER_WORLD needs "
                         "SHADPS4_BLOODBORNE_SERVER_LINK=1; the host's game keeps the world");
        return false;
    }
    return on;
#else
    return false; // the port only (docs/SERVER_COOP_PLAN.md)
#endif
}();

const u32 run = [] {
    std::random_device random;
    return static_cast<u32>(random()) | 1u;
}();

// The stream and its queues: the world thread and the game thread.
std::mutex stream_mutex;
std::optional<CoopStream> stream;
std::deque<bbcoop::Envelope> inbox;
std::deque<bbcoop::Envelope> outbox;
std::string hello_party;
std::string hello_member;
bool hello_due = true;
std::once_flag thread_started;

std::string PartyKey() {
    const char* setting = std::getenv("SHADPS4_BLOODBORNE_PARTY");
    return setting != nullptr ? ParsePartySetting(setting).value_or("") : "";
}

void Queue(bbcoop::Envelope envelope) {
    std::scoped_lock lock{stream_mutex};
    outbox.push_back(std::move(envelope));
}

void StreamTick(s64 now_ms) {
    std::vector<std::vector<u8>> received;
    std::array<u8, 4096> buffer{};
    for (int count = 0; count < 512; ++count) {
        const int size =
            Libraries::Net::ServerCoopReceive(buffer.data(), static_cast<u32>(buffer.size()));
        if (size < 0) {
            break;
        }
        received.emplace_back(buffer.begin(), buffer.begin() + size);
    }
    std::vector<std::vector<u8>> out;
    bool restarted = false;
    {
        std::scoped_lock lock{stream_mutex};
        for (const std::vector<u8>& body : received) {
            for (const std::vector<u8>& message : stream->Receive(body, now_ms)) {
                if (auto envelope = DecodeEnvelope(message)) {
                    inbox.push_back(std::move(*envelope));
                }
            }
        }
        if (stream->TakeRestarted()) {
            restarted = true;
            hello_due = true;
        }
        if (hello_due && !hello_party.empty() && !hello_member.empty()) {
            hello_due = false;
            bbcoop::Envelope hello;
            hello.mutable_hello()->set_protocol(CoopProtocol);
            hello.mutable_hello()->set_party(hello_party);
            hello.mutable_hello()->set_member(hello_member);
            hello.mutable_hello()->set_run(run);
            stream->Send(EncodeEnvelope(hello), now_ms);
        }
        if (!hello_due) {
            for (const bbcoop::Envelope& envelope : outbox) {
                stream->Send(EncodeEnvelope(envelope), now_ms);
            }
            outbox.clear();
        }
        out = stream->Flush(now_ms);
    }
    if (restarted) {
        LOG_WARNING(Debug, "Co-op server world: shadNet started over (or lost this game): "
                           "saying hello again");
        Trace("srvworld", {{"event", "restarted"}});
    }
    for (const std::vector<u8>& body : out) {
        Libraries::Net::ServerCoopSend(body.data(), static_cast<u32>(body.size()));
    }
}

void StartThread() {
    std::call_once(thread_started, [] {
        {
            std::scoped_lock lock{stream_mutex};
            stream.emplace(LocalOnlineId(), run);
        }
        std::thread{[] {
            while (true) {
                StreamTick(SteadyMilliseconds());
                std::this_thread::sleep_for(std::chrono::milliseconds{StreamTickMs});
            }
        }}.detach();
        LOG_INFO(Debug, "Co-op server world: on, shadNet keeps the party's world (run {:#x})", run);
    });
}

// The game thread's copy of the party's log.
WorldLogMember member;
std::atomic<bool> member_reset{false};
WorldLogKills last_proposed;
u32 proposals = 0;
s64 proposed_ms = 0;

void SendLifecycle(bbcoop::Lifecycle::Event event) {
    if (!server_world_switch) {
        return;
    }
    bbcoop::Envelope envelope;
    envelope.mutable_lifecycle()->set_event(event);
    envelope.mutable_lifecycle()->set_host(!IsPartyGuestRole());
    Queue(std::move(envelope));
    LOG_INFO(Debug, "Co-op server world: told shadNet this member's {} ({})",
             bbcoop::Lifecycle::Event_Name(event), IsPartyGuestRole() ? "a guest" : "the host");
}

// This member's session events, for the party's world: a wipe starts its log over once for the
// party; the host's join, leave or new character starts a new one, a guest's takes its share out.
// A new character out of a session is no event of the party's.
[[maybe_unused]] const bool wipe_registered = RegisterLifecycleReset(
    "server world (wipe)", {Lifecycle::Wipe}, [] { SendLifecycle(bbcoop::Lifecycle::WIPE); });
[[maybe_unused]] const bool join_registered =
    RegisterLifecycleReset("server world (join)", {Lifecycle::Join}, [] {
        member_reset.store(true);
        SendLifecycle(bbcoop::Lifecycle::JOIN);
    });
[[maybe_unused]] const bool leave_registered =
    RegisterLifecycleReset("server world (leave)", {Lifecycle::Leave}, [] {
        member_reset.store(true);
        SendLifecycle(bbcoop::Lifecycle::LEAVE);
    });
[[maybe_unused]] const bool new_character_registered =
    RegisterLifecycleReset("server world (new character)", {Lifecycle::NewCharacter}, [] {
        member_reset.store(true);
        if (InPartySession()) {
            SendLifecycle(bbcoop::Lifecycle::NEW_CHARACTER);
        }
    });

std::string EntriesText(std::span<const WorldLogEntry> entries) {
    std::string text;
    for (size_t index = 0; index < entries.size() && index < 8; ++index) {
        text += (text.empty() ? "" : ", ") + FormatWorldLogEntry(entries[index]);
    }
    return entries.size() > 8 ? fmt::format("{}, ... ({} in all)", text, entries.size()) : text;
}

void LogStep(const WorldLogMember::Step& step, std::string_view what) {
    if (step.new_epoch) {
        LOG_INFO(Debug, "Co-op server world: following the party's log {:#x}", member.Epoch());
    }
    if (step.snapshot) {
        LOG_INFO(Debug, "Co-op server world: took the party's world to seq {} ({} kills)",
                 member.Applied(), WorldLogKillCount(member.Kills()));
    }
    if (!step.applied.empty()) {
        LOG_INFO(Debug, "Co-op server world: applied {}", EntriesText(step.applied));
    }
    if (step.stale || step.new_epoch || step.snapshot || step.gap || !step.applied.empty()) {
        Trace("srvworld", {{"event", std::string{what}},
                           {"epoch", fmt::format("{:#x}", member.Epoch())},
                           {"seq", std::to_string(member.Applied())},
                           {"applied", std::to_string(step.applied.size())},
                           {"gap", step.gap ? "1" : "0"},
                           {"stale", step.stale ? "1" : "0"}});
    }
}

// What came from the server, on the game thread: whether the party's log started over after a
// wipe in it.
bool TakeInbox(s64 now_ms) {
    std::deque<bbcoop::Envelope> taken;
    {
        std::scoped_lock lock{stream_mutex};
        taken.swap(inbox);
    }
    bool after_wipe = false;
    const auto took = [&](const WorldLogMember::Step& step, std::string_view what) {
        after_wipe = after_wipe || step.after_wipe || StartsAfterAWipe(step.applied);
        LogStep(step, what);
    };
    for (const bbcoop::Envelope& envelope : taken) {
        if (envelope.has_welcome()) {
            const bbcoop::Welcome& welcome = envelope.welcome();
            if (welcome.accepted()) {
                LOG_INFO(Debug,
                         "Co-op server world: shadNet took this member (log {:#x} at seq {})",
                         welcome.epoch(), welcome.last());
            } else {
                LOG_ERROR(Debug, "Co-op server world: shadNet refused this member: {}",
                          welcome.text());
            }
            Trace("srvworld", {{"event", welcome.accepted() ? "welcome" : "refused"},
                               {"text", welcome.text()}});
        } else if (envelope.has_entries()) {
            took(member.Take(BatchOf(envelope.entries()), now_ms), "entries");
        } else if (envelope.has_snapshot()) {
            const SnapshotParts parts = PartsOf(envelope.snapshot());
            member.Take(parts.kills, now_ms);
            member.Take(parts.shown, now_ms);
            member.Take(parts.map_state, now_ms);
            member.Take(parts.hp, now_ms);
            took(member.Take(parts.flags, now_ms), "snapshot");
        }
    }
    return after_wipe;
}

} // namespace

namespace Coop {

bool ServerWorldOn() {
    return server_world_switch;
}

ServerWorldKills TickServerWorld(const WorldLogKills& mine, bool propose, s64 now_ms) {
    StartThread();
    {
        std::scoped_lock lock{stream_mutex};
        hello_party = PartyKey();
        std::scoped_lock vitals{vitals_mutex};
        hello_member = vitals_member;
    }
    if (member_reset.exchange(false)) {
        member.Reset();
        last_proposed.clear();
        proposed_ms = 0;
    }
    ServerWorldKills world;
    world.host_wiped = TakeInbox(now_ms);
    if (propose && (mine != last_proposed || now_ms - proposed_ms >= KillsResendMs)) {
        last_proposed = mine;
        proposed_ms = now_ms;
        bbcoop::Envelope envelope;
        *envelope.mutable_kills() =
            ToMessage(WorldLogProposal{{}, member.Epoch(), run, ++proposals, mine});
        Queue(std::move(envelope));
    }
    if (member.SnapshotDue(now_ms)) {
        bbcoop::Envelope envelope;
        envelope.mutable_snapshot_request()->set_epoch(member.Epoch());
        envelope.mutable_snapshot_request()->set_seq(member.Applied());
        Queue(std::move(envelope));
        LOG_INFO(Debug,
                 "Co-op server world: asked shadNet for the party's world (log {:#x}, "
                 "seq {})",
                 member.Epoch(), member.Applied());
    }
    world.kills = WorldLogHandles(member.Kills());
    world.fresh = member.Fresh(now_ms);
    return world;
}

} // namespace Coop

} // namespace Core::Bloodborne
