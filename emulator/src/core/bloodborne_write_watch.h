// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "common/types.h"

namespace Core::Bloodborne {

// Called on the writing thread right after a write to the watched byte: rip is the instruction
// after the write, rsp the stack it ran on.
using WriteObserver = void (*)(u64 rip, u64 rsp);

// A hardware write watch (debug register 0) on one byte, armed on every thread of the process
// that exists now; call again to arm threads started since. Diagnostics only. False off Windows
// or when no thread could be armed.
bool WatchByteWrites(u64 address, WriteObserver observer);

} // namespace Core::Bloodborne
