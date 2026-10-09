// SPDX-License-Identifier: GPL-2.0-or-later
// The log file for the co-op rig's checks (BB_LOG_FILE, gpu/shim/common/logging/log.cpp): every
// message from BB_LOG_LEVEL's level on, one line each in shadPS4's own format, which the rig's
// tools (tools/re/check_party_log.py, tools/rig/run_scenario.py) read.
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include "common/logging/log.h"

static int failures;
#define CHECK(condition)                                                                           \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #condition);     \
            ++failures;                                                                            \
        }                                                                                          \
    } while (false)

static void SetVariable(const char* name, const std::string& value) {
#ifdef _WIN32
    _putenv_s(name, value.c_str());
#else
    setenv(name, value.c_str(), 1);
#endif
}

static bool StartsWith(const std::string& text, const std::string& prefix) {
    return text.compare(0, prefix.size(), prefix) == 0;
}

static bool EndsWith(const std::string& text, const std::string& suffix) {
    return text.size() >= suffix.size() &&
           text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0;
}

int main() {
    const auto folder = std::filesystem::temp_directory_path() / "bbport-log-file-test";
    std::filesystem::remove_all(folder);
    const auto path = folder / "log" / "shad_log.txt"; // the folder is made for it
    SetVariable("BB_LOG_FILE", path.string());
    SetVariable("BB_LOG_LEVEL", "info");
    SetVariable("BB_GPU_LOG", "critical");

    LOG_DEBUG(Debug, "below the file's level");
    LOG_INFO(Debug, "Bloodborne seamless patches enabled");
    LOG_ERROR(Debug, "Bloodborne hook {} failed", 7);
    const auto lambda = [] { LOG_WARNING(Lib_Pad, "from a lambda"); };
    lambda();

    std::ifstream file{path};
    std::vector<std::string> lines;
    for (std::string line; std::getline(file, line);) {
        lines.push_back(line);
    }
    CHECK(lines.size() == 3);
    if (lines.size() == 3) {
        CHECK(StartsWith(lines[0], "[Debug] <Info> ("));
        CHECK(lines[0].find(") test_log_file.cpp:") != std::string::npos);
        CHECK(EndsWith(lines[0], " main: Bloodborne seamless patches enabled"));
        CHECK(StartsWith(lines[1], "[Debug] <Error> ("));
        CHECK(EndsWith(lines[1], " main: Bloodborne hook 7 failed"));
        CHECK(StartsWith(lines[2], "[Lib.Pad] <Warning> ("));
        CHECK(EndsWith(lines[2], " lambda: from a lambda"));
    }
    file.close();
    std::filesystem::remove_all(folder);

    if (failures != 0) {
        std::fprintf(stderr, "log file: %d check(s) failed\n", failures);
        return 1;
    }
    std::puts("log file: ok");
    return 0;
}
