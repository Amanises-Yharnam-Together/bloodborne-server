// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

// Party portraits (Kaniel 2026-10-13): each game takes its own character's head and shoulders,
// outfit and all, from the status page's model, keeps it, and sends it to the party; the party
// list shows the others' portraits.

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "core/bloodborne_party.h"
#include "core/bloodborne_portrait.h"

using namespace Core::Bloodborne;

namespace {

// A status page model: transparent but for opaque blocks of a figure, a head of `head` colour
// (`half` either side of `cx`, from row `top`, 70 rows tall) on shoulders of `body` colour (from
// 70 rows below the top down to the target's foot, 70 either side of `cx`).
std::vector<u8> Model(std::array<u8, 3> head, std::array<u8, 3> body, int cx = 300, int top = 30,
                      int half = 30) {
    std::vector<u8> rgba(size_t{StatusModelWidth} * StatusModelHeight * 4, 0);
    const auto fill = [&](int x0, int x1, int y0, int y1, std::array<u8, 3> color) {
        for (int y = std::max(y0, 0); y < std::min(y1, StatusModelHeight); ++y) {
            for (int x = std::max(x0, 0); x < std::min(x1, StatusModelWidth); ++x) {
                u8* pixel = &rgba[(size_t(y) * StatusModelWidth + x) * 4];
                pixel[0] = color[0];
                pixel[1] = color[1];
                pixel[2] = color[2];
                pixel[3] = 0xFF;
            }
        }
    };
    fill(cx - half, cx + half, top, top + 70, head);
    fill(cx - 70, cx + 70, top + 70, StatusModelHeight - 20, body);
    return rgba;
}

constexpr std::array<u8, 3> Skin{200, 150, 100};
constexpr std::array<u8, 3> Coat{40, 60, 80};
constexpr std::array<u8, 4> Ground{PortraitBackdrop[0], PortraitBackdrop[1], PortraitBackdrop[2],
                                   0xFF};

std::array<u8, 4> At(const PortraitPixels& pixels, int x, int y) {
    const u8* pixel = &pixels[(size_t(y) * PortraitSide + x) * 4];
    return {pixel[0], pixel[1], pixel[2], pixel[3]};
}

PortraitPixels Gradient(u8 seed) {
    PortraitPixels pixels(PortraitBytes);
    for (int y = 0; y < PortraitSide; ++y) {
        for (int x = 0; x < PortraitSide; ++x) {
            u8* pixel = &pixels[(size_t(y) * PortraitSide + x) * 4];
            pixel[0] = u8(x * 4 + seed);
            pixel[1] = u8(y * 4);
            pixel[2] = u8((x + y) * 2 + seed);
            pixel[3] = 0xFF;
        }
    }
    return pixels;
}

} // namespace

TEST(BloodbornePortrait, TheHeadAndShouldersAreCroppedScaledAndPutOnTheBackdrop) {
    const auto portrait = PortraitFromModel(Model(Skin, Coat), StatusModelWidth, StatusModelHeight);
    ASSERT_TRUE(portrait.has_value());
    ASSERT_EQ(portrait->size(), PortraitBytes);
    // The head at the top middle, the shoulders (the outfit) below it, all opaque.
    EXPECT_EQ(At(*portrait, PortraitSide / 2, PortraitSide / 4),
              (std::array<u8, 4>{200, 150, 100, 0xFF}));
    EXPECT_EQ(At(*portrait, PortraitSide / 2, PortraitSide - 8),
              (std::array<u8, 4>{40, 60, 80, 0xFF}));
    // Beside the head and above it, where the page is transparent, the backdrop.
    EXPECT_EQ(At(*portrait, 2, PortraitSide / 4), Ground);
    EXPECT_EQ(At(*portrait, PortraitSide / 2, 0), Ground);
    // The head is in the middle: its edges are as far from the portrait's sides.
    int left = 0;
    while (left < PortraitSide && At(*portrait, left, PortraitSide / 4) == Ground) {
        ++left;
    }
    int right = 0;
    while (right < PortraitSide &&
           At(*portrait, PortraitSide - 1 - right, PortraitSide / 4) == Ground) {
        ++right;
    }
    EXPECT_NEAR(left, right, 1);
    EXPECT_GT(left, 8);
}

TEST(BloodbornePortrait, TheCropFollowsTheHeadWhereverTheModelStands) {
    const auto middle = PortraitFromModel(Model(Skin, Coat), StatusModelWidth, StatusModelHeight);
    const auto moved =
        PortraitFromModel(Model(Skin, Coat, 200, 120), StatusModelWidth, StatusModelHeight);
    ASSERT_TRUE(middle.has_value());
    EXPECT_EQ(moved, middle);
    // At the target's edge the crop stays inside it.
    const auto edge =
        PortraitFromModel(Model(Skin, Coat, 40, 2), StatusModelWidth, StatusModelHeight);
    ASSERT_TRUE(edge.has_value());
    EXPECT_EQ(At(*edge, 10, PortraitSide / 4), (std::array<u8, 4>{200, 150, 100, 0xFF}));
    EXPECT_EQ(At(*edge, PortraitSide / 2, PortraitSide / 4), Ground);
}

TEST(BloodbornePortrait, AnEmptyOrOtherTargetIsNoPortrait) {
    std::vector<u8> empty(size_t{StatusModelWidth} * StatusModelHeight * 4, 0);
    EXPECT_FALSE(PortraitFromModel(empty, StatusModelWidth, StatusModelHeight).has_value());
    // A speck is not a model either (the page's first frames).
    std::vector<u8> speck = empty;
    for (int y = 300; y < 303; ++y) {
        for (int x = 300; x < 303; ++x) {
            speck[(size_t(y) * StatusModelWidth + x) * 4 + 3] = 0xFF;
        }
    }
    EXPECT_FALSE(PortraitFromModel(speck, StatusModelWidth, StatusModelHeight).has_value());
    // The status page's face target (560x256) is not the model.
    EXPECT_FALSE(PortraitFromModel(Model(Skin, Coat), StatusModelWidth, 256).has_value());
    const auto model = Model(Skin, Coat);
    EXPECT_FALSE(
        PortraitFromModel(std::span{model}.first(100), StatusModelWidth, StatusModelHeight));
}

TEST(BloodbornePortrait, APortraitGoesInPartsThatFitTheLinksFrames) {
    const PortraitPixels pixels = Gradient(7);
    const auto parts = SplitPortrait("rig_guest", pixels);
    ASSERT_EQ(parts.size(), PortraitPartCount);
    for (size_t index = 0; index < parts.size(); ++index) {
        EXPECT_EQ(parts[index].part, index);
        EXPECT_EQ(parts[index].parts, PortraitPartCount);
        EXPECT_EQ(parts[index].id, PortraitId(pixels));
        const auto frame = EncodeCoopFrame(PortraitPartType, EncodePortraitPart(parts[index]));
        EXPECT_LE(frame.size(), CoopFrameMax);
        const auto decoded = DecodeCoopFrame(frame);
        ASSERT_TRUE(decoded.has_value());
        EXPECT_EQ(decoded->first, PortraitPartType);
        EXPECT_EQ(DecodePortraitPart(decoded->second), parts[index]);
    }
}

TEST(BloodbornePortrait, ABrokenPartIsDropped) {
    const auto part = SplitPortrait("rig_guest", Gradient(1))[3];
    auto bytes = EncodePortraitPart(part);
    EXPECT_FALSE(DecodePortraitPart(std::span{bytes}.first(bytes.size() - 1)));
    EXPECT_FALSE(DecodePortraitPart({}));
    auto wrong = part;
    wrong.part = static_cast<u8>(PortraitPartCount);
    EXPECT_FALSE(DecodePortraitPart(EncodePortraitPart(wrong)));
    wrong = part;
    wrong.parts = 3;
    EXPECT_FALSE(DecodePortraitPart(EncodePortraitPart(wrong)));
    bytes.push_back(0);
    EXPECT_FALSE(DecodePortraitPart(bytes));
}

TEST(BloodbornePortrait, ThePartsMakeThePictureAgainInAnyOrder) {
    const PortraitPixels pixels = Gradient(3);
    auto parts = SplitPortrait("rig_guest", pixels);
    std::reverse(parts.begin(), parts.end());
    PortraitAssembler assembler;
    std::optional<PortraitPixels> made;
    for (size_t index = 0; index < parts.size(); ++index) {
        made = assembler.Take(parts[index]);
        EXPECT_EQ(made.has_value(), index + 1 == parts.size());
    }
    ASSERT_TRUE(made.has_value());
    ASSERT_EQ(made->size(), PortraitBytes);
    // RGB565 on the way: each channel within its lost bits.
    for (size_t at = 0; at < made->size(); at += 4) {
        EXPECT_NEAR((*made)[at], pixels[at], 7);
        EXPECT_NEAR((*made)[at + 1], pixels[at + 1], 3);
        EXPECT_NEAR((*made)[at + 2], pixels[at + 2], 7);
        EXPECT_EQ((*made)[at + 3], 0xFF);
    }
    // Once made, the same parts again make nothing new.
    EXPECT_FALSE(assembler.Take(parts[0]).has_value());
}

TEST(BloodbornePortrait, ALostPartLeavesThePictureUnmadeAndPicturesAreKeptApart) {
    const auto first = SplitPortrait("rig_guest", Gradient(1));
    const auto second = SplitPortrait("rig_guest", Gradient(2));
    const auto third = SplitPortrait("rig_guest", Gradient(3));
    PortraitAssembler assembler;
    for (size_t index = 1; index < first.size(); ++index) {
        EXPECT_FALSE(assembler.Take(first[index]));
    }
    // The member's next picture comes while a part of the one before is still on its way.
    for (size_t index = 0; index + 1 < second.size(); ++index) {
        EXPECT_FALSE(assembler.Take(second[index]));
    }
    const auto late = assembler.Take(first[0]);
    ASSERT_TRUE(late.has_value());
    EXPECT_FALSE(assembler.Take(first[0]).has_value()) << "made once";
    EXPECT_TRUE(assembler.Take(second.back()).has_value());
    // Two more pictures: the oldest unfinished one is dropped.
    PortraitAssembler kept;
    for (size_t index = 1; index < first.size(); ++index) {
        kept.Take(first[index]);
    }
    kept.Take(second[0]);
    kept.Take(third[0]);
    EXPECT_FALSE(kept.Take(first[0]).has_value()) << "its other parts were dropped";
    // Another member's parts are kept apart.
    const auto other = SplitPortrait("rig_host", Gradient(1));
    std::optional<PortraitPixels> made;
    for (const auto& part : other) {
        made = assembler.Take(part);
    }
    EXPECT_TRUE(made.has_value());
}

TEST(BloodbornePortrait, TheListSlotsShowTheMembersThatHaveAPortrait) {
    PartyPortraits portraits;
    EXPECT_FALSE(portraits.Show(0, "rig_guest"));
    const u32 empty = portraits.Version(0);
    EXPECT_FALSE(portraits.Pixels(0).has_value());
    portraits.Take("rig_guest", Gradient(1));
    EXPECT_TRUE(portraits.Has("rig_guest"));
    EXPECT_TRUE(portraits.Show(0, "rig_guest"));
    const u32 shown = portraits.Version(0);
    EXPECT_NE(shown, empty);
    EXPECT_EQ(portraits.Pixels(0), Gradient(1));
    // The same again: the slot's texture is left as it is.
    EXPECT_TRUE(portraits.Show(0, "rig_guest"));
    EXPECT_EQ(portraits.Version(0), shown);
    // A new picture of the member, or another member in the slot: a new version.
    portraits.Take("rig_guest", Gradient(2));
    EXPECT_TRUE(portraits.Show(0, "rig_guest"));
    EXPECT_NE(portraits.Version(0), shown);
    EXPECT_FALSE(portraits.Show(0, "rig_guest2"));
    EXPECT_FALSE(portraits.Pixels(0).has_value());
    EXPECT_FALSE(portraits.Show(1, ""));
    EXPECT_FALSE(portraits.Show(PortraitSlotCount, "rig_guest"));
    EXPECT_FALSE(portraits.Pixels(-1).has_value());
    // A member gone: its picture goes.
    portraits.KeepOnly(std::vector<std::string>{"rig_guest2"});
    EXPECT_FALSE(portraits.Has("rig_guest"));
}

TEST(BloodbornePortrait, TheLastCaptureOfAShowingIsThePortrait) {
    FaceCapture capture;
    EXPECT_FALSE(capture.Settled(0).has_value());
    capture.Take(Gradient(1), 1000);
    capture.Take(Gradient(2), 1500);
    EXPECT_FALSE(capture.Settled(1500 + FaceCapture::SettleMs - 1).has_value());
    EXPECT_EQ(capture.Settled(1500 + FaceCapture::SettleMs), Gradient(2));
    EXPECT_FALSE(capture.Settled(100'000).has_value());
}

TEST(BloodbornePortrait, ThePortraitIsSentOnChangeToNewPeersAndNowAndThen) {
    PortraitSender sender;
    const std::vector<u64> one{1}, two{1, 2};
    EXPECT_FALSE(sender.Due(0, one, 0)) << "no portrait yet";
    EXPECT_FALSE(sender.Due(5, {}, 0)) << "nobody to send to";
    EXPECT_TRUE(sender.Due(5, one, 10));
    EXPECT_FALSE(sender.Due(5, one, 20));
    EXPECT_TRUE(sender.Due(5, two, 30)) << "a new peer";
    EXPECT_FALSE(sender.Due(5, two, 40));
    EXPECT_TRUE(sender.Due(6, two, 50)) << "a new picture";
    EXPECT_FALSE(sender.Due(6, two, 50 + PortraitSender::ResendMs - 1));
    EXPECT_TRUE(sender.Due(6, two, 50 + PortraitSender::ResendMs)) << "now and then (a lost part)";
    EXPECT_FALSE(sender.Due(6, one, 60)) << "a peer gone";
    EXPECT_TRUE(sender.Due(6, two, 70)) << "and back";
}

TEST(BloodbornePortrait, ThePortraitIsKeptOnDiskByTheFace) {
    const auto dir = std::filesystem::temp_directory_path() / "bb-portrait-test";
    std::filesystem::remove_all(dir);
    const PortraitKey key{.face = 0x0123456789ABCDEFull,
                          .outfit = {210000, 210100, 210200, 210300}};
    const auto path = PortraitCachePath(dir, key);
    EXPECT_EQ(path.filename().string().rfind("0123456789abcdef-", 0), 0u);
    EXPECT_NE(path, PortraitCachePath(dir, PortraitKey{.face = 1, .outfit = key.outfit}));
    EXPECT_FALSE(LoadPortrait(path).has_value());
    EXPECT_TRUE(SavePortrait(path, Gradient(4)));
    EXPECT_EQ(LoadPortrait(path), Gradient(4));
    // Anything else in its place is no portrait.
    EXPECT_FALSE(SavePortrait(path, PortraitPixels(10)));
    std::filesystem::resize_file(path, 100);
    EXPECT_FALSE(LoadPortrait(path).has_value());
    std::filesystem::remove_all(dir);
}

TEST(BloodbornePortrait, AnOldOrOutdatedPictureIsNeverLoaded) {
    // Kaniel 2026-10-13: his party list showed a bald head. The cache held the first build's
    // picture (the status page's face target: no hood, no collar) under the face alone, and the
    // new build loaded it. The name holds the capture's format and the outfit worn: an older
    // capture, or one of another outfit, is never the portrait.
    const auto dir = std::filesystem::temp_directory_path() / "bb-portrait-test";
    const PortraitKey key{.face = 0x0123456789ABCDEFull,
                          .outfit = {210000, 210100, 210200, 210300}};
    const auto name = PortraitCachePath(dir, key).filename().string();
    EXPECT_NE(name, "0123456789abcdef.rgba") << "#77's first build's name";
    EXPECT_NE(name.find(std::string{PortraitCaptureFormat}), std::string::npos);
    EXPECT_EQ(PortraitCaptureFormat, "m2") << "the model capture (head, hood and collar)";
    for (size_t piece = 0; piece < key.outfit.size(); ++piece) {
        PortraitKey other = key;
        other.outfit[piece] += 100;
        EXPECT_NE(PortraitCachePath(dir, other), PortraitCachePath(dir, key)) << piece;
    }
    EXPECT_EQ(PortraitCachePath(dir, key), PortraitCachePath(dir, PortraitKey{key}));
}

TEST(BloodbornePortrait, AMemberWithoutAPictureAnyMoreSaysSoAndTheListForgetsIt) {
    // A changed outfit has no picture until the status page shows it: the others stop showing the
    // old one (the member's icon shows again).
    const auto none = DecodePortraitPart(EncodePortraitNone("rig_guest"));
    ASSERT_TRUE(none.has_value());
    EXPECT_TRUE(none->None());
    EXPECT_EQ(none->member, "rig_guest");
    EXPECT_LE(EncodeCoopFrame(PortraitPartType, EncodePortraitNone(std::string(300, 'x'))).size(),
              CoopFrameMax);
    EXPECT_FALSE(SplitPortrait("rig_guest", Gradient(1))[0].None());
    PortraitAssembler assembler;
    EXPECT_FALSE(assembler.Take(*none).has_value());
    PartyPortraits portraits;
    portraits.Take("rig_guest", Gradient(1));
    EXPECT_TRUE(portraits.Show(0, "rig_guest"));
    const u32 shown = portraits.Version(0);
    portraits.Forget("rig_guest");
    EXPECT_FALSE(portraits.Has("rig_guest"));
    EXPECT_FALSE(portraits.Show(0, "rig_guest"));
    EXPECT_NE(portraits.Version(0), shown);
    EXPECT_FALSE(portraits.Pixels(0).has_value());
}

TEST(BloodbornePortrait, NoPictureAnyMoreIsSentAsAPictureIs) {
    PortraitSender sender;
    const std::vector<u64> one{1}, two{1, 2};
    EXPECT_FALSE(sender.Due(0, one, 0)) << "nothing sent before: nothing to take back";
    EXPECT_TRUE(sender.Due(5, one, 10));
    EXPECT_TRUE(sender.Due(0, one, 20)) << "the picture gone";
    EXPECT_FALSE(sender.Due(0, one, 30));
    EXPECT_TRUE(sender.Due(0, two, 40)) << "a new peer";
    EXPECT_TRUE(sender.Due(0, two, 40 + PortraitSender::ResendMs)) << "now and then (lost)";
    EXPECT_TRUE(sender.Due(5, two, 50 + PortraitSender::ResendMs)) << "a picture again";
}

TEST(BloodbornePortrait, TheListShowsThePortraitInACircle) {
    // Kaniel 2026-10-13: a round portrait in a Bloodborne frame. Outside the circle the picture is
    // clear (premultiplied: no colour either), inside it is as it was, its edge smoothed.
    const auto pixels = Gradient(9);
    const auto round = CircularPortrait(pixels);
    ASSERT_EQ(round.size(), PortraitBytes);
    for (const auto& [x, y] : {std::pair{0, 0}, {63, 0}, {0, 63}, {63, 63}, {5, 5}, {58, 6}}) {
        EXPECT_EQ(At(round, x, y), (std::array<u8, 4>{0, 0, 0, 0})) << x << "," << y;
    }
    for (const auto& [x, y] : {std::pair{32, 32}, {31, 3}, {3, 31}, {50, 50}, {20, 55}}) {
        EXPECT_EQ(At(round, x, y), At(pixels, x, y)) << x << "," << y;
    }
    int edge = 0;
    for (int y = 0; y < PortraitSide; ++y) {
        for (int x = 0; x < PortraitSide; ++x) {
            const auto pixel = At(round, x, y);
            EXPECT_EQ(pixel[3], At(round, PortraitSide - 1 - x, y)[3]);
            EXPECT_EQ(pixel[3], At(round, x, PortraitSide - 1 - y)[3]);
            for (int c = 0; c < 3; ++c) {
                EXPECT_LE(pixel[c], pixel[3]) << "premultiplied";
            }
            edge += pixel[3] > 0 && pixel[3] < 0xFF ? 1 : 0;
        }
    }
    EXPECT_GT(edge, 100) << "a smooth edge";
    EXPECT_TRUE(CircularPortrait(PortraitPixels(10)).empty()) << "not a portrait";
}

TEST(BloodbornePortrait, AMemberWithAPortraitShowsItWithTheIconAsItsBadge) {
    PartyListEntry entry;
    entry.visible = true;
    entry.icon = PartyListIconDowned0 + 3;
    EXPECT_EQ(PartyListArtFor(entry, true), (PartyListArt{.icon = PartyListIconNone,
                                                          .badge = PartyListIconDowned0 + 3,
                                                          .face = PortraitFrameShown}));
    EXPECT_EQ(PartyListArtFor(entry, false), (PartyListArt{.icon = PartyListIconDowned0 + 3,
                                                           .badge = PartyListIconNone,
                                                           .face = PortraitFrameNone}));
    entry.visible = false;
    EXPECT_EQ(PartyListArtFor(entry, true), (PartyListArt{.icon = PartyListIconNone,
                                                          .badge = PartyListIconNone,
                                                          .face = PortraitFrameNone}));
}

TEST(BloodbornePortrait, TheListEntryNamesTheMembersBoardName) {
    PartyPanel panel;
    panel.members.push_back(PanelMember{.member = "6b616e69656c", .name = "Kaniel"});
    const auto entries = PartyListEntries(panel);
    EXPECT_EQ(entries[0].member, "6b616e69656c");
    EXPECT_EQ(entries[0].name, "Kaniel");
}

TEST(BloodbornePortrait, ThePortraitFrameIsACoopFrameOfTheNewProtocol) {
    EXPECT_TRUE(IsCoopFrameType(PortraitPartType));
    EXPECT_FALSE(IsWorldLogFrameType(PortraitPartType));
    EXPECT_EQ(CoopProtocolVersion, 9) << "the members' portraits: 9";
}
