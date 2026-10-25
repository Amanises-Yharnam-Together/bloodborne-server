// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

#include <gtest/gtest.h>

#include "core/bloodborne_party.h"

using namespace Core::Bloodborne;
namespace fs = std::filesystem;

namespace {

void Write(const fs::path& path, const std::string& text) {
    fs::create_directories(path.parent_path());
    std::ofstream{path, std::ios::binary | std::ios::trunc} << text;
}

std::string Read(const fs::path& path) {
    std::ifstream file{path, std::ios::binary};
    return {std::istreambuf_iterator<char>{file}, {}};
}

class SaveBackupsTest : public ::testing::Test {
protected:
    void SetUp() override {
        user = fs::temp_directory_path() /
               ("bb-save-backups-" +
                std::to_string(::testing::UnitTest::GetInstance()->random_seed()) + "-" +
                ::testing::UnitTest::GetInstance()->current_test_info()->name());
        fs::remove_all(user);
        fs::create_directories(user);
    }
    void TearDown() override {
        fs::remove_all(user);
    }
    fs::path user;
};

} // namespace

// SHADPS4_BLOODBORNE_SAVE_BACKUPS=<minutes>,<kept>: on by default, every 10 minutes, the newest 20.
TEST(BloodborneSaveBackups, ParsesTheSetting) {
    const auto standard = ParseSaveBackups("");
    ASSERT_TRUE(standard);
    EXPECT_EQ(standard->every, std::chrono::minutes{10});
    EXPECT_EQ(standard->kept, 20u);
    const auto custom = ParseSaveBackups(" 5 , 50 ");
    ASSERT_TRUE(custom);
    EXPECT_EQ(custom->every, std::chrono::minutes{5});
    EXPECT_EQ(custom->kept, 50u);
    EXPECT_FALSE(ParseSaveBackups("off"));
    EXPECT_FALSE(ParseSaveBackups("0"));
    EXPECT_EQ(ParseSaveBackups("x,y"), ParseSaveBackups("")) << "malformed means the default";
    EXPECT_EQ(ParseSaveBackups("0,5"), ParseSaveBackups("")) << "no zero interval";
    EXPECT_EQ(ParseSaveBackups("5,0"), ParseSaveBackups("")) << "keeps at least one";
}

// The port keeps its saves in user/savedata, the emulator in user/home/<user id>/savedata; the
// party's own files (user/bloodborne_party_*.txt) belong to the same character.
TEST_F(SaveBackupsTest, CopiesSavesAndPartyFiles) {
    Write(user / "savedata" / "1" / "CUSA00207" / "SPRJ0005" / "userdata0000", "port save");
    Write(user / "home" / "1000" / "savedata" / "CUSA03173" / "SPRJ0005" / "userdata0000",
          "emulator save");
    Write(user / "bloodborne_party_world_slot0.txt", "2000 1\n");
    Write(user / "log" / "shad_log.txt", "not a save");
    const auto made = BackUpSaves(user, "20261011-150000");
    ASSERT_TRUE(made);
    const fs::path backup = user / SaveBackupsFolder / "20261011-150000";
    EXPECT_EQ(*made, backup);
    EXPECT_EQ(Read(backup / "savedata" / "1" / "CUSA00207" / "SPRJ0005" / "userdata0000"),
              "port save");
    EXPECT_EQ(
        Read(backup / "home" / "1000" / "savedata" / "CUSA03173" / "SPRJ0005" / "userdata0000"),
        "emulator save");
    EXPECT_EQ(Read(backup / "bloodborne_party_world_slot0.txt"), "2000 1\n");
    EXPECT_FALSE(fs::exists(backup / "log"));
}

TEST_F(SaveBackupsTest, NothingToBackUpWithoutSaves) {
    Write(user / "log" / "shad_log.txt", "x");
    EXPECT_FALSE(BackUpSaves(user, "20261011-150000"));
    EXPECT_FALSE(fs::exists(user / SaveBackupsFolder / "20261011-150000"));
}

// A backup the same as the newest one is not kept: an idle game does not push out older ones.
TEST_F(SaveBackupsTest, SkipsAnUnchangedSave) {
    Write(user / "savedata" / "1" / "CUSA00207" / "SPRJ0005" / "userdata0000", "one");
    ASSERT_TRUE(BackUpSaves(user, "20261011-150000"));
    EXPECT_FALSE(BackUpSaves(user, "20261011-151000"));
    Write(user / "savedata" / "1" / "CUSA00207" / "SPRJ0005" / "userdata0000", "two");
    EXPECT_TRUE(BackUpSaves(user, "20261011-152000"));
    EXPECT_FALSE(fs::exists(user / SaveBackupsFolder / "20261011-151000"));
}

// Only the newest `kept` stay; anything else in the folder is left alone.
TEST_F(SaveBackupsTest, KeepsTheNewest) {
    for (const char* name : {"20261011-140000", "20261011-150000", "20261011-160000"}) {
        fs::create_directories(user / SaveBackupsFolder / name / "savedata");
    }
    Write(user / SaveBackupsFolder / "README.txt", "mine");
    EXPECT_EQ(PruneSaveBackups(user, 2), 1u);
    EXPECT_FALSE(fs::exists(user / SaveBackupsFolder / "20261011-140000"));
    EXPECT_TRUE(fs::exists(user / SaveBackupsFolder / "20261011-150000"));
    EXPECT_TRUE(fs::exists(user / SaveBackupsFolder / "20261011-160000"));
    EXPECT_TRUE(fs::exists(user / SaveBackupsFolder / "README.txt"));
}

// The launcher's backups (a launcher.txt inside) are the launcher's to prune: hours of play do not
// push out the save a session began with.
TEST_F(SaveBackupsTest, LeavesTheLaunchersBackups) {
    for (const char* name : {"20261011-140000", "20261011-150000", "20261011-160000"}) {
        fs::create_directories(user / SaveBackupsFolder / name / "savedata");
    }
    Write(user / SaveBackupsFolder / "20261011-130000" / LauncherBackupNote, "before PLAY");
    EXPECT_EQ(PruneSaveBackups(user, 1), 2u);
    EXPECT_TRUE(fs::exists(user / SaveBackupsFolder / "20261011-130000"));
    EXPECT_FALSE(fs::exists(user / SaveBackupsFolder / "20261011-150000"));
    EXPECT_TRUE(fs::exists(user / SaveBackupsFolder / "20261011-160000"));
}

TEST(BloodborneSaveBackups, NamesSortByTime) {
    using namespace std::chrono;
    const sys_seconds early = sys_days{year{2026} / 10 / 11} + hours{9} + minutes{5} + seconds{7};
    EXPECT_EQ(SaveBackupName(early), "20261011-090507");
    EXPECT_LT(SaveBackupName(early), SaveBackupName(early + hours{14}));
}
