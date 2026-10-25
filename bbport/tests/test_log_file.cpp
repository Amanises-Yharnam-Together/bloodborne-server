// SPDX-License-Identifier: GPL-2.0-or-later
// The log file for the co-op rig's checks (BB_LOG_FILE, gpu/shim/common/logging/log.cpp): every
// message from BB_LOG_LEVEL's level on, one line each in shadPS4's own format, which the rig's
// tools (tools/re/check_party_log.py, tools/rig/run_scenario.py) read. A thread of the log's own
// writes the lines (the game thread never waits for the disk); Flush writes what still waits, and
// BB_LOG_APPEND=1 (the in-game restart) keeps what the run before wrote.
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <thread>
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

static std::vector<std::string> ReadLines(const std::filesystem::path& path) {
    std::ifstream file{path};
    std::vector<std::string> lines;
    for (std::string line; std::getline(file, line);) {
        lines.push_back(line);
    }
    return lines;
}

// The in-game restart's second run: BB_LOG_APPEND=1 goes on below the first run's lines.
static int RestartedRun(const char* path) {
    SetVariable("BB_LOG_FILE", path);
    SetVariable("BB_LOG_LEVEL", "info");
    SetVariable("BB_GPU_LOG", "critical");
    SetVariable("BB_LOG_APPEND", "1");
    LOG_INFO(Debug, "after the restart");
    return 0; // exit's handlers write the line out
}

static void ChecksRestart(const char* self, const std::filesystem::path& folder) {
    const auto path = folder / "restart" / "shad_log.txt";
    std::filesystem::create_directories(path.parent_path());
    std::ofstream{path} << "[Debug] <Info> (main) run.cpp:1 main: before the restart\n";
    const std::string command = "\"" + std::string{self} + "\" restarted \"" + path.string() + "\"";
#ifdef _WIN32
    CHECK(std::system(("\"" + command + "\"").c_str()) == 0); // cmd /c strips one pair of quotes
#else
    CHECK(std::system(command.c_str()) == 0);
#endif
    const auto lines = ReadLines(path);
    CHECK(lines.size() == 3);
    if (lines.size() == 3) {
        CHECK(EndsWith(lines[0], "main: before the restart"));
        CHECK(lines[1].find("the game restarted") != std::string::npos);
        CHECK(EndsWith(lines[2], " RestartedRun: after the restart"));
    }
}

int main(int argc, char** argv) {
    if (argc == 3 && std::string{argv[1]} == "restarted") {
        return RestartedRun(argv[2]);
    }
    const auto folder = std::filesystem::temp_directory_path() / "bbport-log-file-test";
    std::error_code ignored; // the log keeps its file open until exit: Windows can't remove it
    std::filesystem::remove_all(folder, ignored);
    const auto path = folder / "log" / "shad_log.txt"; // the folder is made for it
    SetVariable("BB_LOG_FILE", path.string());
    SetVariable("BB_LOG_LEVEL", "info");
    SetVariable("BB_GPU_LOG", "critical");

    LOG_DEBUG(Debug, "below the file's level");
    LOG_INFO(Debug, "Bloodborne seamless patches enabled");
    LOG_ERROR(Debug, "Bloodborne hook {} failed", 7);
    const auto lambda = [] { LOG_WARNING(Lib_Pad, "from a lambda"); };
    lambda();
    Common::Log::Flush();

    std::vector<std::string> lines = ReadLines(path);
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

    // Four threads at once: every line arrives whole, each thread's in its own order.
    std::vector<std::thread> threads;
    for (int t = 0; t < 4; ++t) {
        threads.emplace_back([t] {
            for (int i = 0; i < 500; ++i) LOG_INFO(Debug, "thread {} line {}", t, i);
        });
    }
    for (auto& thread : threads) thread.join();
    Common::Log::Flush();
    lines = ReadLines(path);
    CHECK(lines.size() == 3 + 4 * 500);
    int next[4] = {};
    for (std::size_t i = 3; i < lines.size(); ++i) {
        const auto at = lines[i].find(": thread ");
        CHECK(at != std::string::npos);
        if (at == std::string::npos) continue;
        int t = -1, n = -1;
        CHECK(std::sscanf(lines[i].c_str() + at, ": thread %d line %d", &t, &n) == 2);
        if (t < 0 || t > 3) continue;
        CHECK(n == next[t]);
        next[t] = n + 1;
    }

    // Lines written without a Flush still reach the file soon: the writer does not wait for one
    // (the rig reads the log while the game runs).
    LOG_INFO(Debug, "no flush");
    bool arrived = false;
    for (int tries = 0; tries < 200 && !arrived; ++tries) {
        std::this_thread::sleep_for(std::chrono::milliseconds{10});
        const auto now = ReadLines(path);
        arrived = !now.empty() && EndsWith(now.back(), "main: no flush");
    }
    CHECK(arrived);

    ChecksRestart(argv[0], folder);
    std::filesystem::remove_all(folder, ignored);

    if (failures != 0) {
        std::fprintf(stderr, "log file: %d check(s) failed\n", failures);
        return 1;
    }
    std::puts("log file: ok");
    return 0;
}
