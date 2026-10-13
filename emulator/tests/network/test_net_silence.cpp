// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <gtest/gtest.h>

#include "core/libraries/network/net_silence.h"

using namespace Libraries::Net;

// Kaniel's friend 2026-10-11: the others froze on her screen, then her game left the session,
// and no log said whether packets had stopped reaching her. The transport now says when a peer
// that was talking goes quiet, and when it is heard again.
TEST(NetSilence, APeerThatStopsTalkingIsReportedOnce) {
    PeerSilence silence{3000};
    EXPECT_FALSE(silence.Heard(1, 0).has_value());
    EXPECT_FALSE(silence.Heard(1, 1000).has_value());
    EXPECT_TRUE(silence.Check(3999).empty()) << "quiet for less than 3 s";
    const auto quiet = silence.Check(4001);
    ASSERT_EQ(quiet.size(), 1u);
    EXPECT_EQ(quiet[0].peer, 1u);
    EXPECT_TRUE(quiet[0].silent);
    EXPECT_EQ(quiet[0].packets, 2u);
    EXPECT_TRUE(silence.Check(8000).empty()) << "once";
}

TEST(NetSilence, APeerHeardAgainSaysHowLongItWasQuiet) {
    PeerSilence silence{3000};
    silence.Heard(7, 0);
    silence.Heard(7, 500);
    ASSERT_EQ(silence.Check(5000).size(), 1u);
    const auto back = silence.Heard(7, 9500);
    ASSERT_TRUE(back.has_value());
    EXPECT_FALSE(back->silent);
    EXPECT_EQ(back->gap_ms, 9000);
    EXPECT_FALSE(silence.Heard(7, 9600).has_value());
    EXPECT_TRUE(silence.Check(12000).empty());
}

TEST(NetSilence, OthersStillTalkingAreNotReported) {
    PeerSilence silence{3000};
    silence.Heard(1, 0);
    silence.Heard(1, 100);
    silence.Heard(2, 0);
    silence.Heard(2, 3500);
    const auto quiet = silence.Check(3600);
    ASSERT_EQ(quiet.size(), 1u);
    EXPECT_EQ(quiet[0].peer, 1u);
}

// Ada's log, 2026-10-11: the matching server's port on the host's address sends every 3-4 s,
// and "nothing from" came for it every few seconds. A peer is quiet only against its own pace.
// One packet is no pace yet: a peer is reported once it has talked twice.
TEST(NetSilence, ASlowPeerIsQuietOnlyAgainstItsOwnPace) {
    PeerSilence silence{3000};
    for (s64 at = 0; at <= 35000; at += 3500) {
        silence.Heard(9, at);
        EXPECT_TRUE(silence.Check(at + 3400).empty()) << at;
    }
    EXPECT_TRUE(silence.Check(35000 + 4000).empty()) << "a beat late is no silence";
    EXPECT_EQ(silence.Check(35000 + 15000).size(), 1u) << "four beats missed";
}

TEST(NetSilence, APeerQuietForAMinuteIsForgotten) {
    PeerSilence silence{3000};
    silence.Heard(1, 0);
    silence.Heard(1, 500);
    silence.Check(4000);
    silence.Check(61000);
    EXPECT_FALSE(silence.Heard(1, 70000).has_value()) << "a new start, no 'heard again'";
}
