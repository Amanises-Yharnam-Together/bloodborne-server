// SPDX-License-Identifier: GPL-2.0-or-later
// bbport: checks on a pipeline cache entry that need neither Vulkan nor a game (see
// tests/test_cache_consistency.cpp).
#pragma once

#include <array>
#include <cstddef>
#include <cstring>
#include <span>
#include <string_view>
#include "common/types.h"
#include "shader_recompiler/backend/bindings.h"

namespace Vulkan::CacheCheck {

/// What a graphics or compute pipeline in the shader cache says about one of its stages.
struct StageBindings {
    bool present{};
    /// At least one buffer, image or sampler of the stage has a valid sharp in the stored
    /// permutation (StageSpecialization::bitset): only then is `start` part of what the
    /// permutation is, and a lookup at another start does not find it.
    bool has_resources{};
    /// Where the stored SPIR-V numbers its first descriptor (and user data register).
    Shader::Backend::Bindings start{};
    /// What the stage adds for the stages after it (Info::AddBindings).
    Shader::Backend::Bindings size{};
};

/// Stages are numbered one after the other in stage order (fragment first), and the descriptor set
/// layout of the pipeline is built the same way from the stages' resource lists. A pipeline whose
/// stored stage starts anywhere else was assembled from shaders that were compiled for different
/// neighbours, so its layout and its SPIR-V disagree about which descriptor is which. Returns the
/// index of the first such stage, or -1. A stage without a valid resource is skipped: its start is
/// not part of its identity (StageSpecialization::operator== ignores it), so a running game
/// legitimately pairs such a permutation with fragment shaders of any size, and a pipeline key
/// written that way is sound.
inline int FirstMisplacedStage(std::span<const StageBindings> stages) {
    Shader::Backend::Bindings expected{};
    for (size_t i = 0; i < stages.size(); ++i) {
        const auto& stage = stages[i];
        if (!stage.present) {
            continue;
        }
        if (stage.has_resources && stage.start != expected) {
            return static_cast<int>(i);
        }
        expected.unified += stage.size.unified;
        expected.buffer += stage.size.buffer;
    }
    return -1;
}

/// The "profile" blob of a cache is {versions..., Shader::Profile}. Whoever changes what a cache
/// entry means bumps a version; a blob without this header (an older build) or with other numbers
/// is not compatible and the whole cache is rebuilt.
inline bool HeaderMatches(std::span<const u8> blob, std::span<const u32> versions,
                          size_t profile_size) {
    const size_t header = versions.size_bytes();
    return blob.size() == header + profile_size &&
           std::memcmp(blob.data(), versions.data(), header) == 0;
}

/// A shader pack (shader_packs/*.zip: a cache built by us for one GPU profile, see
/// tools/shader_pack.py) may hold the profile blob and the cache's own blobs only, under the names
/// the cache gives them, with no folder: "profile.bin", "<hash>.meta", "<hash>_<index>.spv",
/// "g_<hash>.key" and "c_<hash>.key", <hash> being 0x and 16 lower-case hex digits. Anything else
/// (the driver's cache, a half-written .tmp, a path) is not installed.
inline bool IsPackEntryName(std::string_view name) {
    if (name == "profile.bin") {
        return true;
    }
    const auto hash_at = [&](size_t pos) {
        if (name.size() < pos + 18 || name[pos] != '0' || name[pos + 1] != 'x') {
            return false;
        }
        for (size_t i = pos + 2; i < pos + 18; ++i) {
            const char c = name[i];
            if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) {
                return false;
            }
        }
        return true;
    };
    if (name.ends_with(".key")) {
        return (name.starts_with("g_") || name.starts_with("c_")) && name.size() == 2 + 18 + 4 &&
               hash_at(2);
    }
    if (name.ends_with(".meta")) {
        return name.size() == 18 + 5 && hash_at(0);
    }
    if (name.ends_with(".spv")) {
        // <hash>_<index>.spv, the index a decimal of 1 to 4 digits.
        if (!hash_at(0) || name.size() < 18 + 1 + 1 + 4 || name[18] != '_') {
            return false;
        }
        const auto index = name.substr(19, name.size() - 19 - 4);
        if (index.size() > 4) {
            return false;
        }
        for (const char c : index) {
            if (c < '0' || c > '9') {
                return false;
            }
        }
        return true;
    }
    return false;
}

} // namespace Vulkan::CacheCheck
