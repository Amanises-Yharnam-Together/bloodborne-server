// The prompt sets file (gpu/shim/bbport_prompt_sets.h) as launcher/bbport_icons.py writes it, and the
// sets controllers map to. No game, GPU or window.
//   ninja -C out/gpu prompt-sets-test && out/gpu/prompt-sets-test.exe [prompts-*.bin]
#include <cassert>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>
#include "bbport_prompt_sets.h"

using namespace BbPromptSets;

namespace {

void Put32(std::vector<std::uint8_t>& out, std::uint32_t v) {
    for (int i = 0; i < 4; ++i) {
        out.push_back(std::uint8_t(v >> (8 * i)));
    }
}

void PutText(std::vector<std::uint8_t>& out, std::string_view text) {
    out.push_back(std::uint8_t(text.size()));
    out.insert(out.end(), text.begin(), text.end());
}

/// A file with one glyph (2 pieces) and one atlas button (1 piece); set s's bytes are s + 1.
std::vector<std::uint8_t> Sample() {
    std::vector<std::uint8_t> out = {'B', 'B', 'P', 'R', 'M', 'P', 'T', '1'};
    Put32(out, SetCount);
    Put32(out, 2);
    for (const auto name : SetNames) {
        PutText(out, name);
    }
    PutText(out, "KG_OK");
    Put32(out, 32);
    Put32(out, 32);
    Put32(out, 2);
    Put32(out, 0);
    Put32(out, 16);
    for (int s = 0; s < SetCount; ++s) {
        out.insert(out.end(), 32, std::uint8_t(s + 1));
    }
    PutText(out, "MENU_Common_00091:KG_L3");
    Put32(out, 1024);
    Put32(out, 512);
    Put32(out, 1);
    Put32(out, 4096);
    for (int s = 0; s < SetCount; ++s) {
        out.insert(out.end(), 16, std::uint8_t(0x10 + s));
    }
    return out;
}

} // namespace

int main(int argc, char** argv) {
    const auto data = Sample();
    const auto entries = Parse(data);
    assert(entries && entries->size() == 2);
    const Entry& glyph = (*entries)[0];
    assert(glyph.name == "KG_OK" && glyph.IsGlyph() && glyph.width == 32 && glyph.height == 32);
    assert(glyph.offsets == (std::vector<std::uint32_t>{0, 16}) && glyph.First() == 0 && glyph.Span() == 32);
    assert(glyph.pieces[Switch] == std::vector<std::uint8_t>(32, 4));
    const Entry& atlas = (*entries)[1];
    assert(!atlas.IsGlyph() && atlas.width == 1024 && atlas.First() == 4096 && atlas.Span() == 16);
    assert(atlas.pieces[Keyboard] == std::vector<std::uint8_t>(16, 0x12));

    // Anything else is no file: another magic, a short or long file, sets out of order, a
    // piece offset that is not a whole block.
    auto bad = data;
    bad[0] = 'X';
    assert(!Parse(bad));
    assert(!Parse(std::span(data).first(data.size() - 1)));
    bad = data;
    bad.push_back(0);
    assert(!Parse(bad));
    bad = data;
    std::swap(bad[17], bad[18]); // "playstation" -> "lpaystation"
    assert(!Parse(bad));
    bad = data;
    bad[16 + 1 + 11 + 1 + 4 + 1 + 8 + 1 + 6 + 1 + 5 + 12 + 4] = 8; // the second offset 16 -> 8
    assert(!Parse(bad));

    // Controllers: PlayStation pads, the Switch's, anything else named as an Xbox pad.
    assert(ForGamepadType(5) == PlayStation && ForGamepadType(6) == PlayStation);
    assert(ForGamepadType(7) == Switch && ForGamepadType(10) == Switch);
    assert(ForGamepadType(0) == Xbox && ForGamepadType(3) == Xbox && ForGamepadType(11) == Xbox);
    assert(Letter(PlayStation) == 'p' && Letter(Xbox) == 'x' && Letter(Keyboard) == 'k' && Letter(Switch) == 's');

    // A file bbport_icons.py wrote (from the game's own common.tpf.dcx), when given.
    if (argc > 1) {
        std::ifstream in(argv[1], std::ios::binary);
        const std::vector<std::uint8_t> file{std::istreambuf_iterator<char>(in), {}};
        const auto real = Parse(file);
        assert(real);
        int glyphs = 0;
        for (const Entry& e : *real) {
            glyphs += e.IsGlyph();
            assert(e.pieces[PlayStation] != e.pieces[Xbox] || !e.IsGlyph());
        }
        assert(glyphs == 20);
        std::printf("prompt-sets-test: %s: %zu pictures\n", argv[1], real->size());
    }
    std::printf("prompt-sets-test: all checks passed\n");
    return 0;
}
