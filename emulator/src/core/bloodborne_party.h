// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace Core::Bloodborne {

// Validates a "<name>:<secret>" party setting and returns the header value to send.
std::optional<std::string> ParsePartySetting(std::string_view setting);

} // namespace Core::Bloodborne
