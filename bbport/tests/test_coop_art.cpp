// SPDX-License-Identifier: GPL-2.0-or-later
// bbport co-op: the co-op screens' own art the overlay embeds (gpu/art/coop_art.bin) decodes to every
// piece, each inside the atlas with a drawn middle and a clear ground where it should have one; the
// card's parts lie on its frame; a damaged atlas decodes to nothing.
#include <cassert>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <vector>

#include "gpu/shim/bbport_coop_art.h"

using namespace BbCoopArt;

namespace {

std::vector<std::uint8_t> ReadFile(const char* path) {
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>{file}, {}};
}

std::uint8_t AlphaAt(const Atlas& atlas, Piece piece, float sx, float sy) {
    const Rect& r = atlas.rects[std::size_t(piece)];
    const int x = r.x + int(sx * float(r.width - 1)), y = r.y + int(sy * float(r.height - 1));
    return atlas.rgba[(std::size_t(y) * std::size_t(atlas.width) + std::size_t(x)) * 4 + 3];
}

} // namespace

int main() {
    const std::vector<std::uint8_t> file = ReadFile(BB_COOP_ART_PATH);
    assert(!file.empty());
    const auto atlas = Decode(file.data(), file.size());
    assert(atlas);
    assert(atlas->rects.size() == std::size_t(Piece::Count));
    // The round pieces: clear corners. The medallions: a clear middle where the sigil goes, a drawn
    // ring. The sigils and marks: a drawn middle.
    for (const Piece piece : {Piece::Sigil0, Piece::Sigil1, Piece::Sigil2, Piece::Sigil3, Piece::Medallion,
                              Piece::MedallionReady, Piece::MarkReady, Piece::MarkLeader, Piece::MarkInvite}) {
        assert(AlphaAt(*atlas, piece, 0.01f, 0.01f) == 0);
        assert(AlphaAt(*atlas, piece, 0.99f, 0.99f) == 0);
    }
    for (const Piece piece : {Piece::Medallion, Piece::MedallionReady}) {
        assert(AlphaAt(*atlas, piece, 0.5f, 0.5f) == 0);
    }
    // The two medallions' rings are drawn at one scale (the ready one has a ring more round it).
    const Rect& plain = atlas->rects[std::size_t(Piece::Medallion)];
    const Rect& ready = atlas->rects[std::size_t(Piece::MedallionReady)];
    assert(ready.width > plain.width && ready.width < plain.width * 5 / 4);
    // The card: opaque inside and on its frame; its corner ornament, arch and flourish are drawn.
    assert(AlphaAt(*atlas, Piece::Card, 0.5f, 0.5f) == 255);
    assert(AlphaAt(*atlas, Piece::Card, 0.003f, 0.5f) > 200);
    assert(AlphaAt(*atlas, Piece::Card, 0.5f, 0.002f) > 200);
    for (const Share& part : {CardParts::Top, CardParts::Bottom, CardParts::Left, CardParts::Right,
                              CardParts::Inside, CardParts::Arch, CardParts::Flourish}) {
        assert(part.x0 >= 0.0f && part.x1 <= 1.0f && part.x0 < part.x1);
        assert(part.y0 >= 0.0f && part.y1 <= 1.0f && part.y0 < part.y1);
    }
    // The backdrop: a picture, opaque everywhere.
    assert(AlphaAt(*atlas, Piece::Backdrop, 0.0f, 0.0f) == 255 && AlphaAt(*atlas, Piece::Backdrop, 1.0f, 1.0f) == 255);
    // Damaged: nothing.
    std::vector<std::uint8_t> damaged = file;
    damaged[0] = 'X';
    assert(!Decode(damaged.data(), damaged.size()));
    damaged = file;
    damaged[8] = 3; // another piece count
    assert(!Decode(damaged.data(), damaged.size()));
    damaged = file;
    damaged[10] = 0xFF; // a piece outside the atlas
    damaged[11] = 0xFF;
    assert(!Decode(damaged.data(), damaged.size()));
    assert(!Decode(file.data(), 20));
    assert(!Decode(nullptr, 0));
    std::printf("Co-op art: PASS (%d pieces in a %dx%d atlas)\n", int(atlas->rects.size()), atlas->width,
                atlas->height);
    return 0;
}
