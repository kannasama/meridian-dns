// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Meridian DNS Contributors
// This file is part of Meridian DNS. See LICENSE for details.

#include "common/TimeUtils.hpp"

#include <chrono>
#include <iomanip>
#include <sstream>

namespace dns::common {

std::string nowIso8601() {
  auto tp = std::chrono::system_clock::now();
  auto tt = std::chrono::system_clock::to_time_t(tp);
  return toIso8601(tt);
}

std::string toIso8601(std::time_t tt) {
  struct tm tmBuf {};
  gmtime_r(&tt, &tmBuf);
  std::ostringstream oss;
  oss << std::put_time(&tmBuf, "%FT%TZ");
  return oss.str();
}

std::string toIso8601Compact(std::time_t tt) {
  struct tm tmBuf {};
  gmtime_r(&tt, &tmBuf);
  std::ostringstream oss;
  oss << std::put_time(&tmBuf, "%Y-%m-%dT%H%M%SZ");
  return oss.str();
}

}  // namespace dns::common
