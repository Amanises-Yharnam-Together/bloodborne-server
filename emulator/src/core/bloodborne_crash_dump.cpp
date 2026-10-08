// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/bloodborne_crash_dump.h"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <exception>
#include <string>

#include <fmt/format.h>

#include "common/path_util.h"
#include "core/bloodborne_party.h"

#ifdef _WIN32
#include <windows.h>
// After windows.h, which it needs.
#include <dbghelp.h>
#endif

namespace Core::Bloodborne {

#ifdef _WIN32
namespace {

std::atomic<u64> crash_image_base{0};
std::atomic<u64> crash_image_size{0};
std::atomic<bool> report_written{false};

std::string ThreadName() {
    PWSTR description = nullptr;
    std::string name;
    if (SUCCEEDED(GetThreadDescription(GetCurrentThread(), &description)) && description) {
        for (const wchar_t* c = description; *c != 0 && name.size() < 64; ++c) {
            name += *c < 0x80 ? static_cast<char>(*c) : '?';
        }
        LocalFree(description);
    }
    return name;
}

void WriteFileText(const std::wstring& path, const std::string& text) {
    const HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                                    FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return;
    }
    DWORD written = 0;
    WriteFile(file, text.data(), static_cast<DWORD>(text.size()), &written, nullptr);
    CloseHandle(file);
}

LONG WINAPI OnUnhandledException(EXCEPTION_POINTERS* exception) {
    WriteCrashReport(exception);
    return EXCEPTION_CONTINUE_SEARCH;
}

void OnAbort(int) {
    WriteCrashReport(nullptr);
}

void OnTerminate() {
    WriteCrashReport(nullptr);
    std::abort();
}

} // namespace

void WriteCrashReport(void* exception_pointers) {
    if (report_written.exchange(true)) {
        return;
    }
    auto* exception = static_cast<EXCEPTION_POINTERS*>(exception_pointers);
    const bool has_record = exception != nullptr && exception->ExceptionRecord != nullptr;
    const u32 code = has_record ? exception->ExceptionRecord->ExceptionCode : 0;
    const u64 address =
        has_record ? reinterpret_cast<u64>(exception->ExceptionRecord->ExceptionAddress) : 0;
    std::string text = FormatCrashLine(code, address, crash_image_base.load(),
                                       crash_image_size.load(), ThreadName());
    if (has_record && code == EXCEPTION_ACCESS_VIOLATION &&
        exception->ExceptionRecord->NumberParameters >= 2) {
        text += fmt::format(" ({} {:#x})",
                            exception->ExceptionRecord->ExceptionInformation[0] ? "write to"
                                                                                : "read from",
                            exception->ExceptionRecord->ExceptionInformation[1]);
    }
    if (exception == nullptr) {
        text += " (abort or std::terminate)";
    }
    text += '\n';

    const auto stamp = std::chrono::duration_cast<std::chrono::seconds>(
                           std::chrono::system_clock::now().time_since_epoch())
                           .count();
    const auto folder = Common::FS::GetUserPath(Common::FS::PathType::LogDir);
    WriteFileText((folder / fmt::format("crash-{}.txt", stamp)).wstring(), text);

    const HANDLE dump =
        CreateFileW((folder / fmt::format("crash-{}.dmp", stamp)).c_str(), GENERIC_WRITE, 0,
                    nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (dump == INVALID_HANDLE_VALUE) {
        return;
    }
    MINIDUMP_EXCEPTION_INFORMATION info{};
    info.ThreadId = GetCurrentThreadId();
    info.ExceptionPointers = exception;
    info.ClientPointers = FALSE;
    const auto type = static_cast<MINIDUMP_TYPE>(MiniDumpWithIndirectlyReferencedMemory |
                                                 MiniDumpScanMemory | MiniDumpWithThreadInfo);
    MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), dump, type,
                      exception != nullptr ? &info : nullptr, nullptr, nullptr);
    CloseHandle(dump);
}

void InstallCrashCatcher(u64 image_base, u64 image_size) {
    crash_image_base.store(image_base);
    crash_image_size.store(image_size);
    SetUnhandledExceptionFilter(OnUnhandledException);
    std::set_terminate(OnTerminate);
    std::signal(SIGABRT, OnAbort);
}
#else
void InstallCrashCatcher(u64, u64) {}
void WriteCrashReport(void*) {}
#endif

} // namespace Core::Bloodborne
