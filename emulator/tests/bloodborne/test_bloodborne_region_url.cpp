// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later
// The game picks its ss.info address by region (0 Japan, 1 US, 2 EU, 3 Asia, 4 EU). The "Unlock
// Game Region" patch forces region 5, whose address is empty: the game asked for "" and the host's
// game failed to acquire the server information (a player's log, 2026-10-22).
#include <gtest/gtest.h>

#include "core/bloodborne_region_url.h"

using namespace Core::Bloodborne;

TEST(RegionUrl, AnEmptyAddressIsTheEuServerInformation) {
    EXPECT_EQ(GameServerUrl(""), "https://ss4.scej-network.jp:20443/bb-eu/ss.info");
}

TEST(RegionUrl, EveryOtherAddressStaysAsTheGameGaveIt) {
    for (const std::string_view url : {"https://ss4.scej-network.jp:20443/bb-jp/ss.info",
                                       "https://ss4.scej-network.jp:20443/bb-us/ss.info",
                                       "http://26.1.2.3:20443/basic_utils/login", "x"}) {
        EXPECT_EQ(GameServerUrl(url), url);
    }
}
