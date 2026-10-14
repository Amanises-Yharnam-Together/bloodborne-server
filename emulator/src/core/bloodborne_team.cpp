// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/bloodborne_team.h"

#include <algorithm>
#include <charconv>
#include <string_view>

#include "core/bloodborne_flags.h"
#include "core/bloodborne_party.h"

namespace Core::Bloodborne {

bool TeamCanBegin(const bbcoop::LobbyState& lobby, const std::string& me) {
    if (!lobby.has_team()) {
        return false;
    }
    if (lobby.team().started()) {
        return true;
    }
    return std::ranges::all_of(lobby.team().members(), [&](const bbcoop::TeamMember& m) {
        return m.member() == me ||
               (m.ready() && m.state() != bbcoop::LobbyPlayer::OFFLINE);
    });
}

TeamGate::Decision TeamGate::Tick(const std::string& me, u32 slot_now, s64 now_ms) {
    (void)me;
    Decision decision;
    if (!holding_since) {
        holding_since = now_ms;
    }
    slot = slot_now;
    if (started && !alone) {
        decision.start = started;
        return decision;
    }
    if (!alone) {
        decision.step = Step::Hold; // the team screen is up: the player decides
    }
    return decision;
}

std::vector<bbcoop::TeamRequest> TeamGate::Choose(Choice choice, const std::string& me) {
    std::vector<bbcoop::TeamRequest> requests;
    switch (choice) {
    case Choice::Alone:
        alone = true;
        break;
    case Choice::Unready:
        requests.emplace_back().set_action(bbcoop::TeamRequest::UNREADY);
        break;
    case Choice::Ready:
    case Choice::Start: {
        bbcoop::TeamRequest& ready = requests.emplace_back();
        ready.set_action(bbcoop::TeamRequest::READY);
        ready.set_slot(slot);
        if (choice == Choice::Start && lobby && lobby->has_team() && lobby->team().leader() == me) {
            requests.emplace_back().set_action(bbcoop::TeamRequest::START);
        }
        break;
    }
    }
    return requests;
}

void TeamGate::Reset() {
    started.reset();
    holding_since.reset();
    alone = false;
}

bool IsWorldFlag(s32 flag) {
    const FlagOwner owner = RuleFor(flag).owner;
    return owner == FlagOwner::Host || owner == FlagOwner::Shared;
}

namespace {

// The world flags on in one block.
void OnInBlock(u32 flags_per_block, const FlagBlockBits& block, std::vector<s32>& out) {
    for (size_t byte = 0; byte < block.bytes.size(); ++byte) {
        const u8 bits = block.bytes[byte];
        if (bits == 0) {
            continue;
        }
        for (u32 bit = 0; bit < 8; ++bit) {
            if ((bits & (0x80 >> bit)) == 0) {
                continue;
            }
            const s32 flag = static_cast<s32>(block.id * flags_per_block + byte * 8 + bit);
            if (IsWorldFlag(flag)) {
                out.push_back(flag);
            }
        }
    }
}

} // namespace

WorldFlags CaptureWorldFlags(u32 flags_per_block, std::span<const FlagBlockBits> blocks) {
    WorldFlags world;
    for (const FlagBlockBits& block : blocks) {
        world.blocks.push_back(block.id);
        OnInBlock(flags_per_block, block, world.on);
    }
    std::sort(world.blocks.begin(), world.blocks.end());
    std::sort(world.on.begin(), world.on.end());
    return world;
}

std::vector<std::pair<s32, bool>> WorldFlagChanges(u32 flags_per_block,
                                                   std::span<const FlagBlockBits> blocks,
                                                   const WorldFlags& target) {
    std::vector<std::pair<s32, bool>> changes;
    if (flags_per_block == 0) {
        return changes;
    }
    for (const FlagBlockBits& block : blocks) {
        if (!std::binary_search(target.blocks.begin(), target.blocks.end(), block.id)) {
            continue;
        }
        std::vector<s32> on;
        OnInBlock(flags_per_block, block, on);
        const s32 first = static_cast<s32>(block.id * flags_per_block);
        const s32 end = first + static_cast<s32>(block.bytes.size() * 8);
        const auto lo = std::lower_bound(target.on.begin(), target.on.end(), first);
        const auto hi = std::lower_bound(target.on.begin(), target.on.end(), end);
        std::vector<s32> wanted;
        std::copy_if(lo, hi, std::back_inserter(wanted), IsWorldFlag);
        std::vector<s32> off_now, on_now;
        std::set_difference(on.begin(), on.end(), wanted.begin(), wanted.end(),
                            std::back_inserter(off_now));
        std::set_difference(wanted.begin(), wanted.end(), on.begin(), on.end(),
                            std::back_inserter(on_now));
        for (const s32 flag : off_now) {
            changes.emplace_back(flag, false);
        }
        for (const s32 flag : on_now) {
            changes.emplace_back(flag, true);
        }
    }
    std::sort(changes.begin(), changes.end());
    return changes;
}

std::string FormatWorldFlags(const WorldFlags& world) {
    std::string text = "blocks\n";
    for (const u32 block : world.blocks) {
        text += std::to_string(block) + "\n";
    }
    text += "on\n";
    for (const s32 flag : world.on) {
        text += std::to_string(flag) + "\n";
    }
    return text + "end\n";
}

std::optional<WorldFlags> ParseWorldFlags(std::string_view text) {
    WorldFlags world;
    int part = 0; // 1 blocks, 2 on, 3 the end
    while (!text.empty()) {
        const auto end = text.find('\n');
        std::string_view line = text.substr(0, end);
        text = end == std::string_view::npos ? std::string_view{} : text.substr(end + 1);
        if (!line.empty() && line.back() == '\r') {
            line.remove_suffix(1);
        }
        if (line == "blocks") {
            part = 1;
            continue;
        }
        if (line == "on") {
            part = 2;
            continue;
        }
        if (line == "end" && part == 2) {
            part = 3;
            continue;
        }
        if (line.empty()) {
            continue;
        }
        s64 value = 0;
        const auto [parsed, error] = std::from_chars(line.data(), line.data() + line.size(), value);
        if (error != std::errc{} || parsed != line.data() + line.size() || part == 0 || part == 3) {
            return std::nullopt;
        }
        if (part == 1) {
            world.blocks.push_back(static_cast<u32>(value));
        } else {
            world.on.push_back(static_cast<s32>(value));
        }
    }
    if (part != 3) {
        return std::nullopt; // cut short
    }
    std::sort(world.blocks.begin(), world.blocks.end());
    std::sort(world.on.begin(), world.on.end());
    return world;
}

WorldFlags WorldFlagsOf(const bbcoop::TeamWorld& message) {
    WorldFlags world;
    world.on.assign(message.on().begin(), message.on().end());
    world.blocks.assign(message.blocks().begin(), message.blocks().end());
    std::sort(world.on.begin(), world.on.end());
    std::sort(world.blocks.begin(), world.blocks.end());
    return world;
}

TeamJoin::Action TeamJoin::Tick(const Frame& frame) {
    if (!frame.host) {
        return frame.ready && frame.matching_state == 0 && frame.session_known ? Action::Join
                                                                               : Action::None;
    }
    if (frame.matching_state == MatchingStateHosting) {
        if (published) {
            return Action::None; // the server keeps it for the guests still out
        }
        published = true;
        return Action::Publish;
    }
    if (published) {
        published = false;
        return Action::PublishClosed;
    }
    return frame.ready && frame.matching_state == 0 ? Action::CreateSession : Action::None;
}

void TeamLoad::Start(s64 now_ms) {
    started_ms = now_ms;
    own = bbcoop::LoadProgress::LOADING;
    own_since_ms = now_ms;
    open = false;
    sent.reset();
}

void TeamLoad::SetStage(TeamLoad::Stage stage, s64 now_ms) {
    if (stage != own) {
        own = stage;
        own_since_ms = now_ms;
    }
}

void TeamLoad::Take(const bbcoop::LoadProgress& other, s64 now_ms) {
    for (auto& [member, heard] : others) {
        if (member == other.member()) {
            heard = {other, now_ms};
            return;
        }
    }
    others.emplace_back(other.member(), Heard{other, now_ms});
}

namespace {

// A loading bar's share of the load: LoadedPercent over LoadingGuessMs, never quite all of it.
u32 LoadingPercent(s64 elapsed_ms) {
    const s64 share = std::clamp<s64>(elapsed_ms, 0, TeamLoad::LoadingGuessMs);
    return static_cast<u32>(5 + (TeamLoad::LoadedPercent - 10) * share / TeamLoad::LoadingGuessMs);
}

} // namespace

u32 TeamLoad::OwnPercent(s64 now_ms) const {
    switch (own) {
    case bbcoop::LoadProgress::LOADING:
        return LoadingPercent(now_ms - own_since_ms);
    case bbcoop::LoadProgress::JOINING:
        return TeamLoad::LoadedPercent +
               static_cast<u32>(std::clamp<s64>((now_ms - own_since_ms) / 500, 0, 25));
    case bbcoop::LoadProgress::READY:
        return 100;
    default:
        return 0;
    }
}

TeamLoad::View TeamLoad::Tick(std::span<const std::pair<std::string, std::string>> members,
                              const std::string& me, s64 now_ms) {
    View view;
    if (!started_ms || open) {
        return view;
    }
    bool all_ready = true;
    for (const auto& [member, name] : members) {
        Row row{.member = member, .name = name, .self = member == me};
        if (row.self) {
            row.stage = own;
            row.percent = OwnPercent(now_ms);
        } else {
            for (const auto& [who, heard] : others) {
                if (who == member) {
                    row.stage = heard.load.stage();
                    row.percent = heard.load.percent();
                    // Between its messages a load's bar goes on growing here too.
                    if (row.stage == bbcoop::LoadProgress::LOADING) {
                        row.percent =
                            std::max(row.percent, std::min(TeamLoad::LoadedPercent - 5,
                                                           LoadingPercent(now_ms - heard.at_ms) +
                                                               row.percent - 5));
                    }
                }
            }
        }
        all_ready = all_ready && row.stage == bbcoop::LoadProgress::READY;
        view.rows.push_back(std::move(row));
    }
    if (all_ready || now_ms - *started_ms >= LimitMs) {
        open = true;
        view.rows.clear();
        return view;
    }
    view.up = true;
    return view;
}

std::optional<bbcoop::LoadProgress> TeamLoad::ToSend(s64 now_ms) {
    if (!started_ms) {
        return std::nullopt;
    }
    const u32 percent = OwnPercent(now_ms);
    // On a stage's change, and every 10 points of a bar.
    if (sent && sent->first == own && percent < sent->second + 10) {
        return std::nullopt;
    }
    sent = std::pair{own, percent};
    bbcoop::LoadProgress load;
    load.set_stage(own);
    load.set_percent(percent);
    return load;
}

} // namespace Core::Bloodborne
