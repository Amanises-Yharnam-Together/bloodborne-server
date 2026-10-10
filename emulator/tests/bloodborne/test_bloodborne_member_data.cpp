// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

// A member's character data (stats, face, gear): asked for again when it was lost with a trip,
// and sent again when it changes in a session.

#include <optional>

#include <gtest/gtest.h>

#include "core/bloodborne_party.h"

using namespace Core::Bloodborne;

namespace {
using Action = MemberDataWait::Action;
} // namespace

TEST(BloodborneMemberData, AllFourDataPartsMakeAMemberComplete) {
    EXPECT_TRUE(HasAllMemberData(0x1F)) << "what the game's own pending check wants";
    EXPECT_TRUE(HasAllMemberData(0x1E)) << "the slot's own bit is not a part";
    EXPECT_FALSE(HasAllMemberData(0x01)) << "a slot with nothing in it";
    EXPECT_FALSE(HasAllMemberData(0x1B)) << "the gear (channel 0x0C) is missing";
    EXPECT_FALSE(HasAllMemberData(0x0F)) << "channel 0x0B is missing";
}

TEST(BloodborneMemberData, AGuestStillJoiningAtTheHostsTripIsAskedForItsDataBackHere) {
    // Rig hp-together with the host's trip 10 s in (backlog 2026-10-10): the host's join task for
    // the guest was at step 3 with flags 0x4 when it left; back on the map nothing ever sent the
    // guest's data again and the guest stayed unseen until it rejoined.
    MemberDataWait wait;
    EXPECT_EQ(wait.Tick(0, false, 0x01), Action::None) << "the guest is not on this map yet";
    EXPECT_EQ(wait.Tick(100, true, 0x01), Action::None) << "it has only just come";
    EXPECT_EQ(wait.Tick(1100, true, 0x01), Action::Request) << "here a moment: ask for its data";
    EXPECT_EQ(wait.Requests(), 1);
    EXPECT_EQ(wait.Tick(1200, true, 0x05), Action::None) << "part of it came";
    EXPECT_EQ(wait.Tick(1300, true, 0x1F), Action::Create) << "all of it: make its character";
}

TEST(BloodborneMemberData, DataSeenBeforeTheRequestIsNotTrusted) {
    // The old fix made the character from whatever the slot held after the trip: a body-less
    // "Beckoner" name plate. Only an answer to the request counts.
    MemberDataWait wait;
    EXPECT_EQ(wait.Tick(0, true, 0x1F), Action::None);
    EXPECT_EQ(wait.Tick(1000, true, 0x1F), Action::Request)
        << "a full slot from before the request is asked for again (and cleared)";
    EXPECT_EQ(wait.Tick(1001, true, 0x1F), Action::Create) << "the answer";
}

TEST(BloodborneMemberData, AnUnansweredRequestIsRepeated) {
    MemberDataWait wait;
    wait.Tick(0, true, 0);
    EXPECT_EQ(wait.Tick(1000, true, 0), Action::Request);
    EXPECT_EQ(wait.Tick(5999, true, 0x03), Action::None) << "still waiting for the answer";
    EXPECT_EQ(wait.Tick(6000, true, 0x03), Action::Request) << "lost or not sent: ask again";
    EXPECT_EQ(wait.Requests(), 2);
}

TEST(BloodborneMemberData, AMemberThatLeavesTheMapIsAskedAgainWhenItComesBack) {
    MemberDataWait wait;
    wait.Tick(0, true, 0);
    EXPECT_EQ(wait.Tick(1000, true, 0), Action::Request);
    EXPECT_EQ(wait.Tick(2000, false, 0x1F), Action::None)
        << "not made while the member is somewhere else";
    EXPECT_EQ(wait.Tick(2500, true, 0x1F), Action::None) << "settling again";
    EXPECT_EQ(wait.Tick(3500, true, 0x1F), Action::Request)
        << "its answer may be for the map it left: ask again";
    EXPECT_EQ(wait.Tick(3600, true, 0x1F), Action::Create);
}

TEST(BloodborneMemberData, ALevelUpIsSentToTheOthersOnceItHolds) {
    // Kaniel 2026-10-10: "when somebody levels up the char info for everybody should also be
    // updated". The game sends stats only after a load.
    OwnDataWatch watch;
    EXPECT_FALSE(watch.Observe(0xA, 0)) << "the game sent this one itself when it joined";
    EXPECT_FALSE(watch.Observe(0xA, 10000));
    EXPECT_FALSE(watch.Observe(0xB, 11000)) << "levelling: more points may follow";
    EXPECT_FALSE(watch.Observe(0xC, 12000));
    EXPECT_FALSE(watch.Observe(0xC, 13999));
    EXPECT_TRUE(watch.Observe(0xC, 14000)) << "held for 2 s: send it";
    EXPECT_FALSE(watch.Observe(0xC, 20000)) << "sent once";
}

TEST(BloodborneMemberData, AChangeUndoneBeforeItHoldsIsNotSent) {
    OwnDataWatch watch;
    watch.Observe(0xA, 0);
    EXPECT_FALSE(watch.Observe(0xB, 1000));
    EXPECT_FALSE(watch.Observe(0xA, 2000)) << "back to what the others have";
    EXPECT_FALSE(watch.Observe(0xA, 9000));
}

TEST(BloodborneMemberData, OnlyChangesWithinOneSessionAreSent) {
    OwnDataWatch watch;
    watch.Observe(0xA, 0);
    EXPECT_FALSE(watch.Observe(std::nullopt, 1000)) << "the session ended (or a load)";
    EXPECT_FALSE(watch.Observe(0xB, 2000)) << "a new session: the game sent this one itself";
    EXPECT_FALSE(watch.Observe(0xB, 9000));
    EXPECT_FALSE(watch.Observe(0xC, 10000));
    EXPECT_FALSE(watch.Observe(std::nullopt, 11000));
    EXPECT_FALSE(watch.Observe(0xC, 20000)) << "a pending change does not outlive a load";
}
