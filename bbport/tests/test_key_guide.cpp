// SPDX-License-Identifier: GPL-2.0-or-later
// bbport co-op: the key guide at the bottom of our screens stays inside its panel
// (bbport_key_guide.h): one centred line when it fits, a little smaller when it nearly does,
// else a second line; nothing ever runs past the panel's sides.
#include <cassert>
#include <cmath>
#include <cstdio>
#include "gpu/shim/bbport_key_guide.h"

using namespace BbKeyGuide;

namespace {

constexpr float IconGap = 6.0f, EntryGap = 28.0f;

/// Every entry inside [0, room] on its line, none overlapping the next.
void Inside(const std::vector<Entry>& entries, const Layout& layout, float room) {
    assert(layout.places.size() == entries.size());
    for (std::size_t i = 0; i < entries.size(); ++i) {
        const Entry& e = entries[i];
        const float w = (e.icons + (e.words > 0.0f ? IconGap : 0.0f) + e.words) * layout.scale;
        const Place& p = layout.places[i];
        assert(p.line >= 0 && p.line < layout.lines);
        assert(p.x >= -0.01f && p.x + w <= room + 0.01f);
        if (i + 1 < entries.size() && layout.places[i + 1].line == p.line) {
            assert(layout.places[i + 1].x >= p.x + w + EntryGap * layout.scale - 0.01f);
        }
    }
}

void OneLineCentredWhenItFits() {
    const std::vector<Entry> entries{{26, 100}, {26, 60}};
    const Layout layout = Arrange(entries, IconGap, EntryGap, 1000.0f);
    const float total = 26 + 6 + 100 + 28 + 26 + 6 + 60;
    assert(layout.scale == 1.0f && layout.lines == 1);
    assert(std::fabs(layout.places[0].x - (1000.0f - total) * 0.5f) < 0.01f);
    Inside(entries, layout, 1000.0f);
}

void SmallerWhenItNearlyFits() {
    // The map's guide, about 1500 px wide in a 1400 px panel: drawn at 1400/1500.
    const std::vector<Entry> entries{{60, 120}, {60, 110}, {60, 120}, {26, 60}, {26, 140},
                                     {26, 80}, {26, 60}, {26, 90}, {26, 80}};
    float total = EntryGap * float(entries.size() - 1);
    for (const Entry& e : entries) {
        total += e.icons + IconGap + e.words;
    }
    const float room = total * 0.93f;
    const Layout layout = Arrange(entries, IconGap, EntryGap, room);
    assert(layout.lines == 1 && layout.scale < 1.0f && layout.scale >= MinScale);
    Inside(entries, layout, room);
}

void SecondLineWhenItDoesNot() {
    // The settings menu's guide in Russian in a narrow window.
    const std::vector<Entry> entries{{26, 160}, {26, 110}, {26, 100}, {60, 120}, {60, 90}, {60, 180}};
    const Layout layout = Arrange(entries, IconGap, EntryGap, 700.0f);
    assert(layout.scale == 1.0f && layout.lines == 2);
    assert(layout.places.front().line == 0 && layout.places.back().line == 1);
    Inside(entries, layout, 700.0f);
}

// Kaniel 2026-10-25: the co-op hint on the left (how to open the co-op menu, where duels are)
// shows a while once the player is in its team's hunt, then fades out.
bool CoopHintShowsThenFades() {
    const bool ok = CoopHintAlpha(-1.0) == 0.0f && CoopHintAlpha(0.0) == 1.0f &&
                    CoopHintAlpha(CoopHintShownS - 0.01) == 1.0f &&
                    std::fabs(CoopHintAlpha(CoopHintShownS + CoopHintFadeS * 0.5) - 0.5f) < 1e-4f &&
                    CoopHintAlpha(CoopHintShownS + CoopHintFadeS) == 0.0f &&
                    CoopHintAlpha(600.0) == 0.0f;
    if (!ok) {
        std::puts("Key guide: FAIL (the co-op hint's fade)");
    }
    return ok;
}

void AnEntryWiderThanTheRoomStaysAtTheLeft() {
    const std::vector<Entry> entries{{26, 900}};
    const Layout layout = Arrange(entries, IconGap, EntryGap, 500.0f);
    assert(layout.lines == 1 && layout.places[0].x == 0.0f);
}

} // namespace

int main() {
    OneLineCentredWhenItFits();
    SmallerWhenItNearlyFits();
    SecondLineWhenItDoesNot();
    AnEntryWiderThanTheRoomStaysAtTheLeft();
    assert(Arrange({}, IconGap, EntryGap, 100.0f).places.empty());
    if (!CoopHintShowsThenFades()) {
        return 1;
    }
    std::puts("Key guide: PASS (one centred line, smaller, a second line, inside the panel)");
    return 0;
}
