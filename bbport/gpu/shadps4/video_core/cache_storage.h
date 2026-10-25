// SPDX-FileCopyrightText: Copyright 2025 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "common/path_util.h"
#include "common/singleton.h"
#include "common/types.h"

#include <filesystem>
#include <functional>
#include <span>
#include <thread>
#include <vector>

namespace Storage {

/// bbport: work to do when the game window is closed. The process then ends with _Exit, so no
/// destructor runs: a cache that saves at shutdown registers here. Remove it before the object
/// it captures dies (waits for a hook that is running).
u32 AddShutdownHook(std::function<void()> hook);
void RemoveShutdownHook(u32 id);
void RunShutdownHooks();

enum class BlobType : u32 {
    ShaderMeta,
    ShaderBinary,
    PipelineKey,
    ShaderProfile,
};

class DataBase {
public:
    static DataBase& Instance() {
        return *Common::Singleton<DataBase>::Instance();
    }

    void Open();
    void Close();
    [[nodiscard]] bool IsOpened() const {
        return opened;
    }
    void FinishPreload();
    /// bbport: removes every cached blob (an incompatible cache is rebuilt, not ignored).
    void Clear();

    bool Save(BlobType type, const std::string& name, std::vector<u8>&& data);
    bool Save(BlobType type, const std::string& name, std::vector<u32>&& data);

    void Load(BlobType type, const std::string& name, std::vector<u8>& data);
    void Load(BlobType type, const std::string& name, std::vector<u32>& data);

    /// bbport: `name` is the blob's file name without the extension, as Save/Load take it.
    void ForEachBlob(BlobType type,
                     const std::function<void(const std::string& name, std::vector<u8>&& data)>&
                         func);
    /// bbport: deletes one blob (not in an archive: false there, and when it is missing).
    bool Remove(BlobType type, const std::string& name);

    /// bbport: whether the cache holds at least one blob of this type.
    [[nodiscard]] bool HasBlobs(BlobType type) const;

    /// bbport: replaces the whole cache with a shader pack (a zip of cache blobs, see
    /// CacheCheck::IsPackEntryName) when `matches` accepts the pack's profile.bin (this build and
    /// this GPU). Returns the number of blobs written, profile.bin included; 0 leaves the cache as
    /// it was (no match, not a pack). A pack that breaks off while it is written leaves an empty
    /// cache. Written at once, not queued: called before the preload, which then reads the pack's
    /// blobs as its own.
    u32 InstallPack(const std::filesystem::path& pack,
                    const std::function<bool(std::span<const u8> profile)>& matches);

    /// bbport: the driver's VkPipelineCache blob, kept beside the shader cache of the game. Not
    /// queued like the blobs above: Load/Save are synchronous and Save is atomic (temporary
    /// file renamed into place), so a crash leaves the old file or none. False when the cache is
    /// not open, the file is missing or cannot be written.
    bool LoadDriverCache(std::vector<u8>& data);
    bool SaveDriverCache(const std::vector<u8>& data);
    void DeleteDriverCache();

private:
    [[nodiscard]] std::filesystem::path DriverCachePath() const;

    std::jthread io_worker{};
    std::filesystem::path cache_path{};
    bool opened{};
};

} // namespace Storage
