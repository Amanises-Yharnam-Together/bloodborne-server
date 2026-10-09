// bbport: see log.h. Messages go to stderr from BB_GPU_LOG's level on and, with BB_LOG_FILE set,
// to that file from BB_LOG_LEVEL's level on (default info), in shadPS4's own line format: the
// co-op rig's tools read it as an emulator log (tools/re/check_party_log.py).
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <system_error>
#include "common/logging/log.h"

namespace Common::Log {
static Level ParseLevel(const char* v, Level fallback) {
    if (!v) return fallback;
    static const char* names[] = {"trace", "debug", "info", "warning", "error", "critical"};
    for (int i = 0; i < 6; ++i)
        if (!std::strcmp(v, names[i])) return Level(i);
    return fallback;
}
static Level threshold() {
    static const Level level = ParseLevel(std::getenv("BB_GPU_LOG"), Level::warn);
    return level;
}
struct FileSink {
    std::FILE* file{};
    Level threshold{Level::off};
};
static const FileSink& file_sink() {
    static const FileSink sink = [] {
        FileSink opened;
        const char* path = std::getenv("BB_LOG_FILE");
        if (!path || !*path) return opened;
        std::error_code error;
        std::filesystem::create_directories(std::filesystem::path{path}.parent_path(), error);
        opened.file = std::fopen(path, "w");
        if (opened.file) opened.threshold = ParseLevel(std::getenv("BB_LOG_LEVEL"), Level::info);
        return opened;
    }();
    return sink;
}
bool ShouldLog(Level level) {
    return level >= threshold() || (file_sink().file && level >= file_sink().threshold);
}
void Write(std::string_view log_class, Level level, const char* file, int line, const char* function,
           std::string&& message) {
    static std::mutex mutex;
    static const char* names[] = {"Trace", "Debug", "Info", "Warning", "Error", "Critical"};
    const char* base = std::strrchr(file, '/');
#ifdef _WIN32
    if (const char* backslash = std::strrchr(base ? base : file, '\\')) base = backslash;
#endif
    base = base ? base + 1 : file;
    const auto& sink = file_sink();
    const bool to_file = sink.file && level >= sink.threshold;
    const std::string thread = to_file ? Common::GetCurrentThreadName() : std::string{};
    std::scoped_lock lock{mutex};
    if (level >= threshold()) {
        std::fprintf(stderr, "GPU [%.*s] <%s> %s:%d %s: %s\n", int(log_class.size()), log_class.data(),
                     names[int(level)], base, line, function, message.c_str());
    }
    if (to_file) {
        std::fprintf(sink.file, "[%.*s] <%s> (%s) %s:%d %s: %s\n", int(log_class.size()),
                     log_class.data(), names[int(level)], thread.c_str(), base, line,
                     std::strcmp(function, "operator()") ? function : "lambda", message.c_str());
        std::fflush(sink.file); // the rig reads it while the game runs
    }
}
} // namespace Common::Log
