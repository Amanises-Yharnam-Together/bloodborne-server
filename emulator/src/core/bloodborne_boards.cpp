// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

// The party boards' clients (rest, HP, vitals) and the direct P2P frames. Split from
// bloodborne_re.cpp; the shared declarations are in bloodborne_re_internal.h.

#include "core/bloodborne_crash_dump.h"
#include "core/bloodborne_party.h"
#include "core/bloodborne_portrait.h"
#include "core/bloodborne_re.h"
#include "core/bloodborne_re_internal.h"
#include "core/bloodborne_write_watch.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <charconv>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <map>
#include <mutex>
#include <optional>
#include <random>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>
#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#endif

#include <fmt/ranges.h>
#include "common/logging/log.h"
#include "common/memory_patcher.h"
#include "common/path_util.h"
#include "common/singleton.h"
#include "core/cpu_patches.h"
#include "core/debugger.h"
#include "core/emulator_settings.h"
#include "core/file_format/psf.h"
#include "core/libraries/network/sockets.h"
#include "core/libraries/np/np_handler.h"
#include "core/libraries/np/np_matching2/np_matching2_signaling.h"
#include "core/libraries/np/np_trophy.h"
#include "core/libraries/system/userservice.h"
#include "core/memory.h"

#include <httplib.h>

namespace Core::Bloodborne {
namespace Coop {

PartyKills party_kills; // kill_board_mutex

} // namespace Coop
namespace {

std::jthread rest_board_client;

// A post to one of the party's boards, traced (the request before it goes, the answer after).
httplib::Result PostToBoard(httplib::Client& client, const std::string& endpoint,
                            const httplib::Headers& headers, const std::string& body) {
    if (CoopTraceOn()) {
        TraceBoard("board-out", endpoint, std::nullopt, body);
    }
    auto response = client.Post(endpoint, headers, body, "text/plain");
    if (CoopTraceOn()) {
        if (response) {
            TraceBoard("board-in", endpoint, response->status, response->body);
        } else {
            Trace("board-in",
                  {{"endpoint", endpoint}, {"error", httplib::to_string(response.error())}});
        }
    }
    return response;
}

// This member's kill changes to the kill board (in the flag board's format) and the standing kills
// it answers with. A post after the kills changed path (kill_board_fresh_start) asks since
// FreshStart, so the board takes back what this member posted before; failed changes wait for the
// next post.
void PostKillBoard(httplib::Client& client, const httplib::Headers& headers,
                   const std::string& member, s64& since) {
    std::vector<std::pair<s32, bool>> kills_out;
    bool fresh = false;
    {
        std::scoped_lock lock{kill_board_mutex};
        kills_out.swap(kills_to_post);
        fresh = std::exchange(kill_board_fresh_start, false);
    }
    const s64 asked = fresh ? std::numeric_limits<s64>::max() : since;
    const auto response = PostToBoard(client, "/bloodborne/party/kills", headers,
                                      EncodeFlagUpdate(member, asked, kills_out));
    const auto kills =
        response && response->status == 200 ? ParseKillBoard(response->body) : std::nullopt;
    std::scoped_lock lock{kill_board_mutex};
    if (!kills) {
        kills_to_post.insert(kills_to_post.begin(), kills_out.begin(), kills_out.end());
        kill_board_fresh_start = kill_board_fresh_start || fresh;
        return;
    }
    since = kills->latest;
    if (asked == std::numeric_limits<s64>::max()) {
        party_kills.Forget(member); // taken back; what this post added is not in the answer
    }
    party_kills.Apply(kills->changes);
    others_kills = party_kills.All();
}

// The host posts its map's characters; a guest only reads (map 0).
void PostShownBoard(httplib::Client& client, const httplib::Headers& headers,
                    const std::string& member) {
    MemberMapState mine;
    {
        std::scoped_lock lock{shown_board_mutex};
        mine = local_shown;
    }
    const auto response = PostToBoard(client, "/bloodborne/party/shown", headers,
                                      EncodeMapStateUpdate(member, mine.map, mine.bits));
    if (response && response->status == 200) {
        const auto states = ParseMapStateBoard(response->body);
        std::scoped_lock lock{shown_board_mutex};
        host_shown = states && !states->empty() ? std::optional{states->front()} : std::nullopt;
    }
}

// The host's map state (see SnapshotHostMapState); a guest reads it.
void PostMapStateBoard(httplib::Client& client, const httplib::Headers& headers,
                       const std::string& member) {
    MemberMapState mine;
    {
        std::scoped_lock lock{map_state_mutex};
        mine = local_map_state;
    }
    const auto response = PostToBoard(client, "/bloodborne/party/mapstate", headers,
                                      EncodeMapStateUpdate(member, mine.map, mine.bits));
    if (const auto states =
            response && response->status == 200 ? ParseMapStateBoard(response->body) : std::nullopt;
        states && !states->empty()) {
        std::scoped_lock lock{map_state_mutex};
        host_map_state = states->front();
    }
}

void PostEchoBoard(httplib::Client& client, const httplib::Headers& headers,
                   const std::string& member, s64& since) {
    std::vector<KillEchoes> echoes_out;
    {
        std::scoped_lock lock{echo_board_mutex};
        echoes_out.swap(echoes_to_post);
    }
    const auto response = PostToBoard(client, "/bloodborne/party/echoes", headers,
                                      EncodeEchoUpdate(member, since, echoes_out));
    if (const auto echoes =
            response && response->status == 200 ? ParseEchoBoard(response->body) : std::nullopt) {
        since = echoes->latest;
        std::scoped_lock lock{echo_board_mutex};
        echo_ledger.Received(echoes->echoes, SteadyMilliseconds());
    } else {
        std::scoped_lock lock{echo_board_mutex};
        echoes_to_post.insert(echoes_to_post.begin(), echoes_out.begin(), echoes_out.end());
    }
}

// The lamps (FlagsToShare) this member saw lit, and the others'.
void PostFlagBoard(httplib::Client& client, const httplib::Headers& headers,
                   const std::string& member, s64& since) {
    std::vector<std::pair<s32, bool>> outgoing;
    {
        std::scoped_lock lock{flag_board_mutex};
        outgoing.swap(flags_to_post);
    }
    const auto response = PostToBoard(client, "/bloodborne/party/flags", headers,
                                      EncodeFlagUpdate(member, since, outgoing));
    const auto board =
        response && response->status == 200 ? ParseFlagBoard(response->body) : std::nullopt;
    std::scoped_lock lock{flag_board_mutex};
    if (!board) {
        flags_to_post.insert(flags_to_post.begin(), outgoing.begin(), outgoing.end());
        return;
    }
    since = board->latest;
    std::vector<std::pair<s32, bool>> changes = board->changes;
    FilterBoardFlags(changes);
    flags_to_set.insert(flags_to_set.end(), changes.begin(), changes.end());
}

// The world snapshot (POST /bloodborne/party/world): the host posts its log's state when the game
// thread made a new one (every WorldSnapshotPostMs); a member reads it while its copy is not fresh
// (a join, a restart, its link to the host down).
void PostWorldSnapshot(httplib::Client& client, const httplib::Headers& headers,
                       const std::string& member) {
    if (const auto blob = TakeWorldSnapshotToPost()) {
        PostToBoard(client, "/bloodborne/party/world", headers,
                    EncodeWorldSnapshotUpdate(member, *blob));
        return;
    }
    if (!WorldSnapshotWanted()) {
        return;
    }
    const auto response = PostToBoard(client, "/bloodborne/party/world", headers,
                                      EncodeWorldSnapshotUpdate(member, {}));
    if (const auto answer = response && response->status == 200
                                ? ParseWorldSnapshotAnswer(response->body)
                                : std::nullopt;
        answer && !answer->blob.empty()) {
        NoteWorldSnapshot(answer->age_ms, std::move(answer->blob));
    }
}

// Posts this member's rest about once a second while it rests (once more when it stands up) and
// keeps the board the server answers with. Runs on its own thread; the game never waits for it.
// The signed-in user's online id, or "" before shadNet's login.
std::string LocalOnlineId() {
    int user_id = -1;
    if (Libraries::UserService::sceUserServiceGetInitialUser(&user_id) != 0 || user_id < 0) {
        return {};
    }
    auto& np = Libraries::Np::NpHandler::GetInstance();
    if (!np.IsPsnSignedIn(user_id)) {
        return {};
    }
    const auto online_id = np.GetOnlineId(user_id);
    return std::string{online_id.data, strnlen(online_id.data, sizeof(online_id.data))};
}

// The boards know a member by its online id (hex, see BoardMemberOfOnlineId): a party of three
// tells its session members apart by it. Random if shadNet never signs in.
std::string FindBoardMember(std::stop_token stop) {
    std::string member;
    for (int attempt = 0; attempt < 120 && !stop.stop_requested() && member.empty(); ++attempt) {
        if (const std::string online_id = LocalOnlineId(); !online_id.empty()) {
            member = BoardMemberOfOnlineId(online_id);
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds{500});
        }
    }
    if (member.empty()) {
        std::array<u8, 8> random_bytes{};
        std::random_device random;
        for (auto& byte : random_bytes) {
            byte = static_cast<u8>(random());
        }
        member = BytesToHex(random_bytes);
    }
    return member;
}

void RunRestBoardClient(std::stop_token stop, std::string base_url, std::string party) {
    const std::string member = FindBoardMember(stop);
    httplib::Client client(base_url);
    client.set_connection_timeout(std::chrono::seconds{1});
    client.set_read_timeout(std::chrono::seconds{2});
    const httplib::Headers headers{{"X-ShadPS4-Bloodborne-Party", party}};
    LOG_INFO(Debug, "Bloodborne party rest board client for {} as member {}", base_url, member);

    std::mutex wait_mutex;
    std::condition_variable_any wait;
    s32 posted = NotResting;
    bool failing = false;
    auto next_trophy_post = std::chrono::steady_clock::now();
    auto next_echo_post = std::chrono::steady_clock::now();
    // The first post asks for nothing older than now: the board's history belongs to whatever
    // characters played before (a new character must not get it).
    s64 flag_board_since = std::numeric_limits<s64>::max();
    s64 echo_board_since = std::numeric_limits<s64>::max();
    s64 kill_board_since = std::numeric_limits<s64>::max();
    while (!stop.stop_requested()) {
        {
            std::unique_lock lock{wait_mutex};
            wait.wait_for(lock, stop, std::chrono::seconds{1}, [] { return false; });
        }
        if (const u32 map = party_map_now.load(); map != 0) {
            const auto map_response =
                PostToBoard(client, "/bloodborne/party/map", headers, EncodeMapUpdate(member, map));
            if (const auto maps = map_response && map_response->status == 200
                                      ? ParseMapBoard(map_response->body)
                                      : std::nullopt) {
                std::scoped_lock lock{map_board_mutex};
                map_board = *maps;
                map_board_member = member;
                map_board_time = std::chrono::steady_clock::now();
            }
        }
        // While the world log runs, the kills, shown characters, flags, map state and HP go over
        // the link and the host's world snapshot replaces their boards (see PostWorldSnapshot).
        const bool world_log = WorldLogCarriesBoards();
        if (!world_log) {
            PostShownBoard(client, headers, member);
        }
        // Kill echoes go both ways in and out of a session: a member on its own trip (a guest
        // in the Hunter's Dream) still gets the party's kills made apart.
        if (std::chrono::steady_clock::now() >= next_echo_post) {
            if (!world_log) {
                PostMapStateBoard(client, headers, member);
                PostKillBoard(client, headers, member, kill_board_since);
            }
            next_echo_post = std::chrono::steady_clock::now() + std::chrono::seconds{3};
            PostEchoBoard(client, headers, member, echo_board_since);
        }
        if (world_log) {
            PostWorldSnapshot(client, headers, member);
        }
        if (trophy_board_active.load() && std::chrono::steady_clock::now() >= next_trophy_post) {
            next_trophy_post = std::chrono::steady_clock::now() + std::chrono::seconds{3};
            std::vector<s32> earned;
            {
                std::scoped_lock lock{trophy_mutex};
                earned = trophies_earned_in_party;
            }
            const auto trophy_response = PostToBoard(client, "/bloodborne/party/trophy", headers,
                                                     EncodeTrophyUpdate(member, earned));
            if (const auto trophies = trophy_response && trophy_response->status == 200
                                          ? ParseTrophyBoard(trophy_response->body)
                                          : std::nullopt) {
                ReadTrophyBoard(*trophies);
            }
            if (!world_log) {
                PostFlagBoard(client, headers, member, flag_board_since);
            }
        }
        // In a session every member reads the board (to join a rest nearby); outside one only
        // a member that still has a rest on it posts, to take it off.
        const bool active = rest_board_active.load();
        const s32 lamp = active ? rest_board_lamp.load() : NotResting;
        if (!active && posted == NotResting) {
            continue;
        }
        std::optional<LampPosition> at;
        {
            std::scoped_lock lock{rest_board_position_mutex};
            at = rest_board_position;
        }
        const auto response = PostToBoard(client, "/bloodborne/party/rest", headers,
                                          EncodeLampRestUpdate(member, lamp, at));
        const auto board =
            response && response->status == 200 ? ParseLampRestBoard(response->body) : std::nullopt;
        if (!board) {
            if (!failing) {
                LOG_WARNING(Debug, "Bloodborne party rest board unreachable ({})",
                            response ? std::to_string(response->status)
                                     : httplib::to_string(response.error()));
            }
            failing = true;
            continue;
        }
        failing = false;
        posted = lamp;
        std::scoped_lock lock{rest_board_mutex};
        rest_board = *board;
        rest_board_time = std::chrono::steady_clock::now();
    }
}

} // namespace

namespace Coop {

// Enemy HP (see SharedDamage) on its own client, every HpBoardRound and at once when this
// member's own damage changes (see HpBoardWait): on the rest board's one-second round the other
// members' hits showed late (friend run 2026-10-10), and on a 200 ms round the host's two-hit
// kills were over before the guests' bars moved (forest run 2026-10-11).
std::condition_variable_any enemy_hp_wake;
bool enemy_hp_changed = false; // enemy_hp_mutex

} // namespace Coop

namespace {

void RunHpBoardClient(std::stop_token stop, std::string base_url, std::string party) {
    const std::string member = FindBoardMember(stop);
    httplib::Client client(base_url);
    client.set_keep_alive(true);
    client.set_connection_timeout(std::chrono::seconds{1});
    client.set_read_timeout(std::chrono::seconds{1});
    const httplib::Headers headers{{"X-ShadPS4-Bloodborne-Party", party}};
    while (!stop.stop_requested()) {
        MemberMapState mine;
        {
            std::unique_lock lock{enemy_hp_mutex};
            enemy_hp_wake.wait_for(lock, stop, HpBoardWait(enemy_hp_changed),
                                   [] { return enemy_hp_changed; });
            enemy_hp_changed = false;
            mine = local_enemy_hp;
        }
        if (mine.map == 0 || WorldLogCarriesBoards()) {
            continue; // the world log carries the HP (the world snapshot is its copy)
        }
        const auto hp_response = PostToBoard(client, "/bloodborne/party/hp", headers,
                                             EncodeMapStateUpdate(member, mine.map, mine.bits));
        if (const auto states = hp_response && hp_response->status == 200
                                    ? ParseMapStateBoard(hp_response->body)
                                    : std::nullopt) {
            std::scoped_lock lock{enemy_hp_mutex};
            others_enemy_hp = *states;
        } else {
            static auto last_logged = std::chrono::steady_clock::time_point{};
            if (std::chrono::steady_clock::now() - last_logged > std::chrono::seconds{10}) {
                last_logged = std::chrono::steady_clock::now();
                LOG_INFO(Debug, "Bloodborne party: HP board post failed (status {})",
                         hp_response ? hp_response->status : -1);
            }
        }
    }
}
std::jthread hp_board_client;
void StopBoardClientsAtExit();

} // namespace

namespace Coop {

void StartRestBoardClient() {
    const char* party_setting = std::getenv("SHADPS4_BLOODBORNE_PARTY");
    const auto party = party_setting ? ParsePartySetting(party_setting) : std::nullopt;
    const std::string base_url = EmulatorSettings.GetShadNetWebApiServer();
    if (!lamp_rest_hooks_installed || !party || !EmulatorSettings.IsShadNetEnabled() ||
        base_url.empty()) {
        return;
    }
    Libraries::Np::NpTrophy::SetTrophyUnlockObserver(NoteGameTrophyUnlock);
    rest_board_client = std::jthread(RunRestBoardClient, base_url, *party);
    hp_board_client = std::jthread(RunHpBoardClient, base_url, *party);
    StopBoardClientsAtExit();
}

// Which build each game runs (see PeerHelloType): this game's, from SetBuildIdentity; no hello
// goes out before it is set.
std::mutex peer_hello_mutex;
std::optional<PeerHello> own_hello;

} // namespace Coop

namespace {

PeerBuildCheck peer_build_check;

void NotePeerHello(PeerHello theirs, u32 addr, u16 port) {
    std::optional<std::string> warning;
    {
        std::scoped_lock lock{peer_hello_mutex};
        if (!own_hello) {
            return;
        }
        if (theirs.member.empty()) {
            theirs.member = fmt::format("peer {:08x}:{}", addr, port);
        }
        warning = peer_build_check.Take(theirs, *own_hello);
    }
    if (warning) {
        LOG_WARNING(Debug, "{}", *warning);
    }
}

} // namespace

namespace {

// Party portraits (bloodborne_portrait.h): the members' (from their frames, on the game thread) and
// the status page's captures (from the port's texture cache).
std::mutex portraits_mutex;
PartyPortraits party_portraits; // portraits_mutex
FaceCapture face_capture;       // portraits_mutex
PortraitAssembler portrait_assembler;

void TakePortraitPart(std::span<const u8> payload) {
    const auto part = DecodePortraitPart(payload);
    if (!part) {
        return;
    }
    auto pixels = portrait_assembler.Take(*part);
    if (!pixels) {
        return;
    }
    LOG_INFO(Debug, "Bloodborne party portraits: {}'s portrait came ({:08x})", part->member,
             part->id);
    std::scoped_lock lock{portraits_mutex};
    party_portraits.Take(part->member, std::move(*pixels));
}

// One co-op frame from a room peer (`bytes` whole, `payload` after the type); whether it brought
// what the enemy step uses (HP, kills, the world log), so that step runs on the next frame.
bool TakeCoopFrame(u8 type, std::span<const u8> payload, std::span<const u8> bytes, u32 addr,
                   u16 port) {
    const u64 sender = (u64{addr} << 16) | port;
    const s64 now = SteadyMilliseconds();
    if (type == DirectVitalsType) {
        if (auto vitals = ParseVitalsUpdate(std::string(payload.begin(), payload.end()))) {
            std::scoped_lock lock{direct_boards_mutex};
            direct_vitals_inbox.Take(sender, std::move(*vitals), now);
        }
        return false;
    }
    if (type == DirectKillsType) {
        auto kills = DecodeDirectKills(payload);
        if (kills) {
            std::scoped_lock lock{direct_boards_mutex};
            direct_kills_inbox.Take(sender, std::move(*kills), now);
        }
        return kills.has_value();
    }
    if (type == PortraitPartType) {
        TakePortraitPart(payload);
        return false;
    }
    if (type == PeerHelloType) {
        if (auto hello = DecodePeerHello(payload)) {
            NotePeerHello(std::move(*hello), addr, port);
        }
        return false;
    }
    if (IsWorldLogFrameType(type)) {
        TakeWorldLogFrame(type, payload, addr, port);
        return true;
    }
    auto state = DecodeDirectDamage(bytes);
    if (state) {
        direct_damage_inbox.Take(sender, std::move(*state), now);
    }
    return state.has_value();
}

} // namespace

namespace Coop {

bool TakeDirectDamage() {
    std::array<u8, 1400> buffer{};
    std::optional<std::vector<Libraries::Np::NpMatching2::RoomPeerAddress>> peers;
    bool took = false;
    for (int frames = 0; frames < 64; ++frames) {
        u32 addr = 0;
        u16 port = 0;
        const int size = Libraries::Net::P2PCoopRecvFrom(
            buffer.data(), static_cast<u32>(buffer.size()), &addr, &port);
        if (size <= 0) {
            break;
        }
        if (!peers) {
            peers = Libraries::Np::NpMatching2::GetActiveRoomPeers();
        }
        const bool from_peer = std::ranges::any_of(
            *peers, [&](const auto& peer) { return peer.addr == addr && peer.port == port; });
        const std::span<const u8> bytes{buffer.data(), static_cast<size_t>(size)};
        const auto frame = DecodeCoopFrame(bytes);
        TraceFrame("p2p-in", bytes, addr, port, from_peer);
        if (from_peer && frame) {
            took = TakeCoopFrame(frame->first, frame->second, bytes, addr, port) || took;
        }
    }
    return took;
}

} // namespace Coop

namespace Coop {

void SendToRoomPeers(const std::vector<u8>& frame) {
    for (const auto& peer : Libraries::Np::NpMatching2::GetActiveRoomPeers()) {
        SendToPeer(frame, peer.addr, peer.port);
    }
}

void SendToPeer(const std::vector<u8>& frame, u32 addr, u16 port) {
    Libraries::Net::P2PCoopSendTo(frame.data(), static_cast<u32>(frame.size()), addr, port);
    TraceFrame("p2p-out", frame, addr, port);
}

} // namespace Coop

namespace {

std::string BoardMemberName();

// Another member left the session (MemberLeft): the frames of the senders no longer linked go at
// once (a dead game's last vitals line counted it as standing for DirectDamageLifeMs); the
// remaining members' stay.
[[maybe_unused]] const bool member_frames_reset_registered =
    RegisterLifecycleReset("frames of members gone", {Lifecycle::MemberLeft}, [] {
        std::vector<u64> linked;
        for (const auto& peer : Libraries::Np::NpMatching2::GetActiveRoomPeers()) {
            linked.push_back((u64{peer.addr} << 16) | peer.port);
        }
        direct_damage_inbox.KeepOnly(linked);
        std::scoped_lock lock{direct_boards_mutex};
        direct_vitals_inbox.KeepOnly(linked);
        direct_kills_inbox.KeepOnly(linked);
    });

} // namespace

namespace Coop {

// This member's standing kills to every room peer: at once when they changed, else every
// DirectDamageResendMs. Its whole set each time: a lost frame is made good by the next one.
void SendDirectKills(std::span<const u32> kills, bool changed) {
    static s64 sent_at = 0;
    const s64 now = SteadyMilliseconds();
    if (!changed && now - sent_at < DirectDamageResendMs) {
        return;
    }
    sent_at = now;
    SendToRoomPeers(EncodeCoopFrame(DirectKillsType, EncodeDirectKills(BoardMemberName(), kills)));
}

// This member's report to every room peer: at once when it changed, else every
// DirectDamageResendMs (a lost frame is made good by the next one: its counts only grow).
void SendDirectDamage(const MemberMapState& mine, bool changed) {
    static s64 sent_at = 0;
    const s64 now = SteadyMilliseconds();
    if (mine.map == 0 || (!changed && now - sent_at < DirectDamageResendMs)) {
        return;
    }
    sent_at = now;
    const std::vector<u8> frame = EncodeDirectDamage(mine.map, mine.bits);
    for (const auto& peer : Libraries::Np::NpMatching2::GetActiveRoomPeers()) {
        Libraries::Net::P2PCoopSendTo(frame.data(), static_cast<u32>(frame.size()), peer.addr,
                                      peer.port);
        TraceFrame("p2p-out", frame, peer.addr, peer.port);
    }
}

// This game's hello to every room peer: every PeerHelloResendMs, and at once when a peer it has
// not greeted joins the room's links (a new member, or one whose link came back).
void SendPeerHello() {
    static s64 checked_at = 0;
    static s64 sent_at = 0;
    static std::set<u64> greeted;
    const s64 now = SteadyMilliseconds();
    if (now - checked_at < DirectDamageResendMs) {
        return;
    }
    checked_at = now;
    PeerHello hello;
    {
        std::scoped_lock lock{peer_hello_mutex};
        if (!own_hello) {
            return;
        }
        hello = *own_hello;
    }
    const auto peers = Libraries::Np::NpMatching2::GetActiveRoomPeers();
    std::set<u64> linked;
    for (const auto& peer : peers) {
        linked.insert((u64{peer.addr} << 16) | peer.port);
    }
    const bool new_peer =
        std::ranges::any_of(linked, [&](u64 peer) { return !greeted.contains(peer); });
    for (const u64 peer : linked) {
        if (!greeted.contains(peer)) {
            TraceLife("peer-join", {{"peer", fmt::format("{:08x}:{}", peer >> 16, peer & 0xFFFF)}});
        }
    }
    for (const u64 peer : greeted) {
        if (!linked.contains(peer)) {
            TraceLife("peer-leave",
                      {{"peer", fmt::format("{:08x}:{}", peer >> 16, peer & 0xFFFF)}});
        }
    }
    greeted = std::move(linked);
    if (peers.empty() || (!new_peer && now - sent_at < PeerHelloResendMs)) {
        return;
    }
    sent_at = now;
    hello.member = BoardMemberName();
    SendToRoomPeers(EncodeCoopFrame(PeerHelloType, EncodePeerHello(hello)));
}

} // namespace Coop

namespace {

// The character's face data (PlayerGameData +0x4AC, 0xF0 bytes, as channel 8 sends it): its
// portrait is kept on disk by it. Nothing before the character is loaded.
std::optional<u64> OwnFaceFingerprint() {
    const u64 data = Coop::OwnGameData();
    if (data < 0x10000 || !HasMemoryAccess(data + 0x4AC, 0xF0, MemoryProt::CpuRead)) {
        return std::nullopt;
    }
    u64 hash = 0xCBF29CE484222325ull;
    bool any = false;
    for (u64 index = 0; index < 0xF0; ++index) {
        const u8 byte = ReadValue<u8>(data, 0x4AC + index);
        any = any || byte != 0;
        hash = (hash ^ byte) * 0x100000001B3ull;
    }
    return any ? std::optional<u64>{hash} : std::nullopt;
}

} // namespace

namespace Coop {

// This character's portrait: the status page's last capture (kept on disk by its face), else the
// one kept for its face; to every room peer at once when it changed or a peer came, and again now
// and then (PortraitSender).
void TickPortraits() {
    static s64 ticked_at = 0;
    static std::optional<u64> own_face;
    static std::optional<PortraitPixels> own;
    static PortraitSender sender;
    static std::pair<u32, size_t> logged{};
    const s64 now = SteadyMilliseconds();
    if (now - ticked_at < 250) {
        return;
    }
    ticked_at = now;
    const auto face = OwnFaceFingerprint();
    std::optional<PortraitPixels> settled;
    {
        std::scoped_lock lock{portraits_mutex};
        settled = face_capture.Settled(now);
    }
    const auto dir = Common::FS::GetUserPath(Common::FS::PathType::UserDir) / "portraits";
    if (face && face != own_face) {
        own_face = face;
        own = LoadPortrait(PortraitCachePath(dir, *face));
        if (own) {
            LOG_INFO(Debug, "Bloodborne party portraits: the character's portrait from {}",
                     PortraitCachePath(dir, *face).string());
        }
    }
    if (settled) {
        own = std::move(settled);
        const bool kept = face && SavePortrait(PortraitCachePath(dir, *face), *own);
        LOG_INFO(Debug,
                 "Bloodborne party portraits: took the character's portrait from the status page "
                 "({:08x}; {})",
                 PortraitId(*own),
                 kept ? PortraitCachePath(dir, *face).string() : std::string{"not kept: no face"});
    }
    const std::string member = BoardMemberName();
    if (!own || member.empty()) {
        return;
    }
    std::vector<u64> peers;
    for (const auto& peer : Libraries::Np::NpMatching2::GetActiveRoomPeers()) {
        peers.push_back((u64{peer.addr} << 16) | peer.port);
    }
    std::ranges::sort(peers);
    const u32 id = PortraitId(*own);
    if (!sender.Due(id, peers, now)) {
        return;
    }
    for (const auto& part : SplitPortrait(member, *own)) {
        SendToRoomPeers(EncodeCoopFrame(PortraitPartType, EncodePortraitPart(part)));
    }
    if (logged != std::pair{id, peers.size()}) {
        logged = {id, peers.size()};
        LOG_INFO(Debug,
                 "Bloodborne party portraits: sent the character's portrait ({:08x}) to {} "
                 "peer(s) as {}",
                 id, peers.size(), member);
    }
}

bool ShowPartyPortrait(int slot, std::string_view member) {
    std::scoped_lock lock{portraits_mutex};
    return party_portraits.Show(slot, member);
}

// The vitals board (see RunVitalsBoardClient): what the game thread posts and what the client
// last heard back, as the member it posts as.
std::mutex vitals_mutex;

} // namespace Coop

namespace {

std::condition_variable_any vitals_wake;
MemberVitals vitals_mine;
std::vector<ReviveHitTotal> vitals_hits;
bool vitals_changed{};

} // namespace

namespace Coop {

std::string vitals_member;

} // namespace Coop

namespace {

std::jthread vitals_board_client;

std::string BoardMemberName() {
    std::scoped_lock lock{vitals_mutex};
    return vitals_member;
}

// The net sync object (SummonSessionRulesPointerOffset, see MemberDataWait): +0x84 is 4 in a
// session, +0x68 the list of members with a remote player slot (head node; node: next +0, member
// id +0x10, slot +0x30). The slot's byte of received parts is GameDataMan [+0x18][slot].
std::optional<s32> MemberDataSlot(u64 net, u64 member_id) {
    if (net < 0x10000 || !HasMemoryAccess(net, 0x88, MemoryProt::CpuRead)) {
        return std::nullopt;
    }
    using IdEqual = bool PS4_SYSV_ABI (*)(u64 a, u64 b);
    const auto id_equal = reinterpret_cast<IdEqual>(image_base + MemberIdEqualOffset);
    const u64 head = ReadValue<u64>(net, 0x68);
    if (head < 0x10000 || !HasMemoryAccess(head, 8, MemoryProt::CpuRead)) {
        return std::nullopt;
    }
    u64 node = ReadValue<u64>(head, 0);
    for (s32 hops = 0; node >= 0x10000 && node != head && hops < 16; ++hops) {
        if (!HasMemoryAccess(node, 0x34, MemoryProt::CpuRead)) {
            break;
        }
        if (id_equal(node + 0x10, member_id)) {
            const s32 slot = ReadValue<s32>(node, 0x30);
            return slot >= 0 && slot < 4 ? std::optional{slot} : std::nullopt;
        }
        node = ReadValue<u64>(node, 0);
    }
    return std::nullopt;
}

u64 MemberDataPartsAddress(s32 slot) {
    const u64 data = ReadValue<u64>(image_base + GameDataManPointerOffset, 0);
    const u64 parts = data >= 0x10000 && HasMemoryAccess(data, 0x20, MemoryProt::CpuRead)
                          ? ReadValue<u64>(data, 0x18)
                          : 0;
    return parts >= 0x10000 && HasMemoryAccess(parts + slot, 1, MemoryProt::CpuWrite) ? parts + slot
                                                                                      : 0;
}

// The game's own request (0x01781A70 sends the same): one zero byte on channel 0x0A, which the
// member's game answers with all of its data.
void SendMemberDataRequest(u64 net, u64 member_id) {
    using Send =
        s32 PS4_SYSV_ABI (*)(u64 net, u64 member_id, u32 channel, const void* buffer, u32 size);
    const u8 zero = 0;
    reinterpret_cast<Send>(image_base + NetSyncSendOffset)(net, member_id, MemberDataRequestChannel,
                                                           &zero, 1);
}

} // namespace

namespace Coop {

// See joins_waiting_for_data: once another member has stood on this map a moment, the waiting
// members are asked for their data until all of it is in, then their characters are created.
void FinishWaitingJoins(bool in_session) {
    if (joins_waiting_for_data.empty()) {
        return;
    }
    if (!in_session) {
        joins_waiting_for_data.clear();
        return;
    }
    const VitalsView view = ReadVitalsBoard();
    const bool here = view.Fresh() && !view.member.empty() &&
                      SomeoneElseStands(view.board.members, view.member, PlayingMap());
    const s64 now = SteadyMilliseconds();
    const u64 net = ReadValue<u64>(image_base + SummonSessionRulesPointerOffset, 0);
    using SetStep = void PS4_SYSV_ABI (*)(u64 task, s32 step);
    std::vector<u64> done;
    ForEachJoinTask([&](u64 task) {
        const u64 identity = ReadValue<u64>(task, 0xC8);
        const auto waiting = joins_waiting_for_data.find(identity);
        if (waiting == joins_waiting_for_data.end()) {
            return;
        }
        const s32 step = ReadValue<s32>(task, 0x50);
        const u8 flags = ReadValue<u8>(task, 0xED);
        if (step > CreateCharacterStep) {
            done.push_back(identity);
            return;
        }
        const u64 member_id = task + 0xC8;
        const std::optional<s32> slot = MemberDataSlot(net, member_id);
        const u64 parts = slot ? MemberDataPartsAddress(*slot) : 0;
        switch (waiting->second.Tick(now, here, parts ? ReadValue<u8>(parts, 0) : 0)) {
        case MemberDataWait::Action::None:
            return;
        case MemberDataWait::Action::Request:
            if (parts) {
                WriteValue(parts, 0, static_cast<u8>(ReadValue<u8>(parts, 0) & ~MemberDataParts));
            }
            SendMemberDataRequest(net, member_id);
            LOG_INFO(Debug,
                     "Bloodborne party trip: asked a member still joining for its data (request "
                     "{}, task {:#x} step={} flags={:#x} slot={})",
                     waiting->second.Requests(), task, step, flags,
                     slot ? fmt::format("{}", *slot) : "none");
            return;
        case MemberDataWait::Action::Create:
            WriteValue(task, 0xED, static_cast<u8>(flags | 1));
            if (step < CreateCharacterStep) {
                reinterpret_cast<SetStep>(ReadValue<u64>(ReadValue<u64>(task, 0), 0x28))(
                    task, CreateCharacterStep);
            }
            done.push_back(identity);
            LOG_INFO(Debug,
                     "Bloodborne party trip: a member that was still joining sent its data; "
                     "creating its character (task {:#x} step={} flags={:#x} slot={})",
                     task, step, flags, slot.value_or(-1));
            return;
        }
    });
    for (const u64 identity : done) {
        joins_waiting_for_data.erase(identity);
    }
}

} // namespace Coop

namespace {

// See OwnDataWatch: the stats and face channel 8 carries, PlayerGameData ([GameDataMan +0x08],
// the remote slots at [GameDataMan +0x10] + 0x6A0 each have the same layout) +0x40..+0x90 (every
// 8 bytes), +0xD4, +0xE0, +0xE4 and the face (0xF0 bytes at +0x4AC).
std::optional<u64> MemberDataFingerprint(u64 data) {
    if (data < 0x10000 || !HasMemoryAccess(data, 0x5A0, MemoryProt::CpuRead)) {
        return std::nullopt;
    }
    u64 hash = 0xCBF29CE484222325ull;
    const auto mix = [&](u64 at, u64 size) {
        for (u64 index = 0; index < size; ++index) {
            hash = (hash ^ ReadValue<u8>(data, at + index)) * 0x100000001B3ull;
        }
    };
    for (u64 at = 0x40; at <= 0x90; at += 8) {
        mix(at, 4);
    }
    mix(0xD4, 4);
    mix(0xE0, 8);
    mix(0x4AC, 0xF0);
    return hash;
}

} // namespace

namespace Coop {

u64 OwnGameData() {
    const u64 data = ReadValue<u64>(image_base + GameDataManPointerOffset, 0);
    return data >= 0x10000 && HasMemoryAccess(data, 0x10, MemoryProt::CpuRead)
               ? ReadValue<u64>(data, 0x08)
               : 0;
}

} // namespace Coop

namespace {

std::optional<u64> OwnDataFingerprint() {
    return MemberDataFingerprint(OwnGameData());
}

} // namespace

namespace Coop {

// The remote player slots' stats and face, logged when they change in a session (a member's
// level up seen here; the rig's raise-stat).
void LogMemberDataChanges(bool in_session) {
    static std::array<std::optional<u64>, 4> seen;
    const u64 data = ReadValue<u64>(image_base + GameDataManPointerOffset, 0);
    const u64 slots = data >= 0x10000 && HasMemoryAccess(data, 0x20, MemoryProt::CpuRead)
                          ? ReadValue<u64>(data, 0x10)
                          : 0;
    for (s32 slot = 0; slot < 4; ++slot) {
        const auto now = in_session && slots >= 0x10000
                             ? MemberDataFingerprint(slots + static_cast<u64>(slot) * 0x6A0)
                             : std::nullopt;
        if (now && seen[slot] && *now != *seen[slot]) {
            LOG_INFO(Debug, "Bloodborne party: member slot {} stats or face changed (first {})",
                     slot, ReadValue<s32>(slots + static_cast<u64>(slot) * 0x6A0, 0x40));
        }
        seen[slot] = now;
    }
}

// A level up (or any change to the stats or face) in a session reaches the others: the game's
// own after-load broadcast of all of the player's data (BroadcastMemberDataOffset).
void ResendOwnDataOnChange(bool in_session) {
    static OwnDataWatch watch;
    const u64 net = ReadValue<u64>(image_base + SummonSessionRulesPointerOffset, 0);
    const u64 state = GetGlobalState();
    const bool can_send =
        in_session && net >= 0x10000 && HasMemoryAccess(net, 0x88, MemoryProt::CpuRead) &&
        ReadValue<s32>(net, 0x84) == 4 && ReadValue<u64>(net, 0x10) != 0 && state >= 0x10000 &&
        HasMemoryAccess(state, 0x09, MemoryProt::CpuRead) && ReadValue<u8>(state, 0x08) == 0;
    if (watch.Observe(can_send ? OwnDataFingerprint() : std::nullopt, SteadyMilliseconds())) {
        reinterpret_cast<void PS4_SYSV_ABI (*)()>(image_base + BroadcastMemberDataOffset)();
        LOG_INFO(Debug, "Bloodborne party: the player's stats or face changed; sent its data to "
                        "the session again");
    }
}

// Hands the client this member's vitals: posted at once when more than its position changed.
void PostVitals(const MemberVitals& mine, std::vector<ReviveHitTotal> hits) {
    // On change only (the rig checks SessionArrival by it).
    static VitalsPostLog post_log;
    if (const auto line = post_log.Next(mine.vital, mine.map)) {
        LOG_INFO(Debug, "Bloodborne party vitals: {}", *line);
    }
    std::scoped_lock lock{vitals_mutex};
    MemberVitals moved = vitals_mine;
    moved.at = mine.at;
    const bool changed = moved != mine || hits != vitals_hits;
    vitals_mine = mine;
    vitals_hits = std::move(hits);
    if (changed) {
        vitals_changed = true;
        vitals_wake.notify_all();
    }
    // Straight to the room's peers too (see DirectVitalsType), once the board named this member.
    static s64 sent_at = 0;
    const s64 now = SteadyMilliseconds();
    if (mine.vital != Vital::Off && !vitals_member.empty() &&
        (changed || now - sent_at >= DirectDamageResendMs)) {
        sent_at = now;
        MemberVitals self = mine;
        self.member = vitals_member;
        const std::string line = EncodeVitalsUpdate(self, vitals_hits);
        SendToRoomPeers(EncodeCoopFrame(
            DirectVitalsType,
            std::span<const u8>{reinterpret_cast<const u8*>(line.data()), line.size()}));
    }
}

} // namespace Coop

namespace {

// Posts this member's vitals to the party's vitals board on shadNet and keeps its answer: once a
// second, four times a second while anyone in the party is down (revive progress), and soon after
// a change. Out of a session it posts "off" once, then waits.
void RunVitalsBoardClient(std::stop_token stop, std::string base_url, std::string party) {
    const std::string member = FindBoardMember(stop);
    {
        std::scoped_lock lock{vitals_mutex};
        vitals_member = member;
    }
    httplib::Client client(base_url);
    client.set_connection_timeout(std::chrono::seconds{1});
    client.set_read_timeout(std::chrono::seconds{2});
    const httplib::Headers headers{{"X-ShadPS4-Bloodborne-Party", party}};
    LOG_INFO(Debug, "Bloodborne party vitals board client for {} as member {}", base_url, member);

    bool posted_on = false;
    bool failing = false;
    bool someone_down = false;
    auto last_post = std::chrono::steady_clock::now();
    while (!stop.stop_requested()) {
        MemberVitals mine;
        std::vector<ReviveHitTotal> hits;
        {
            std::unique_lock lock{vitals_mutex};
            vitals_wake.wait_until(lock, stop, last_post + std::chrono::milliseconds{100},
                                   [] { return false; });
            vitals_wake.wait_until(
                lock, stop, last_post + std::chrono::milliseconds{someone_down ? 250 : 1'000},
                [] { return vitals_changed; });
            vitals_changed = false;
            mine = vitals_mine;
            hits = vitals_hits;
        }
        if (stop.stop_requested() || (mine.vital == Vital::Off && !posted_on)) {
            last_post = std::chrono::steady_clock::now();
            continue;
        }
        mine.member = member;
        last_post = std::chrono::steady_clock::now();
        const auto response = PostToBoard(client, "/bloodborne/party/vitals", headers,
                                          EncodeVitalsUpdate(mine, hits));
        const auto board =
            response && response->status == 200 ? ParseVitalsBoard(response->body) : std::nullopt;
        if (!board) {
            if (!failing) {
                LOG_WARNING(Debug, "Bloodborne party vitals board unreachable ({})",
                            response ? std::to_string(response->status)
                                     : httplib::to_string(response.error()));
            }
            failing = true;
            continue;
        }
        failing = false;
        posted_on = mine.vital != Vital::Off;
        someone_down = std::ranges::any_of(
            board->members, [](const MemberVitals& other) { return other.vital == Vital::Down; });
        std::scoped_lock lock{vitals_mutex};
        vitals_board = *board;
        vitals_heard_at = std::chrono::steady_clock::now();
    }
}

// The board clients use state that lives in the other co-op files (flags, kills, HP, the trace
// queue), and the order the files' statics are destroyed in at exit() is not defined across
// files. So they are stopped and joined before any static destructor runs: by an atexit handler
// registered once the first client starts, after all static initialization (handlers registered
// later run earlier). The window's close ends the process with _Exit and runs neither; the port's
// guest exit() and STOP paths run both.
void StopBoardClientsAtExit() {
    static std::once_flag registered;
    std::call_once(registered, [] {
        std::atexit([] {
            for (std::jthread* client :
                 {&rest_board_client, &hp_board_client, &vitals_board_client}) {
                client->request_stop();
            }
            for (std::jthread* client :
                 {&rest_board_client, &hp_board_client, &vitals_board_client}) {
                if (client->joinable()) {
                    client->join();
                }
            }
        });
    });
}

} // namespace

namespace Coop {

void StartVitalsBoardClient() {
    const char* party_setting = std::getenv("SHADPS4_BLOODBORNE_PARTY");
    const auto party = party_setting ? ParsePartySetting(party_setting) : std::nullopt;
    const std::string base_url = EmulatorSettings.GetShadNetWebApiServer();
    if (!party_controller || !party || !EmulatorSettings.IsShadNetEnabled() || base_url.empty()) {
        return;
    }
    vitals_board_client = std::jthread(RunVitalsBoardClient, base_url, *party);
    StopBoardClientsAtExit();
}

} // namespace Coop

void NoteStatusModel(std::span<const u8> rgba, int width, int height) {
    auto portrait = PortraitFromModel(rgba, width, height);
    if (!portrait) {
        return;
    }
    std::scoped_lock lock{portraits_mutex};
    face_capture.Take(std::move(*portrait), SteadyMilliseconds());
}

bool ReadPortraitSlot(int slot, std::span<u8> rgba, u32& version) {
    std::scoped_lock lock{portraits_mutex};
    version = party_portraits.Version(slot);
    const auto pixels = party_portraits.Pixels(slot);
    if (!pixels || rgba.size() < pixels->size()) {
        return false;
    }
    std::ranges::copy(*pixels, rgba.begin());
    return true;
}

u32 PortraitSlotVersion(int slot) {
    std::scoped_lock lock{portraits_mutex};
    return party_portraits.Version(slot);
}

} // namespace Core::Bloodborne
