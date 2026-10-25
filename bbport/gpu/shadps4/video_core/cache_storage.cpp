// SPDX-FileCopyrightText: Copyright 2025-2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "common/elf_info.h"
#include "common/io_file.h"
#include "common/polyfill_thread.h"
#include "common/thread.h"
#include "core/emulator_settings.h"

#include "video_core/cache_storage.h"
#include "video_core/renderer_vulkan/vk_cache_consistency.h"
#include "video_core/renderer_vulkan/vk_instance.h"
#include "video_core/renderer_vulkan/vk_pipeline_cache.h"

#include <miniz.h>

#include <array>
#include <condition_variable>
#include <functional>
#include <future>
#include <mutex>
#include <queue>

namespace {

std::mutex submit_mutex{};
u32 num_requests{};
std::condition_variable_any request_cv{};
std::queue<std::packaged_task<void()>> req_queue{};
std::mutex m_request{};

mz_zip_archive zip_ar{};
bool ar_is_read_only{true};

std::mutex hooks_mutex;
u32 next_hook_id{1};
std::vector<std::pair<u32, std::function<void()>>> shutdown_hooks;

} // namespace

namespace Storage {

u32 AddShutdownHook(std::function<void()> hook) {
    std::scoped_lock lock{hooks_mutex};
    shutdown_hooks.emplace_back(next_hook_id, std::move(hook));
    return next_hook_id++;
}

void RemoveShutdownHook(u32 id) {
    std::scoped_lock lock{hooks_mutex};
    std::erase_if(shutdown_hooks, [id](const auto& hook) { return hook.first == id; });
}

void RunShutdownHooks() {
    std::scoped_lock lock{hooks_mutex};
    for (const auto& [id, hook] : shutdown_hooks) {
        hook();
    }
}

void ProcessIO(const std::stop_token& stoken) {
    Common::SetCurrentThreadName("shadPS4:PipelineCacheIO");

    while (!stoken.stop_requested()) {
        {
            std::unique_lock lk{submit_mutex};
            Common::CondvarWait(request_cv, lk, stoken, [&] { return num_requests; });
        }

        if (stoken.stop_requested()) {
            break;
        }

        while (num_requests) {
            std::packaged_task<void()> request{};
            {
                std::scoped_lock lock{m_request};
                if (req_queue.empty()) {
                    continue;
                }
                request = std::move(req_queue.front());
                req_queue.pop();
            }

            if (request.valid()) {
                request();
                request.get_future().wait();
            }

            --num_requests;
        }
    }
}

constexpr std::string GetBlobFileExtension(BlobType type) {
    switch (type) {
    case BlobType::ShaderMeta: {
        return "meta";
    }
    case BlobType::ShaderBinary: {
        return "spv";
    }
    case BlobType::PipelineKey: {
        return "key";
    }
    case BlobType::ShaderProfile: {
        return "bin";
    }
    default:
        UNREACHABLE();
    }
}

void DataBase::Open() {
    if (opened) {
        return;
    }

    const auto& game_info = Common::ElfInfo::Instance();

    using namespace Common::FS;
    if (EmulatorSettings.IsPipelineCacheArchived()) {
        mz_zip_zero_struct(&zip_ar);

        cache_path = GetUserPath(PathType::CacheDir) /
                     std::filesystem::path{game_info.GameSerial()}.replace_extension(".zip");

        if (!mz_zip_reader_init_file(&zip_ar, cache_path.string().c_str(),
                                     MZ_ZIP_FLAG_READ_ALLOW_WRITING) ||
            !mz_zip_validate_archive(&zip_ar, 0)) {
            LOG_INFO(Render, "Cache archive {} is not found or archive is corrupted",
                     cache_path.string().c_str());
            mz_zip_reader_end(&zip_ar);
            mz_zip_writer_init_file(&zip_ar, cache_path.string().c_str(), 0);
        }
    } else {
        cache_path = GetUserPath(PathType::CacheDir) / game_info.GameSerial();
        if (!std::filesystem::exists(cache_path)) {
            std::filesystem::create_directories(cache_path);
        }
    }

    io_worker = std::jthread{ProcessIO};
    opened = true;
}

void DataBase::Close() {
    if (!IsOpened()) {
        return;
    }

    io_worker.request_stop();
    io_worker.join();

    if (EmulatorSettings.IsPipelineCacheArchived()) {
        mz_zip_writer_finalize_archive(&zip_ar);
        mz_zip_writer_end(&zip_ar);
    }

    opened = false;
    LOG_INFO(Render, "Cache dumped");
}

template <typename T>
bool WriteVector(const BlobType type, std::filesystem::path&& path_, std::vector<T>&& v) {
    {
        auto request = std::packaged_task<void()>{[=]() {
            auto path{path_};
            path.replace_extension(GetBlobFileExtension(type));
            if (EmulatorSettings.IsPipelineCacheArchived()) {
                ASSERT_MSG(!ar_is_read_only,
                           "The archive is read-only. Did you forget to call `FinishPreload`?");
                if (!mz_zip_writer_add_mem(&zip_ar, path.string().c_str(), v.data(),
                                           v.size() * sizeof(T), MZ_BEST_COMPRESSION)) {
                    LOG_ERROR(Render, "Failed to add {} to the archive", path.string().c_str());
                }
            } else {
                // bbport: written under a temporary name and renamed into place, so a crash or a
                // power loss mid-write leaves the old file or none, never a cut-short one that
                // the next start reads as a damaged entry (issue #28).
                using namespace Common::FS;
                auto temp = path;
                temp += ".tmp";
                bool written = false;
                {
                    const auto file = IOFile{temp, FileAccessMode::Create};
                    written = file.IsOpen() && file.Write(v) == v.size();
                }
                std::error_code ec;
                if (written) {
                    std::filesystem::rename(temp, path, ec);
                }
                if (!written || ec) {
                    LOG_ERROR(Render, "Failed to write {}", path.string());
                    std::filesystem::remove(temp, ec);
                }
            }
        }};
        std::scoped_lock lock{m_request};
        req_queue.emplace(std::move(request));
    }

    std::scoped_lock lk{submit_mutex};
    ++num_requests;
    request_cv.notify_one();
    return true;
}

template <typename T>
void LoadVector(BlobType type, std::filesystem::path& path, std::vector<T>& v) {
    using namespace Common::FS;
    path.replace_extension(GetBlobFileExtension(type));
    if (EmulatorSettings.IsPipelineCacheArchived()) {
        int index{-1};
        index = mz_zip_reader_locate_file(&zip_ar, path.string().c_str(), nullptr, 0);
        if (index < 0) {
            LOG_WARNING(Render, "File {} is not found in the archive", path.string().c_str());
            return;
        }
        mz_zip_archive_file_stat stat{};
        mz_zip_reader_file_stat(&zip_ar, index, &stat);
        v.resize(stat.m_uncomp_size / sizeof(T));
        mz_zip_reader_extract_to_mem(&zip_ar, index, v.data(), stat.m_uncomp_size, 0);
    } else {
        const auto file = IOFile{path, FileAccessMode::Read};
        v.resize(file.GetSize() / sizeof(T));
        file.Read(v);
    }
}

bool DataBase::Save(BlobType type, const std::string& name, std::vector<u8>&& data) {
    if (!opened) {
        return false;
    }

    auto path = EmulatorSettings.IsPipelineCacheArchived() ? std::filesystem::path{name}
                                                           : cache_path / name;
    return WriteVector(type, std::move(path), std::move(data));
}

bool DataBase::Save(BlobType type, const std::string& name, std::vector<u32>&& data) {
    if (!opened) {
        return false;
    }

    auto path = EmulatorSettings.IsPipelineCacheArchived() ? std::filesystem::path{name}
                                                           : cache_path / name;
    return WriteVector(type, std::move(path), std::move(data));
}

void DataBase::Load(BlobType type, const std::string& name, std::vector<u8>& data) {
    if (!opened) {
        return;
    }

    auto path = EmulatorSettings.IsPipelineCacheArchived() ? std::filesystem::path{name}
                                                           : cache_path / name;
    return LoadVector(type, path, data);
}

void DataBase::Load(BlobType type, const std::string& name, std::vector<u32>& data) {
    if (!opened) {
        return;
    }

    auto path = EmulatorSettings.IsPipelineCacheArchived() ? std::filesystem::path{name}
                                                           : cache_path / name;
    return LoadVector(type, path, data);
}

void DataBase::ForEachBlob(
    BlobType type,
    const std::function<void(const std::string& name, std::vector<u8>&& data)>& func) {
    const auto& ext = GetBlobFileExtension(type);
    if (EmulatorSettings.IsPipelineCacheArchived()) {
        const auto num_files = mz_zip_reader_get_num_files(&zip_ar);
        for (int index = 0; index < num_files; ++index) {
            std::array<char, MZ_ZIP_MAX_ARCHIVE_FILENAME_SIZE> file_name{};
            file_name.fill(0);
            mz_zip_reader_get_filename(&zip_ar, index, file_name.data(), file_name.size());
            if (std::string{file_name.data()}.ends_with(ext)) {
                mz_zip_archive_file_stat stat{};
                mz_zip_reader_file_stat(&zip_ar, index, &stat);
                std::vector<u8> data(stat.m_uncomp_size);
                mz_zip_reader_extract_to_mem(&zip_ar, index, data.data(), data.size(), 0);
                func(std::filesystem::path{file_name.data()}.stem().string(), std::move(data));
            }
        }
    } else {
        for (const auto& file_name : std::filesystem::directory_iterator{cache_path}) {
            if (file_name.path().extension().string().ends_with(ext)) {
                using namespace Common::FS;
                const auto& file = IOFile{file_name, FileAccessMode::Read};
                if (file.IsOpen()) {
                    std::vector<u8> data(file.GetSize());
                    file.Read(data);
                    func(file_name.path().stem().string(), std::move(data));
                }
            }
        }
    }
}

bool DataBase::Remove(BlobType type, const std::string& name) {
    if (!opened || EmulatorSettings.IsPipelineCacheArchived()) {
        return false;
    }
    auto path = cache_path / name;
    path.replace_extension(GetBlobFileExtension(type));
    std::error_code ec;
    return std::filesystem::remove(path, ec);
}

void DataBase::Clear() {
    if (!opened || EmulatorSettings.IsPipelineCacheArchived()) {
        return;
    }
    std::error_code ec;
    u64 removed = 0;
    for (const auto& entry : std::filesystem::directory_iterator(cache_path, ec)) {
        if (entry.is_regular_file(ec) && std::filesystem::remove(entry.path(), ec)) {
            ++removed;
        }
    }
    if (removed) {
        LOG_WARNING(Render, "Pipeline cache cleared ({} files): it is rebuilt for this build",
                    removed);
    }
}

bool DataBase::HasBlobs(BlobType type) const {
    if (!opened || EmulatorSettings.IsPipelineCacheArchived()) {
        return false;
    }
    const auto ext = "." + GetBlobFileExtension(type);
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(cache_path, ec)) {
        if (entry.path().extension().string() == ext && entry.is_regular_file(ec)) {
            return true;
        }
    }
    return false;
}

namespace {

/// One blob written under a temporary name and renamed into place, as WriteVector does.
bool WriteFileAtomic(const std::filesystem::path& path, std::span<const u8> data) {
    using namespace Common::FS;
    auto temp = path;
    temp += ".tmp";
    bool written = false;
    {
        const auto file = IOFile{temp, FileAccessMode::Create};
        written = file.IsOpen() && file.WriteRaw<u8>(data.data(), data.size()) == data.size();
    }
    std::error_code ec;
    if (written) {
        std::filesystem::rename(temp, path, ec);
    }
    if (!written || ec) {
        std::filesystem::remove(temp, ec);
        return false;
    }
    return true;
}

} // namespace

u32 DataBase::InstallPack(const std::filesystem::path& pack,
                          const std::function<bool(std::span<const u8> profile)>& matches) {
    if (!opened || EmulatorSettings.IsPipelineCacheArchived()) {
        return 0;
    }
    mz_zip_archive zip{};
    mz_zip_zero_struct(&zip);
    if (!mz_zip_reader_init_file(&zip, pack.string().c_str(), 0)) {
        LOG_WARNING(Render, "Shader pack {} is not a zip", pack.string());
        return 0;
    }
    const auto extract = [&](int index, std::vector<u8>& data) {
        mz_zip_archive_file_stat stat{};
        if (!mz_zip_reader_file_stat(&zip, index, &stat) || stat.m_is_directory ||
            stat.m_uncomp_size > (u64{256} << 20)) {
            return false;
        }
        data.resize(stat.m_uncomp_size);
        return mz_zip_reader_extract_to_mem(&zip, index, data.data(), data.size(), 0) != 0;
    };

    // The profile first: a pack for another GPU, setting or build is left alone.
    std::vector<u8> pack_profile;
    const int profile_index = mz_zip_reader_locate_file(&zip, "profile.bin", nullptr, 0);
    if (profile_index < 0 || !extract(profile_index, pack_profile) ||
        !matches(pack_profile)) {
        mz_zip_reader_end(&zip);
        return 0;
    }

    Clear();
    u32 written = 0;
    u32 skipped = 0;
    bool damaged = false;
    std::vector<u8> data;
    const auto num_files = static_cast<int>(mz_zip_reader_get_num_files(&zip));
    for (int index = 0; index < num_files && !damaged; ++index) {
        std::array<char, MZ_ZIP_MAX_ARCHIVE_FILENAME_SIZE> file_name{};
        mz_zip_reader_get_filename(&zip, index, file_name.data(), file_name.size());
        const std::string name{file_name.data()};
        if (name == "profile.bin" || !Vulkan::CacheCheck::IsPackEntryName(name)) {
            skipped += name == "profile.bin" ? 0 : 1;
            continue;
        }
        if (!extract(index, data) || !WriteFileAtomic(cache_path / name, data)) {
            damaged = true;
            break;
        }
        ++written;
    }
    mz_zip_reader_end(&zip);
    if (damaged) {
        // Half a pack is not a cache of one generation: start empty instead.
        LOG_WARNING(Render, "Shader pack {} could not be installed: the cache starts empty",
                    pack.string());
        Clear();
        return 0;
    }
    // The profile last: a pack cut short by a crash reads as no cache (it is rebuilt).
    if (!WriteFileAtomic(cache_path / "profile.bin", pack_profile)) {
        Clear();
        return 0;
    }
    if (skipped) {
        LOG_WARNING(Render, "Shader pack {}: {} entries that are no cache blobs were left out",
                    pack.string(), skipped);
    }
    return written + 1;
}

std::filesystem::path DataBase::DriverCachePath() const {
    // The extension is none of the blob extensions, so ForEachBlob never reads it as one.
    return cache_path / "vk_pipeline_cache.vkc";
}

bool DataBase::LoadDriverCache(std::vector<u8>& data) {
    if (!opened || EmulatorSettings.IsPipelineCacheArchived()) {
        return false;
    }
    using namespace Common::FS;
    std::error_code ec;
    const auto path = DriverCachePath();
    if (!std::filesystem::is_regular_file(path, ec)) {
        return false;
    }
    const auto file = IOFile{path, FileAccessMode::Read};
    if (!file.IsOpen()) {
        return false;
    }
    data.resize(file.GetSize());
    return file.Read(data) == data.size();
}

bool DataBase::SaveDriverCache(const std::vector<u8>& data) {
    if (!opened || EmulatorSettings.IsPipelineCacheArchived()) {
        return false;
    }
    using namespace Common::FS;
    const auto path = DriverCachePath();
    auto temp = path;
    temp += ".tmp";
    bool written = false;
    {
        const auto file = IOFile{temp, FileAccessMode::Create};
        written = file.IsOpen() && file.Write(data) == data.size();
    }
    std::error_code ec;
    if (written) {
        std::filesystem::rename(temp, path, ec);
    }
    if (!written || ec) {
        LOG_ERROR(Render, "Failed to write {}", path.string());
        std::filesystem::remove(temp, ec);
        return false;
    }
    return true;
}

void DataBase::DeleteDriverCache() {
    if (!opened || EmulatorSettings.IsPipelineCacheArchived()) {
        return;
    }
    std::error_code ec;
    std::filesystem::remove(DriverCachePath(), ec);
}

void DataBase::FinishPreload() {
    if (EmulatorSettings.IsPipelineCacheArchived()) {
        mz_zip_writer_init_from_reader(&zip_ar, cache_path.string().c_str());
        ar_is_read_only = false;
    }
}

} // namespace Storage
