// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <gtest/gtest.h>

#include "core/libraries/network/windows_trust.h"

using Libraries::Http::HostOfUrl;
using Libraries::Http::WindowsTrustEnabled;

// The host name Windows' SSL policy checks the certificate against.
TEST(WindowsTrust, HostOfUrlDropsSchemePortAndPath) {
    EXPECT_EQ(HostOfUrl("https://example.com"), "example.com");
    EXPECT_EQ(HostOfUrl("https://example.com:8443/v1/sessions?x=1#y"), "example.com");
    EXPECT_EQ(HostOfUrl("example.com:443"), "example.com");
}

TEST(WindowsTrust, HostOfUrlDropsUserInfo) {
    EXPECT_EQ(HostOfUrl("https://user:pass@example.com:443/"), "example.com");
}

TEST(WindowsTrust, HostOfUrlKeepsIpv6Literals) {
    EXPECT_EQ(HostOfUrl("https://[::1]:443/path"), "::1");
    EXPECT_EQ(HostOfUrl("https://[2001:db8::2]"), "2001:db8::2");
}

// BB_WINDOWS_TRUST=0 leaves OpenSSL's answer alone; unset or anything else asks Windows.
TEST(WindowsTrust, OnUnlessTurnedOff) {
    EXPECT_TRUE(WindowsTrustEnabled(nullptr));
    EXPECT_TRUE(WindowsTrustEnabled(""));
    EXPECT_TRUE(WindowsTrustEnabled("1"));
    EXPECT_FALSE(WindowsTrustEnabled("0"));
}
