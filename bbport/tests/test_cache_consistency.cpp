// SPDX-License-Identifier: GPL-2.0-or-later
// Standalone CPU checks of the shader cache's consistency rules: no game, Vulkan device or window.
//   ninja -C out/gpu cache-consistency-test && out/gpu/cache-consistency-test.exe
// The same file with BB_CACHE_STORAGE_TEST also checks the blobs on disk (Storage::DataBase), which
// needs the port's GPU library for the file helpers and the log:
//   ninja -C out/gpu cache-storage-test && out/gpu/cache-storage-test.exe
#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>
#include "video_core/renderer_vulkan/vk_cache_consistency.h"
#ifdef BB_CACHE_STORAGE_TEST
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <span>
#include <string>
#include <thread>
#include "common/elf_info.h"
#include <miniz.h>
#include "common/path_util.h"
#include "video_core/cache_storage.h"

// ElfInfo's fields are private to Core::Emulator; the port fills them in shim/bbgpu.cpp, here the
// test names the game.
namespace Core {
class Emulator {
public:
    static void SetSerial(const char* serial) {
        auto& info = Common::ElfInfo::Instance();
        info.initialized = true;
        info.game_serial = serial;
    }
};
} // namespace Core
#endif

using namespace Vulkan::CacheCheck;
using Shader::Backend::Bindings;

namespace {

// Stage numbers as in Shader::SwStage.
constexpr size_t Fragment = 0, TessControl = 1, TessEval = 2, Vertex = 3;

StageBindings Stage(Bindings start, Bindings size, bool has_resources = true) {
    return {.present = true, .has_resources = has_resources, .start = start, .size = size};
}

#ifdef BB_CACHE_STORAGE_TEST
namespace fs = std::filesystem;

// Saves are queued to the cache's own thread: wait until the file shows up.
bool Appears(const fs::path& path) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (!fs::exists(path)) {
        if (std::chrono::steady_clock::now() > deadline) {
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return true;
}

// Always evaluated: most calls below have effects.
#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            std::printf("FAIL line %d: %s\n", __LINE__, #cond); \
            std::abort(); \
        } \
    } while (0)
// A zip of the given entries (name, bytes), as tools/shader_pack.py writes it.
void WriteZip(const fs::path& path, const std::vector<std::pair<std::string, std::string>>& entries) {
    mz_zip_archive zip{};
    mz_zip_zero_struct(&zip);
    CHECK(mz_zip_writer_init_file(&zip, path.string().c_str(), 0));
    for (const auto& [name, data] : entries) {
        CHECK(mz_zip_writer_add_mem(&zip, name.c_str(), data.data(), data.size(),
                                    MZ_BEST_COMPRESSION));
    }
    CHECK(mz_zip_writer_finalize_archive(&zip));
    CHECK(mz_zip_writer_end(&zip));
}

std::string ReadAll(const fs::path& path) {
    std::ifstream file{path, std::ios::binary};
    return {std::istreambuf_iterator<char>{file}, {}};
}

// Shader packs (shader_packs/*.zip): installed whole into the cache only when its profile fits,
// with nothing but cache blobs, and an older cache is gone afterwards.
void CheckPackInstall(Storage::DataBase& db, const fs::path& dir, const fs::path& root) {
    const auto ours = [](std::span<const u8> blob) {
        return blob.size() == 3 && blob[0] == 'G' && blob[1] == 'P' && blob[2] == 'U';
    };
    const std::string key = "g_0x00000000000000aa.key", meta = "0x00000000000000bb.meta",
                      spv = "0x00000000000000cc_12.spv", compute = "c_0x00000000000000dd.key";
    const auto pack = root / "nvidia.zip";
    WriteZip(pack, {{"profile.bin", "GPU"},
                    {key, "k"},
                    {meta, "m"},
                    {spv, "s"},
                    {compute, "c"},
                    {"vk_pipeline_cache.vkc", "driver"},
                    {"../escape.key", "x"},
                    {"sub/0x00000000000000bb.meta", "x"},
                    {"0x00000000000000ee.key.tmp", "x"}});
    const auto other_gpu = root / "amd.zip";
    WriteZip(other_gpu, {{"profile.bin", "AMD"}, {key, "other"}});
    const auto no_profile = root / "plain.zip";
    WriteZip(no_profile, {{key, "k"}});
    std::ofstream{root / "broken.zip", std::ios::binary} << "not a zip";

    // The cache as a session left it: one blob of an older generation and the driver's cache.
    std::ofstream{dir / "0x0000000000000099.meta", std::ios::binary} << "old";
    CHECK(db.SaveDriverCache(std::vector<u8>{7}));

    CHECK(db.InstallPack(other_gpu, ours) == 0);
    CHECK(db.InstallPack(no_profile, ours) == 0);
    CHECK(db.InstallPack(root / "broken.zip", ours) == 0);
    CHECK(db.InstallPack(root / "missing.zip", ours) == 0);
    // None of them touched the cache.
    CHECK(fs::exists(dir / "0x0000000000000099.meta") && fs::exists(dir / "vk_pipeline_cache.vkc"));
    CHECK(!fs::exists(dir / key));

    CHECK(db.InstallPack(pack, ours) == 5); // four blobs and the profile
    CHECK(ReadAll(dir / key) == "k" && ReadAll(dir / meta) == "m" && ReadAll(dir / spv) == "s" &&
          ReadAll(dir / compute) == "c" && ReadAll(dir / "profile.bin") == "GPU");
    CHECK(!fs::exists(dir / "0x0000000000000099.meta") && !fs::exists(dir / "vk_pipeline_cache.vkc"));
    CHECK(!fs::exists(root / "escape.key") && !fs::exists(dir / "sub") &&
          !fs::exists(dir / "0x00000000000000ee.key.tmp"));
    size_t files = 0;
    for ([[maybe_unused]] const auto& entry : fs::directory_iterator{dir}) {
        ++files;
    }
    CHECK(files == 5);
    CHECK(db.HasBlobs(Storage::BlobType::PipelineKey) && db.HasBlobs(Storage::BlobType::ShaderMeta));
    // What WarmUp reads next is the pack's own blobs under the names it loads them by.
    std::vector<u8> profile;
    db.Load(Storage::BlobType::ShaderProfile, "profile", profile);
    CHECK(ours(profile));
    std::map<std::string, std::vector<u8>> keys;
    db.ForEachBlob(Storage::BlobType::PipelineKey,
                   [&](const std::string& name, std::vector<u8>&& data) { keys[name] = data; });
    CHECK(keys.size() == 2 && keys.contains("g_0x00000000000000aa") &&
          keys.contains("c_0x00000000000000dd"));
    std::vector<u32> binary;
    db.Load(Storage::BlobType::ShaderBinary, "0x00000000000000cc_12", binary);
    CHECK(binary.empty()); // one byte is no SPIR-V word: what the loader rejects as damaged
    for (const auto& name : {key, meta, spv, compute, std::string{"profile.bin"}}) {
        fs::remove(dir / name);
    }
    CHECK(!db.HasBlobs(Storage::BlobType::PipelineKey));
    std::puts("  shader packs: profile check, cache blobs only, old cache replaced: PASS");
}

// What WarmUp relies on in the cache on disk: ForEachBlob hands out each blob under its file
// name without the extension (the name Save and Remove take), and Remove deletes that blob and
// nothing else. The port always runs with a cache directory (EmulatorSettings::
// IsPipelineCacheArchived() is fixed to false in shim/core/emulator_settings.h), so the
// "Remove does nothing in an archive" branch cannot be reached from a test.
void CheckBlobsOnDisk() {
    using Storage::BlobType;
    const auto root = fs::temp_directory_path() /
                      ("bbport-cache-test-" +
                       std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(root);
    Common::FS::SetUserPath(Common::FS::PathType::CacheDir, root);
    Core::Emulator::SetSerial("CUSATEST0");
    auto& db = Storage::DataBase::Instance();
    CHECK(!db.Remove(BlobType::PipelineKey, "0x0000000000000001")); // not open
    db.Open();
    const auto dir = root / "CUSATEST0";
    CHECK(fs::is_directory(dir));

    const std::string first = "0x0000000000000001", second = "0x0000000000000002";
    CHECK(db.Save(BlobType::PipelineKey, first, std::vector<u8>{1, 2, 3}));
    CHECK(db.Save(BlobType::PipelineKey, second, std::vector<u8>{4}));
    CHECK(db.Save(BlobType::ShaderMeta, first, std::vector<u8>{9, 9}));
    CHECK(Appears(dir / (first + ".key")) && Appears(dir / (second + ".key")) &&
           Appears(dir / (first + ".meta")));
    // A write cut short (its temporary name) and the driver's cache are no blobs.
    std::ofstream{dir / ("0x0000000000000003.key.tmp"), std::ios::binary} << "half";
    CHECK(db.SaveDriverCache(std::vector<u8>{7}));

    std::map<std::string, std::vector<u8>> seen;
    db.ForEachBlob(BlobType::PipelineKey, [&](const std::string& name, std::vector<u8>&& data) {
        seen[name] = std::move(data);
    });
    CHECK(seen.size() == 2);
    CHECK(seen.at(first) == (std::vector<u8>{1, 2, 3}) && seen.at(second) == std::vector<u8>{4});
    std::map<std::string, std::vector<u8>> metas;
    db.ForEachBlob(BlobType::ShaderMeta, [&](const std::string& name, std::vector<u8>&& data) {
        metas[name] = std::move(data);
    });
    CHECK(metas.size() == 1 && metas.at(first) == (std::vector<u8>{9, 9}));

    // Remove: that blob of that type, once.
    CHECK(db.Remove(BlobType::PipelineKey, first));
    CHECK(!fs::exists(dir / (first + ".key")) && fs::exists(dir / (first + ".meta")) &&
           fs::exists(dir / (second + ".key")));
    CHECK(!db.Remove(BlobType::PipelineKey, first));
    // The names ForEachBlob gave are the ones Remove takes, as in WarmUp's sweep of unused entries.
    std::vector<std::string> names;
    db.ForEachBlob(BlobType::PipelineKey,
                   [&](const std::string& name, std::vector<u8>&&) { names.push_back(name); });
    CHECK(names == std::vector<std::string>{second});
    for (const auto& name : names) {
        CHECK(db.Remove(BlobType::PipelineKey, name));
    }
    db.ForEachBlob(BlobType::PipelineKey, [&](const std::string&, std::vector<u8>&&) { CHECK(false); });
    CHECK(fs::exists(dir / "0x0000000000000003.key.tmp") && fs::exists(dir / (first + ".meta")));

    db.Close();
    CHECK(!db.Remove(BlobType::ShaderMeta, first)); // closed again
    CHECK(fs::exists(dir / (first + ".meta")));

    db.Open();
    CheckPackInstall(db, dir, root);
    db.Close();
    std::error_code ec;
    fs::remove_all(root, ec);
    std::puts("  blobs on disk: ForEachBlob names, Remove: PASS");
}
#endif

} // namespace

int main() {
    std::array<StageBindings, 6> stages{};

    // A fragment shader of 19 descriptors (7 buffers) and the vertex shader compiled behind it.
    stages[Fragment] = Stage({0, 0}, {19, 7});
    stages[Vertex] = Stage({19, 7}, {5, 5});
    assert(FirstMisplacedStage(stages) == -1);

    // The same vertex shader read back from a cache in which a later session wrote another
    // permutation under its name: it was compiled behind a fragment shader of 17 descriptors. The
    // layout built from the fragment shader gives binding 17 to a fragment sampler (the logged
    // VUID-VkGraphicsPipelineCreateInfo-layout-07988 of the shipped build).
    stages[Vertex] = Stage({17, 7}, {5, 5});
    assert(FirstMisplacedStage(stages) == int(Vertex));

    // Both counters are checked (descriptors and buffers).
    stages[Vertex] = Stage({19, 6}, {5, 5});
    assert(FirstMisplacedStage(stages) == int(Vertex));

    // A vertex-only pipeline whose vertex shader was compiled behind a fragment shader.
    stages = {};
    stages[Vertex] = Stage({3, 1}, {1, 1});
    assert(FirstMisplacedStage(stages) == int(Vertex));
    stages[Vertex] = Stage({0, 0}, {1, 1});
    assert(FirstMisplacedStage(stages) == -1);

    // A stage without resources keeps whatever start it was compiled with: it names no descriptor.
    stages = {};
    stages[Fragment] = Stage({0, 0}, {4, 2});
    stages[Vertex] = Stage({9, 9}, {0, 0}, false);
    assert(FirstMisplacedStage(stages) == -1);
    // ... but it still adds its own size for the stages after it.
    stages = {};
    stages[Fragment] = Stage({0, 0}, {4, 2});
    stages[TessControl] = Stage({9, 9}, {2, 0}, false);
    stages[TessEval] = Stage({6, 2}, {1, 1});
    stages[Vertex] = Stage({7, 3}, {1, 1});
    assert(FirstMisplacedStage(stages) == -1);
    stages[Vertex] = Stage({6, 3}, {1, 1});
    assert(FirstMisplacedStage(stages) == int(Vertex));

    // Absent stages add nothing, and no stage at all is consistent.
    stages = {};
    assert(FirstMisplacedStage(stages) == -1);
    stages[Fragment] = Stage({0, 0}, {2, 1});
    stages[Vertex] = Stage({2, 1}, {1, 1});
    assert(FirstMisplacedStage(stages) == -1);

    // A compute pipeline is one stage that starts at zero.
    const std::array<StageBindings, 1> compute{Stage({0, 0}, {8, 4})};
    assert(FirstMisplacedStage(compute) == -1);
    const std::array<StageBindings, 1> late_compute{Stage({1, 0}, {8, 4})};
    assert(FirstMisplacedStage(late_compute) == 0);

    // The profile blob: versions first, then the profile, and nothing else.
    constexpr size_t profile_size = 24;
    const std::array<u32, 4> versions{0x42425043u, 10, 7, 6};
    std::vector<u8> blob(sizeof(versions) + profile_size, 0xAB);
    std::memcpy(blob.data(), versions.data(), sizeof(versions));
    assert(HeaderMatches(blob, versions, profile_size));
    const std::array<u32, 4> bumped{0x42425043u, 11, 7, 6};
    assert(!HeaderMatches(blob, bumped, profile_size));
    // A blob of an older build holds the profile alone.
    assert(!HeaderMatches(std::vector<u8>(profile_size, 0xAB), versions, profile_size));
    blob.push_back(0);
    assert(!HeaderMatches(blob, versions, profile_size));
    assert(!HeaderMatches(std::vector<u8>{}, versions, profile_size));

    // Shader pack entries: the cache's own blob names, nothing else.
    assert(IsPackEntryName("profile.bin"));
    assert(IsPackEntryName("g_0x0123456789abcdef.key"));
    assert(IsPackEntryName("c_0x0123456789abcdef.key"));
    assert(IsPackEntryName("0x0123456789abcdef.meta"));
    assert(IsPackEntryName("0x0123456789abcdef_0.spv"));
    assert(IsPackEntryName("0x0123456789abcdef_1234.spv"));
    assert(!IsPackEntryName("vk_pipeline_cache.vkc"));
    assert(!IsPackEntryName("g_0x0123456789abcdef.key.tmp"));
    assert(!IsPackEntryName("../g_0x0123456789abcdef.key"));
    assert(!IsPackEntryName("sub/0x0123456789abcdef.meta"));
    assert(!IsPackEntryName("sub\\0x0123456789abcdef.meta"));
    assert(!IsPackEntryName("0x0123456789ABCDEF.meta"));
    assert(!IsPackEntryName("0x0123456789abcde.meta"));
    assert(!IsPackEntryName("x_0x0123456789abcdef.key"));
    assert(!IsPackEntryName("0x0123456789abcdef.key"));
    assert(!IsPackEntryName("0x0123456789abcdef_.spv"));
    assert(!IsPackEntryName("0x0123456789abcdef_12345.spv"));
    assert(!IsPackEntryName("0x0123456789abcdef_1a.spv"));
    assert(!IsPackEntryName("0x0123456789abcdef.spv"));
    assert(!IsPackEntryName("profile.bin.tmp"));
    assert(!IsPackEntryName(""));

#ifdef BB_CACHE_STORAGE_TEST
    CheckBlobsOnDisk();
#endif
    std::puts("PASS");
    return 0;
}
