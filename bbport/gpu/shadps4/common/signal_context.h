// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "common/types.h"

namespace Common {

void* GetXmmPointer(void* ctx, u8 index);

void* GetRip(void* ctx);

void IncrementRip(void* ctx, u64 length);

// bbport: the registers a fault handler steps over a load with (the SRT walker's).
struct ResumeRegs {
    u64* rip;
    u64* rdi;
    u64* r10;
};
ResumeRegs GetResumeRegs(void* ctx);

bool IsWriteError(void* ctx);
bool IsExecuteError(void* ctx);

} // namespace Common
