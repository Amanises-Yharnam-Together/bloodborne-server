// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/bloodborne_portrait.h"

#include <algorithm>
#include <bit>
#include <fstream>

#include <fmt/format.h>

namespace Core::Bloodborne {

namespace {

// Of the samples a portrait pixel averages (Samples x Samples each), the share that must be the
// character for a target to count as one (its first frames are empty).
constexpr int Samples = 4;
constexpr float MinFigureShare = 0.05f;
// The opaque pixels a row needs to be the top of the head.
constexpr int MinTopPixels = 3;

void PutU32(std::vector<u8>& out, u32 value) {
    for (int shift = 0; shift < 32; shift += 8) {
        out.push_back(static_cast<u8>(value >> shift));
    }
}

u16 ToRgb565(const u8* rgba) {
    return static_cast<u16>((rgba[0] >> 3) << 11 | (rgba[1] >> 2) << 5 | rgba[2] >> 3);
}

void FromRgb565(u16 value, u8* rgba) {
    const u8 r = (value >> 11) & 0x1F, g = (value >> 5) & 0x3F, b = value & 0x1F;
    rgba[0] = static_cast<u8>(r << 3 | r >> 2);
    rgba[1] = static_cast<u8>(g << 2 | g >> 4);
    rgba[2] = static_cast<u8>(b << 3 | b >> 2);
    rgba[3] = 0xFF;
}

} // namespace

std::optional<PortraitCrop> HeadAndShoulders(std::span<const u8> rgba, int width, int height) {
    if (width < StatusModelCropSide || height < StatusModelCropSide ||
        rgba.size() < size_t(width) * size_t(height) * 4) {
        return std::nullopt;
    }
    const auto opaque = [&](int x, int y) {
        return rgba[(size_t(y) * size_t(width) + size_t(x)) * 4 + 3] >= 0x80;
    };
    // The top of the head: the first row with a few opaque pixels (not a stray one).
    int top = -1;
    for (int y = 0; y < height && top < 0; ++y) {
        int count = 0;
        for (int x = 0; x < width && count < MinTopPixels; ++x) {
            count += opaque(x, y) ? 1 : 0;
        }
        top = count >= MinTopPixels ? y : -1;
    }
    if (top < 0) {
        return std::nullopt;
    }
    // The head's middle: the middle of each of its rows, averaged.
    s64 middles = 0;
    int rows = 0;
    for (int y = top; y < std::min(top + StatusModelHeadRows, height); ++y) {
        int first = -1, last = -1;
        for (int x = 0; x < width; ++x) {
            if (opaque(x, y)) {
                first = first < 0 ? x : first;
                last = x;
            }
        }
        if (first >= 0) {
            middles += first + last + 1; // twice the middle, rounded up
            ++rows;
        }
    }
    const int middle = int(middles / (2 * s64{rows}));
    return PortraitCrop{
        .x = std::clamp(middle - StatusModelCropSide / 2, 0, width - StatusModelCropSide),
        .y = std::clamp(top - StatusModelCropAbove, 0, height - StatusModelCropSide),
        .side = StatusModelCropSide,
    };
}

std::optional<PortraitPixels> PortraitFromModel(std::span<const u8> rgba, int width, int height) {
    if (width != StatusModelWidth || height != StatusModelHeight) {
        return std::nullopt;
    }
    const auto crop = HeadAndShoulders(rgba, width, height);
    if (!crop) {
        return std::nullopt;
    }
    PortraitPixels out(PortraitBytes);
    const float step = float(crop->side) / float(PortraitSide * Samples);
    int figure_samples = 0;
    for (int y = 0; y < PortraitSide; ++y) {
        for (int x = 0; x < PortraitSide; ++x) {
            std::array<u32, 3> sum{};
            for (int sy = 0; sy < Samples; ++sy) {
                for (int sx = 0; sx < Samples; ++sx) {
                    const int fx = crop->x + int((x * Samples + sx + 0.5f) * step);
                    const int fy = crop->y + int((y * Samples + sy + 0.5f) * step);
                    const u8* pixel = &rgba[(size_t(fy) * size_t(width) + size_t(fx)) * 4];
                    const u32 alpha = pixel[3];
                    figure_samples += alpha >= 0x80 ? 1 : 0;
                    for (int c = 0; c < 3; ++c) {
                        sum[c] +=
                            (pixel[c] * alpha + PortraitBackdrop[c] * (255 - alpha) + 127) / 255;
                    }
                }
            }
            u8* pixel = &out[(size_t(y) * PortraitSide + size_t(x)) * 4];
            for (int c = 0; c < 3; ++c) {
                pixel[c] = static_cast<u8>((sum[c] + Samples * Samples / 2) / (Samples * Samples));
            }
            pixel[3] = 0xFF;
        }
    }
    const int all = PortraitSide * PortraitSide * Samples * Samples;
    if (float(figure_samples) < float(all) * MinFigureShare) {
        return std::nullopt;
    }
    return out;
}

u32 PortraitId(std::span<const u8> pixels) {
    u32 hash = 0x811C9DC5u;
    for (const u8 byte : pixels) {
        hash = (hash ^ byte) * 0x01000193u;
    }
    return hash == 0 ? 1 : hash;
}

std::vector<PortraitPart> SplitPortrait(std::string_view member, const PortraitPixels& pixels) {
    if (pixels.size() != PortraitBytes) {
        return {};
    }
    std::vector<u8> rgb565;
    rgb565.reserve(PortraitBytes / 2);
    for (size_t at = 0; at < pixels.size(); at += 4) {
        const u16 value = ToRgb565(&pixels[at]);
        rgb565.push_back(static_cast<u8>(value));
        rgb565.push_back(static_cast<u8>(value >> 8));
    }
    const u32 id = PortraitId(pixels);
    std::vector<PortraitPart> parts;
    for (size_t part = 0; part < PortraitPartCount; ++part) {
        const auto first = rgb565.begin() + static_cast<std::ptrdiff_t>(part * PortraitPartBytes);
        parts.push_back(PortraitPart{.member = std::string{member.substr(0, 0xFF)},
                                     .id = id,
                                     .part = static_cast<u8>(part),
                                     .parts = static_cast<u8>(PortraitPartCount),
                                     .bytes = std::vector<u8>(first, first + PortraitPartBytes)});
    }
    return parts;
}

std::vector<u8> EncodePortraitPart(const PortraitPart& part) {
    std::vector<u8> payload;
    const size_t name = std::min<size_t>(part.member.size(), 0xFF);
    payload.push_back(static_cast<u8>(name));
    payload.insert(payload.end(), part.member.begin(), part.member.begin() + name);
    PutU32(payload, part.id);
    payload.push_back(part.part);
    payload.push_back(part.parts);
    payload.insert(payload.end(), part.bytes.begin(), part.bytes.end());
    return payload;
}

std::optional<PortraitPart> DecodePortraitPart(std::span<const u8> payload) {
    if (payload.empty()) {
        return std::nullopt;
    }
    const size_t name = payload[0];
    if (payload.size() != 1 + name + 4 + 2 + PortraitPartBytes) {
        return std::nullopt;
    }
    PortraitPart part;
    part.member.assign(payload.begin() + 1,
                       payload.begin() + 1 + static_cast<std::ptrdiff_t>(name));
    size_t at = 1 + name;
    for (int shift = 0; shift < 32; shift += 8) {
        part.id |= u32{payload[at++]} << shift;
    }
    part.part = payload[at++];
    part.parts = payload[at++];
    if (part.parts != PortraitPartCount || part.part >= part.parts) {
        return std::nullopt;
    }
    part.bytes.assign(payload.begin() + static_cast<std::ptrdiff_t>(at), payload.end());
    return part;
}

std::optional<PortraitPixels> PortraitAssembler::Take(const PortraitPart& part) {
    if (part.parts != PortraitPartCount || part.part >= part.parts ||
        part.bytes.size() != PortraitPartBytes) {
        return std::nullopt;
    }
    auto& pictures = by_member[part.member];
    auto found = std::ranges::find(pictures, part.id, &Pending::id);
    if (found == pictures.end()) {
        if (pictures.size() >= PicturesKept) {
            pictures.erase(std::ranges::min_element(pictures, {}, &Pending::touched));
        }
        pictures.push_back(Pending{.id = part.id, .rgb565 = std::vector<u8>(PortraitBytes / 2)});
        found = pictures.end() - 1;
    }
    Pending& pending = *found;
    pending.touched = ++touches;
    if (pending.made) {
        return std::nullopt;
    }
    std::ranges::copy(part.bytes, pending.rgb565.begin() +
                                      static_cast<std::ptrdiff_t>(part.part * PortraitPartBytes));
    pending.have |= 1u << part.part;
    if (pending.have != (1u << PortraitPartCount) - 1) {
        return std::nullopt;
    }
    pending.made = true;
    PortraitPixels pixels(PortraitBytes);
    for (size_t index = 0; index < PortraitBytes / 4; ++index) {
        FromRgb565(static_cast<u16>(pending.rgb565[index * 2] | pending.rgb565[index * 2 + 1] << 8),
                   &pixels[index * 4]);
    }
    return pixels;
}

void PartyPortraits::Take(std::string member, PortraitPixels pixels) {
    const u32 id = PortraitId(pixels);
    by_member[std::move(member)] = Picture{.id = id, .pixels = std::move(pixels)};
}

bool PartyPortraits::Has(std::string_view member) const {
    return by_member.contains(member);
}

bool PartyPortraits::Show(int slot, std::string_view member) {
    if (slot < 0 || slot >= PortraitSlotCount) {
        return false;
    }
    const auto found = member.empty() ? by_member.end() : by_member.find(member);
    const bool has = found != by_member.end();
    const std::string_view now = has ? member : std::string_view{};
    const u32 id = has ? found->second.id : 0;
    if (shown[slot] != now || shown_id[slot] != id) {
        shown[slot] = std::string{now};
        shown_id[slot] = id;
        ++versions[slot];
    }
    return has;
}

u32 PartyPortraits::Version(int slot) const {
    return slot >= 0 && slot < PortraitSlotCount ? versions[slot] : 0;
}

std::optional<PortraitPixels> PartyPortraits::Pixels(int slot) const {
    if (slot < 0 || slot >= PortraitSlotCount || shown[slot].empty()) {
        return std::nullopt;
    }
    const auto found = by_member.find(shown[slot]);
    if (found == by_member.end()) {
        return std::nullopt;
    }
    return found->second.pixels;
}

void PartyPortraits::KeepOnly(std::span<const std::string> members) {
    std::erase_if(by_member, [&](const auto& entry) {
        return std::ranges::find(members, entry.first) == members.end();
    });
}

void FaceCapture::Take(PortraitPixels pixels, s64 now_ms) {
    last = std::move(pixels);
    at = now_ms;
}

std::optional<PortraitPixels> FaceCapture::Settled(s64 now_ms) {
    if (!last || now_ms - at < SettleMs) {
        return std::nullopt;
    }
    return std::exchange(last, std::nullopt);
}

bool PortraitSender::Due(u32 id, std::span<const u64> peers, s64 now_ms) {
    if (id == 0 || peers.empty()) {
        sent_peers.assign(peers.begin(), peers.end());
        return false;
    }
    const bool new_peer = std::ranges::any_of(
        peers, [&](u64 peer) { return std::ranges::find(sent_peers, peer) == sent_peers.end(); });
    sent_peers.assign(peers.begin(), peers.end());
    if (id == sent_id && !new_peer && now_ms - sent_at < ResendMs) {
        return false;
    }
    sent_id = id;
    sent_at = now_ms;
    return true;
}

std::filesystem::path PortraitCachePath(const std::filesystem::path& dir, u64 fingerprint) {
    return dir / fmt::format("{:016x}.rgba", fingerprint);
}

bool SavePortrait(const std::filesystem::path& path, const PortraitPixels& pixels) {
    if (pixels.size() != PortraitBytes) {
        return false;
    }
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    const auto partial = std::filesystem::path{path}.concat(".part");
    {
        std::ofstream out{partial, std::ios::binary | std::ios::trunc};
        out.write(reinterpret_cast<const char*>(pixels.data()),
                  static_cast<std::streamsize>(pixels.size()));
        if (!out) {
            return false;
        }
    }
    std::filesystem::rename(partial, path, error);
    return !error;
}

std::optional<PortraitPixels> LoadPortrait(const std::filesystem::path& path) {
    std::error_code error;
    if (std::filesystem::file_size(path, error) != PortraitBytes || error) {
        return std::nullopt;
    }
    std::ifstream in{path, std::ios::binary};
    PortraitPixels pixels(PortraitBytes);
    in.read(reinterpret_cast<char*>(pixels.data()), static_cast<std::streamsize>(pixels.size()));
    if (!in) {
        return std::nullopt;
    }
    return pixels;
}

PartyListArt PartyListArtFor(const PartyListEntry& entry, bool portrait) {
    if (!entry.visible) {
        return {};
    }
    if (portrait) {
        return PartyListArt{
            .icon = PartyListIconNone, .badge = entry.icon, .face = PortraitFrameShown};
    }
    return PartyListArt{.icon = entry.icon, .badge = PartyListIconNone, .face = PortraitFrameNone};
}

} // namespace Core::Bloodborne
