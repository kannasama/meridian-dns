#pragma once
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Meridian DNS Contributors
// This file is part of Meridian DNS. See LICENSE for details.

#include <chrono>
#include <mutex>
#include <string>
#include <unordered_map>

namespace dns::security {

/// In-memory cache to prevent SAML assertion replay.
class SamlReplayCache {
 public:
  SamlReplayCache();
  ~SamlReplayCache();

  /// Returns false if assertion_id was already seen (replay detected).
  bool checkAndInsert(const std::string& sAssertionId,
                      std::chrono::system_clock::time_point tpNotOnOrAfter);

 private:
  void evictExpired();

  std::unordered_map<std::string, std::chrono::system_clock::time_point> _mCache;
  std::mutex _mtx;
};

}  // namespace dns::security
