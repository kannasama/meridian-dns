#pragma once
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Meridian DNS Contributors
// This file is part of Meridian DNS. See LICENSE for details.
#include <chrono>
#include <mutex>
#include <string>
#include <unordered_map>

namespace dns::api {

/// Token-bucket rate limiter keyed by client identifier (IP address).
/// Thread-safe. Class abbreviation: rl
class RateLimiter {
 public:
  RateLimiter(int iMaxRequests, std::chrono::steady_clock::duration durWindow);
  bool allow(const std::string& sKey);
 private:
  struct Bucket { int iTokens; std::chrono::steady_clock::time_point tpLastRefill; };
  void evictStale();
  int _iMaxRequests;
  std::chrono::steady_clock::duration _durWindow;
  std::unordered_map<std::string, Bucket> _mBuckets;
  std::mutex _mtx;
};

}  // namespace dns::api
