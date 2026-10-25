// Button icons that follow the device in use (gpu/shim/bbport_prompts.cpp): the game's pictures are
// found again when it loads them anew, whatever the memory held before. Guest memory is a host
// buffer here (addresses are its pointers). No game, GPU or window.
//   ninja -C out/gpu prompts-test && out/gpu/prompts-test.exe
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include "bbport_prompts.h"

using namespace BbPromptSets;

extern "C" void runtime_memory_read_backing(uintptr_t address, void* data, uint64_t size) {
    std::memcpy(data, reinterpret_cast<const void*>(address), size);
}
extern "C" int runtime_memory_is_mapped(uintptr_t, uint64_t) {
    return 1;
}
extern "C" void runtime_memory_note_write(uintptr_t, uint64_t) {}

namespace {

constexpr std::uint32_t AtlasPieces = 4;
constexpr std::uint32_t AtlasFirst = 4096; ///< the atlas button's first block in its texture
constexpr std::uint32_t AtlasSize = 8192;

/// Set s's byte i of an entry's pieces (each entry and set different).
std::uint8_t Byte(int entry, int s, std::size_t i) {
    return std::uint8_t(entry * 101 + s * 37 + i * 13 + 1);
}

void Put32(std::vector<std::uint8_t>& out, std::uint32_t v) {
    for (int i = 0; i < 4; ++i) {
        out.push_back(std::uint8_t(v >> (8 * i)));
    }
}

void PutText(std::vector<std::uint8_t>& out, std::string_view text) {
    out.push_back(std::uint8_t(text.size()));
    out.insert(out.end(), text.begin(), text.end());
}

void PutEntry(std::vector<std::uint8_t>& out, int entry, std::string_view name, std::uint32_t size,
              const std::vector<std::uint32_t>& offsets) {
    PutText(out, name);
    Put32(out, size);
    Put32(out, size);
    Put32(out, std::uint32_t(offsets.size()));
    for (const std::uint32_t o : offsets) {
        Put32(out, o);
    }
    for (int s = 0; s < SetCount; ++s) {
        for (std::size_t i = 0; i < offsets.size() * 16; ++i) {
            out.push_back(Byte(entry, s, i));
        }
    }
}

/// A glyph (KG_OK, 32x32: 64 pieces) and a button baked into a 64x64 atlas (4 pieces).
std::string WriteSets() {
    std::vector<std::uint8_t> out = {'B', 'B', 'P', 'R', 'M', 'P', 'T', '1'};
    Put32(out, SetCount);
    Put32(out, 2);
    for (const auto name : SetNames) {
        PutText(out, name);
    }
    std::vector<std::uint32_t> glyph, atlas;
    for (std::uint32_t o = 0; o < 1024; o += 16) {
        glyph.push_back(o);
    }
    for (std::uint32_t i = 0; i < AtlasPieces; ++i) {
        atlas.push_back(AtlasFirst + i * 16);
    }
    PutEntry(out, 0, "KG_OK", 32, glyph);
    PutEntry(out, 1, "MENU_Common_00091:KG_OK", 64, atlas);
    const char* folder = std::getenv("TEMP");
    std::string path = std::string(folder && *folder ? folder : "/tmp") + "/bbport-prompts-test.bin";
    FILE* f = std::fopen(path.c_str(), "wb");
    assert(f);
    std::fwrite(out.data(), 1, out.size(), f);
    std::fclose(f);
    return path;
}

/// The game puts a set's glyph at `at`.
void LoadGlyph(std::uint8_t* at, int s) {
    for (std::size_t i = 0; i < 1024; ++i) {
        at[i] = Byte(0, s, i);
    }
}

bool GlyphIs(const std::uint8_t* at, int s) {
    for (std::size_t i = 0; i < 1024; ++i) {
        if (at[i] != Byte(0, s, i)) {
            return false;
        }
    }
    return true;
}

void LoadAtlas(std::uint8_t* at, int s) {
    std::memset(at, 0x55, AtlasSize);
    for (std::size_t i = 0; i < AtlasPieces * 16; ++i) {
        at[AtlasFirst + i] = Byte(1, s, i);
    }
}

bool AtlasIs(const std::uint8_t* at, int s) {
    for (std::size_t i = 0; i < AtlasPieces * 16; ++i) {
        if (at[AtlasFirst + i] != Byte(1, s, i)) {
            return false;
        }
    }
    return true;
}

void Upload(const std::uint8_t* at, std::size_t size, std::uint32_t width) {
    BbPrompts::OnUpload(reinterpret_cast<std::uintptr_t>(at), size, width, width, 1, 128);
}

} // namespace

int main() {
    const std::string path = WriteSets();
#ifdef _WIN32
    _putenv_s("BB_PROMPT_SETS", path.c_str());
#else
    setenv("BB_PROMPT_SETS", path.c_str(), 1);
#endif
    assert(BbPrompts::Active());
    BbPrompts::OnInput(Xbox);

    // A glyph is swapped once found.
    std::vector<std::uint8_t> memory(1 << 16);
    std::uint8_t* glyph = memory.data();
    LoadGlyph(glyph, PlayStation);
    Upload(glyph, 1024, 32);
    BbPrompts::Apply();
    assert(GlyphIs(glyph, Xbox));

    // The game loads its file again at the same place (back to the title, a new area): the
    // PlayStation glyph is swapped again, with no change of device.
    LoadGlyph(glyph, PlayStation);
    Upload(glyph, 1024, 32);
    BbPrompts::Apply();
    assert(GlyphIs(glyph, Xbox));

    // An atlas whose memory first held another texture of its size is found once the game's
    // atlas is there.
    std::uint8_t* atlas = memory.data() + 16384;
    std::memset(atlas, 0x55, AtlasSize);
    Upload(atlas, AtlasSize, 64);
    BbPrompts::Apply();
    LoadAtlas(atlas, PlayStation);
    Upload(atlas, AtlasSize, 64);
    BbPrompts::Apply();
    assert(AtlasIs(atlas, Xbox));

    // ...and again after the game loads it anew.
    LoadAtlas(atlas, PlayStation);
    Upload(atlas, AtlasSize, 64);
    BbPrompts::Apply();
    assert(AtlasIs(atlas, Xbox));

    // Our own write uploads with our set: nothing changes, and a change of device still follows.
    Upload(atlas, AtlasSize, 64);
    Upload(glyph, 1024, 32);
    BbPrompts::OnInput(Keyboard);
    BbPrompts::Apply();
    assert(GlyphIs(glyph, Keyboard) && AtlasIs(atlas, Keyboard));

    std::remove(path.c_str());
    std::printf("prompts-test: ok\n");
    return 0;
}
