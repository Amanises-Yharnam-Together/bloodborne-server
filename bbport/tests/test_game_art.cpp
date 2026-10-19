// SPDX-License-Identifier: GPL-2.0-or-later
// bbport co-op: reading the game's HUD art from its files (bbport_game_art.h), on made-up data:
// the PS4 block order, BC7 blocks, a TPF's entries and a DCX's zlib payload.
#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include "gpu/shim/bbport_game_art.h"
#include "miniz.h"

using namespace BbGameArt;

namespace {

/// BC7 bits written least significant first, as the format reads them.
struct BitWriter {
    std::array<std::uint8_t, 16> block{};
    int at = 0;
    void Put(int value, int count) {
        for (int i = 0; i < count; ++i, ++at) {
            block[at >> 3] |= std::uint8_t(((value >> i) & 1) << (at & 7));
        }
    }
};

/// A mode 6 block (one subset, 7-bit RGBA endpoints with a p-bit each, 4-bit indices): black and
/// opaque at endpoint 0, white at endpoint 1, every pixel's index `index_of(pixel)`.
template <typename IndexOf>
std::array<std::uint8_t, 16> Mode6Block(IndexOf index_of) {
    BitWriter bits;
    bits.Put(1 << 6, 7);
    for (int channel = 0; channel < 3; ++channel) {
        bits.Put(0, 7);
        bits.Put(127, 7);
    }
    bits.Put(127, 7); // alpha 0
    bits.Put(127, 7); // alpha 1
    bits.Put(0, 1);   // p-bit 0: 0x00 for colour, 0xFE for alpha
    bits.Put(1, 1);   // p-bit 1: 0xFF
    for (int pixel = 0; pixel < 16; ++pixel) {
        bits.Put(index_of(pixel), pixel == 0 ? 3 : 4);
    }
    return bits.block;
}

void PutLe32(std::vector<std::uint8_t>& bytes, std::size_t at, std::uint32_t value) {
    for (int i = 0; i < 4; ++i) {
        bytes[at + i] = std::uint8_t(value >> (8 * i));
    }
}

void DecodesBc7() {
    const auto block = Mode6Block([](int pixel) { return pixel == 0 ? 0 : pixel == 1 ? 15 : 7; });
    const auto pixels = DecodeBc7Block(block);
    assert(pixels[0] == 0 && pixels[1] == 0 && pixels[2] == 0 && pixels[3] == 0xFE);
    assert(pixels[4] == 255 && pixels[5] == 255 && pixels[6] == 255 && pixels[7] == 255);
    // Index 7 of 15: weight 30 of 64 between 0 and 255.
    assert(pixels[8] == 120 && pixels[11] == 0xFE + 0);
    // A reserved mode (no bit set in the first byte's mode field) is transparent black.
    const std::array<std::uint8_t, 16> reserved{};
    for (const std::uint8_t value : DecodeBc7Block(reserved)) {
        assert(value == 0);
    }
}

void PutsBlocksBackInRowOrder() {
    // 16x8 one-byte blocks numbered in the PS4's order: two 8x8 tiles, Morton inside each.
    std::vector<std::uint8_t> tiled(16 * 8);
    for (std::size_t i = 0; i < tiled.size(); ++i) {
        tiled[i] = std::uint8_t(i);
    }
    const auto rows = DeswizzleBlocks(tiled, 16, 8, 1);
    assert(rows[0] == 0);           // (0, 0)
    assert(rows[1] == 1);           // (1, 0): x is the low bit
    assert(rows[16] == 2);          // (0, 1)
    assert(rows[17] == 3);          // (1, 1)
    assert(rows[2] == 4);           // (2, 0)
    assert(rows[7 * 16 + 7] == 63); // the first tile's last block
    assert(rows[8] == 64);          // the second tile starts at x 8
    // A texture narrower than a tile keeps only its own blocks; the tile's padding is skipped.
    const auto narrow = DeswizzleBlocks(tiled, 4, 2, 1);
    assert(narrow.size() == 8 && narrow[4] == 2 && narrow[7] == 7);
}

std::vector<std::uint8_t> OneTextureTpf(const char* name, std::uint32_t dxgi, int w, int h,
                                        const std::vector<std::uint8_t>& data) {
    const std::size_t name_at = 16 + 0x24, data_at = name_at + (std::strlen(name) + 1) * 2;
    std::vector<std::uint8_t> tpf(data_at + data.size());
    std::memcpy(tpf.data(), "TPF\0", 4);
    PutLe32(tpf, 8, 1);
    tpf[12] = 4; // PS4
    tpf[14] = 1; // UTF-16 names
    PutLe32(tpf, 16, std::uint32_t(data_at));
    PutLe32(tpf, 20, std::uint32_t(data.size()));
    tpf[16 + 12] = std::uint8_t(w);
    tpf[16 + 13] = std::uint8_t(w >> 8);
    tpf[16 + 14] = std::uint8_t(h);
    tpf[16 + 15] = std::uint8_t(h >> 8);
    PutLe32(tpf, 16 + 0x18, std::uint32_t(name_at));
    PutLe32(tpf, 16 + 0x20, dxgi);
    for (std::size_t i = 0; name[i]; ++i) {
        tpf[name_at + i * 2] = std::uint8_t(name[i]);
    }
    std::memcpy(tpf.data() + data_at, data.data(), data.size());
    return tpf;
}

std::vector<std::uint8_t> Dcx(const std::vector<std::uint8_t>& payload) {
    mz_ulong size = mz_compressBound(mz_ulong(payload.size()));
    std::vector<std::uint8_t> zlib(size);
    assert(mz_compress(zlib.data(), &size, payload.data(), mz_ulong(payload.size())) == MZ_OK);
    std::vector<std::uint8_t> file(0x48); // the header up to "DCA" and its size 8; then the stream
    std::memcpy(file.data(), "DCX\0", 4);
    std::memcpy(file.data() + 0x40, "DCA\0", 4);
    file[0x47] = 8;
    file.insert(file.end(), zlib.begin(), zlib.begin() + std::ptrdiff_t(size));
    return file;
}

void ReadsTheTpfInsideTheDcx() {
    const std::vector<std::uint8_t> data(32, 0xAB);
    const auto tpf = OneTextureTpf("MENU_Test", 98, 8, 4, data);
    const auto payload = InflateDcx(Dcx(tpf));
    assert(payload && *payload == tpf);
    const auto textures = ParseTpf(*payload);
    assert(textures && textures->size() == 1);
    const TpfTexture& texture = (*textures)[0];
    assert(texture.name == "MENU_Test" && texture.dxgi == 98);
    assert(texture.width == 8 && texture.height == 4 && texture.size == 32);
    assert((*payload)[texture.offset] == 0xAB);
    // Not a DCX or TPF, or one whose data runs past its end: nothing.
    assert(!InflateDcx(tpf));
    assert(!ParseTpf(data));
    auto cut = tpf;
    cut.resize(cut.size() - 1);
    assert(!ParseTpf(cut));
}

void FindsTheGaugeArt() {
    // A 1024x512 BC7 MENU_Common_00091 of white blocks, tiled: its top rows come back white.
    const auto white = Mode6Block([](int) { return 15; });
    std::vector<std::uint8_t> blocks;
    for (int i = 0; i < 256 * 128; ++i) {
        blocks.insert(blocks.end(), white.begin(), white.end());
    }
    const auto art = LoadGaugeArt(Dcx(OneTextureTpf("MENU_Common_00091", 98, 1024, 512, blocks)));
    assert(art && art->width == 656 && art->height == 48);
    assert(art->rgba.size() == std::size_t(656 * 48 * 4));
    // (A block's first pixel is its anchor: its index has a bit less and can't be 15.)
    assert(art->rgba[4] == 255 && art->rgba[art->rgba.size() - 1] == 255);
    // The parts sit in those rows.
    for (const PixelRect& part : {GaugeFrame, GaugeFill, GaugeRegain}) {
        assert(part.x0 >= 0 && part.x1 <= art->width && part.y0 >= 0 && part.y1 <= art->height);
    }
    // Another texture, or the same name in another format: nothing.
    assert(!LoadGaugeArt(Dcx(OneTextureTpf("MENU_Common_00090", 98, 1024, 512, blocks))));
    assert(!LoadGaugeArt(Dcx(OneTextureTpf("MENU_Common_00091", 71, 1024, 512, blocks))));
}

// Kaniel 2026-10-14: the team screens in the game's own art. BC1: two RGB565 colours and the two
// between them; the three-colour mode's index 3 is transparent.
void DecodesBc1() {
    // White (0xFFFF) and black (0x0000): four colours; pixel 0 index 0, pixel 1 index 1, pixel 2
    // index 2 (two thirds white), pixel 3 index 3 (one third).
    const std::array<std::uint8_t, 8> four{0xFF, 0xFF, 0x00, 0x00, 0b11100100, 0, 0, 0};
    const auto p = DecodeBc1Block(four);
    assert(p[0] == 255 && p[1] == 255 && p[2] == 255 && p[3] == 255);
    assert(p[4] == 0 && p[5] == 0 && p[6] == 0 && p[7] == 255);
    assert(p[8] == 170 && p[11] == 255);
    assert(p[12] == 85 && p[15] == 255);
    // Black first, white second: three colours, index 2 the middle, index 3 transparent.
    const std::array<std::uint8_t, 8> three{0x00, 0x00, 0xFF, 0xFF, 0b11100100, 0, 0, 0};
    const auto q = DecodeBc1Block(three);
    assert(q[0] == 0 && q[3] == 255 && q[4] == 255);
    assert(q[8] == 128 && q[11] == 255);
    assert(q[12] == 0 && q[15] == 0);
}

void CutsAPieceOfATexture() {
    // A 16x8 BC1 texture, tiled: the block at (2, 1) is white, the others black.
    std::vector<std::uint8_t> rows(4 * 2 * 8, 0);
    const std::array<std::uint8_t, 8> white{0xFF, 0xFF, 0xFF, 0xFF, 0, 0, 0, 0};
    std::memcpy(rows.data() + (1 * 4 + 2) * 8, white.data(), 8);
    // Back into the PS4's order: one 8x8-block tile, Morton inside it (x the low bit).
    std::vector<std::uint8_t> tiled(64 * 8, 0);
    for (int y = 0; y < 2; ++y) {
        for (int x = 0; x < 4; ++x) {
            int morton = 0;
            for (int bit = 0; bit < 3; ++bit) {
                morton |= ((x >> bit) & 1) << (2 * bit) | ((y >> bit) & 1) << (2 * bit + 1);
            }
            std::memcpy(tiled.data() + morton * 8, rows.data() + (y * 4 + x) * 8, 8);
        }
    }
    const auto file = TpfFile::Read(Dcx(OneTextureTpf("MENU_Title_00001", 71, 16, 8, tiled)));
    assert(file);
    const auto all = file->Piece("MENU_Title_00001");
    assert(all && all->width == 16 && all->height == 8 && all->rgba.size() == 16 * 8 * 4);
    const auto piece = file->Piece("MENU_Title_00001", PixelRect{6, 3, 10, 6});
    assert(piece && piece->width == 4 && piece->height == 3);
    // (6, 3) is in block (1, 0): black; (8, 4) in block (2, 1): white.
    assert(piece->rgba[0] == 0 && piece->rgba[3] == 255);
    const std::size_t white_at = (std::size_t(1) * 4 + 2) * 4;
    assert(piece->rgba[white_at] == 255 && piece->rgba[white_at + 3] == 255);
    assert(!file->Piece("MENU_Title_00002"));
    assert(!file->Piece("MENU_Title_00001", PixelRect{0, 0, 17, 8}));
}

/// SWF bits, most significant first.
struct SwfBits {
    std::vector<std::uint8_t> bytes;
    int bit = 0;
    void Put(std::uint32_t value, int count) {
        for (int i = count - 1; i >= 0; --i) {
            if (bit == 0) {
                bytes.push_back(0);
            }
            bytes.back() |= std::uint8_t(((value >> i) & 1) << (7 - bit));
            bit = (bit + 1) & 7;
        }
    }
};

void PutLe16(std::vector<std::uint8_t>& bytes, std::uint16_t value) {
    bytes.push_back(std::uint8_t(value));
    bytes.push_back(std::uint8_t(value >> 8));
}

std::uint32_t Be32(const std::vector<std::uint8_t>& bytes, std::size_t at) {
    return std::uint32_t(bytes[at]) << 24 | std::uint32_t(bytes[at + 1]) << 16 |
           std::uint32_t(bytes[at + 2]) << 8 | bytes[at + 3];
}

std::uint16_t Be16(const std::vector<std::uint8_t>& bytes, std::size_t at) {
    return std::uint16_t(bytes[at] << 8 | bytes[at + 1]);
}

void TurnsTheGameFontIntoTrueType() {
    // One glyph, 'A': a square 0..10240 twips (0..512 units) drawn as a move and four lines.
    SwfBits shape;
    shape.Put(1, 4); // fill bits
    shape.Put(0, 4); // line bits
    shape.Put(0, 1);
    shape.Put(0b00011, 5); // a move and fill style 0
    shape.Put(15, 5);      // move bits
    shape.Put(0, 15);
    shape.Put(0, 15);
    shape.Put(1, 1); // fill style 0
    const auto line = [&](bool vertical, int delta) {
        shape.Put(1, 1); // edge
        shape.Put(1, 1); // straight
        shape.Put(14, 4); // 16 bits
        shape.Put(0, 1);  // not general
        shape.Put(vertical ? 1 : 0, 1);
        shape.Put(std::uint32_t(delta) & 0xFFFF, 16);
    };
    line(false, 10240);
    line(true, -10240);
    line(false, -10240);
    line(true, 10240);
    shape.Put(0, 6); // end
    // DefineFont3 (tag 75): id, flags (layout, wide codes), language, name, one glyph.
    std::vector<std::uint8_t> font;
    PutLe16(font, 1);
    font.push_back(0x84);
    font.push_back(0);
    font.push_back(4);
    for (const char c : std::string("Test")) {
        font.push_back(std::uint8_t(c));
    }
    PutLe16(font, 1);
    PutLe16(font, 4);                          // the glyph's offset (after the table)
    PutLe16(font, std::uint16_t(4 + shape.bytes.size())); // the code table's
    font.insert(font.end(), shape.bytes.begin(), shape.bytes.end());
    PutLe16(font, 'A');
    PutLe16(font, 18000); // ascent
    PutLe16(font, 2400);  // descent
    PutLe16(font, 0);     // leading
    PutLe16(font, 12000); // the advance
    PutLe16(font, 0);     // bounds and kerning: none read
    // The GFX: "GFX", version, size, a 5-bit-zero RECT, frame rate and count, the tag, the end.
    std::vector<std::uint8_t> gfx{'G', 'F', 'X', 11, 0, 0, 0, 0, 0x00, 0, 24, 1, 0};
    PutLe16(gfx, std::uint16_t(75 << 6 | 0x3F));
    for (int i = 0; i < 4; ++i) {
        gfx.push_back(std::uint8_t(font.size() >> (8 * i)));
    }
    gfx.insert(gfx.end(), font.begin(), font.end());
    PutLe16(gfx, 0);
    const auto ttf = TtfFromGfxFont(gfx);
    assert(ttf && ttf->size() > 12);
    assert(Be32(*ttf, 0) == 0x10000);
    // The tables, by tag: two glyphs (.notdef and 'A'), 1024 units, 'A' mapped, its advance 600.
    std::size_t maxp = 0, head = 0, cmap = 0, hmtx = 0, glyf = 0;
    for (std::uint16_t i = 0, n = Be16(*ttf, 4); i < n; ++i) {
        const std::size_t entry = 12 + std::size_t(i) * 16;
        const std::string tag(ttf->begin() + std::ptrdiff_t(entry), ttf->begin() + std::ptrdiff_t(entry) + 4);
        const std::size_t at = Be32(*ttf, entry + 8);
        if (tag == "maxp") {
            maxp = at;
        } else if (tag == "head") {
            head = at;
        } else if (tag == "cmap") {
            cmap = at;
        } else if (tag == "hmtx") {
            hmtx = at;
        } else if (tag == "glyf") {
            glyf = at;
        }
    }
    assert(maxp && head && cmap && hmtx && glyf);
    assert(Be16(*ttf, maxp + 4) == 2);
    assert(Be16(*ttf, head + 18) == 1024);
    // cmap: one format 12 subtable, its one group 'A' -> glyph 1.
    const std::size_t sub = cmap + Be32(*ttf, cmap + 8);
    assert(Be16(*ttf, sub) == 12 && Be32(*ttf, sub + 12) == 1);
    assert(Be32(*ttf, sub + 16) == 'A' && Be32(*ttf, sub + 20) == 'A' && Be32(*ttf, sub + 24) == 1);
    assert(Be16(*ttf, hmtx + 4) == 600);
    // The glyph: one contour of four points, y up (the GFX's y runs down).
    assert(Be16(*ttf, glyf) == 1);
    assert(std::int16_t(Be16(*ttf, glyf + 2)) == 0 && std::int16_t(Be16(*ttf, glyf + 4)) == 0);
    assert(std::int16_t(Be16(*ttf, glyf + 6)) == 512 && std::int16_t(Be16(*ttf, glyf + 8)) == 512);
    assert(Be16(*ttf, glyf + 10) == 3);
    // Not a GFX: nothing.
    assert(!TtfFromGfxFont(font));
}

} // namespace

// `game-art-test <game folder> <out folder>`: the real files' font and title art written out
// (bbmenu.ttf, title.ppm) to look at.
int ConvertRealFiles(const char* game, const char* out) {
    const auto read = [](const std::string& path) {
        std::vector<std::uint8_t> bytes;
        if (FILE* f = std::fopen(path.c_str(), "rb")) {
            std::uint8_t chunk[65536];
            for (std::size_t n; (n = std::fread(chunk, 1, sizeof(chunk), f)) > 0;) {
                bytes.insert(bytes.end(), chunk, chunk + n);
            }
            std::fclose(f);
        }
        return bytes;
    };
    const std::string root = std::string(game) + "/dvdroot_ps4/menu/";
    const auto ttf = TtfFromGfxFont(read(root + "engus/font.gfx"));
    const auto title = TpfFile::Read(read(root + "title.tpf.dcx"));
    const auto art = title ? title->Piece("MENU_Title_00001") : std::nullopt;
    if (!ttf || !art) {
        std::puts("the font or the title art did not convert");
        return 1;
    }
    if (FILE* f = std::fopen((std::string(out) + "/bbmenu.ttf").c_str(), "wb")) {
        std::fwrite(ttf->data(), 1, ttf->size(), f);
        std::fclose(f);
    }
    if (FILE* f = std::fopen((std::string(out) + "/title.ppm").c_str(), "wb")) {
        std::fprintf(f, "P6 %d %d 255\n", art->width, art->height);
        for (std::size_t i = 0; i < art->rgba.size(); i += 4) {
            std::fwrite(art->rgba.data() + i, 1, 3, f);
        }
        std::fclose(f);
    }
    // The button icon and a row's scroll, raw RGBA, to check the BC7 decoder against a reference.
    const auto common = TpfFile::Read(read(root + "common.tpf.dcx"));
    for (const auto& [name, file] : {std::pair{"KG_Cancel", "kg_cancel.rgba"}, std::pair{"MENU_Common_00001", "common1.rgba"}}) {
        const auto piece = common ? common->Piece(name) : std::nullopt;
        if (piece) {
            if (FILE* f = std::fopen((std::string(out) + "/" + file).c_str(), "wb")) {
                std::fwrite(piece->rgba.data(), 1, piece->rgba.size(), f);
                std::fclose(f);
            }
        }
    }
    // Every key guide icon the screens draw is in the game's file, 32x32.
    for (int input = 0; !KeyGuideTexture(input).empty(); ++input) {
        const auto icon = common ? common->Piece(KeyGuideTexture(input)) : std::nullopt;
        if (!icon || icon->width != 32 || icon->height != 32) {
            std::printf("no key guide icon %.*s\n", int(KeyGuideTexture(input).size()), KeyGuideTexture(input).data());
            return 1;
        }
    }
    std::printf("wrote bbmenu.ttf (%zu bytes) and title.ppm (%dx%d)\n", ttf->size(), art->width,
                art->height);
    return 0;
}

/// Kaniel 2026-10-19: our screens show the game's own key guide icon for each button. KG_Cancel is
/// the game's Cross and KG_OK its Circle; Square and Triangle are KG_R_L and KG_R_U.
void NamesTheKeyGuideIcons() {
    const char* const expected[] = {"KG_Cancel", "KG_OK", "KG_R_L", "KG_R_U", "KG_L1", "KG_R1", "KG_L2",
                                    "KG_R2", "KG_L3", "KG_R3", "KG_Start", "KG_TP_L", "KG_TP_R", "KG_L_U",
                                    "KG_L_D", "KG_L_LR", "KG_L_LR", "KG_LS", "KG_LS", "KG_LS", "KG_LS",
                                    "KG_RS", "KG_RS", "KG_RS", "KG_RS"};
    for (int input = 0; input < int(std::size(expected)); ++input) {
        if (KeyGuideTexture(input) != expected[input]) {
            std::printf("input %d: %.*s, not %s\n", input, int(KeyGuideTexture(input).size()),
                        KeyGuideTexture(input).data(), expected[input]);
            assert(false);
        }
    }
    assert(KeyGuideTexture(-1).empty() && KeyGuideTexture(int(std::size(expected))).empty());
    // The names our texts write buttons by.
    assert(KeyGuideTextureNamed("Cross") == "KG_Cancel" && KeyGuideTextureNamed("Circle") == "KG_OK");
    assert(KeyGuideTextureNamed("Square") == "KG_R_L" && KeyGuideTextureNamed("Triangle") == "KG_R_U");
    assert(KeyGuideTextureNamed("Dpad") == "KG_L_UDLR" && KeyGuideTextureNamed("RS") == "KG_RS");
    assert(KeyGuideTextureNamed("TouchLeft") == "KG_TP_L" && KeyGuideTextureNamed("Options") == "KG_Start");
    assert(KeyGuideTextureNamed("cross").empty() && KeyGuideTextureNamed("").empty());
}

/// The game's own menus draw a button inline from <img src="KG_..." vspace='-8'>, as in its
/// controls page ("<img src="KG_R1" vspace='-8'>: Attack").
void PutsTheGamesIconsInItsMenuTexts() {
    assert(GameMenuIcons("Toggled with {Cross} + {L3}") ==
           "Toggled with <img src=\"KG_Cancel\" vspace='-8'> + <img src=\"KG_L3\" vspace='-8'>");
    assert(GameMenuIcons("{Circle}") == "<img src=\"KG_OK\" vspace='-8'>");
    // Braces that name no button stay as they are.
    assert(GameMenuIcons("a {b} {L1") == "a {b} {L1");
}

int main(int argc, char** argv) {
    if (argc == 3) {
        return ConvertRealFiles(argv[1], argv[2]);
    }
    DecodesBc7();
    PutsBlocksBackInRowOrder();
    ReadsTheTpfInsideTheDcx();
    FindsTheGaugeArt();
    DecodesBc1();
    CutsAPieceOfATexture();
    TurnsTheGameFontIntoTrueType();
    NamesTheKeyGuideIcons();
    PutsTheGamesIconsInItsMenuTexts();
    std::puts("Game art: PASS (BC7, BC1, PS4 block order, TPF in DCX, gauge texture, texture "
              "pieces, the game's font, key guide icons)");
    return 0;
}
