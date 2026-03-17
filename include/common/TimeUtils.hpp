#pragma once
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Meridian DNS Contributors
// This file is part of Meridian DNS. See LICENSE for details.

#include <ctime>
#include <string>

namespace dns::common {

/// Format current time as ISO 8601 UTC: "2026-03-17T01:30:00Z"
/// Thread-safe (uses gmtime_r).
std::string nowIso8601();

/// Format a time_t as ISO 8601 UTC: "2026-03-17T01:30:00Z"
/// Thread-safe (uses gmtime_r).
std::string toIso8601(std::time_t tt);

/// Format a time_t as compact ISO 8601 UTC for filenames: "2026-03-17T013000Z"
/// Thread-safe (uses gmtime_r).
std::string toIso8601Compact(std::time_t tt);

}  // namespace dns::common
