// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Meridian DNS Contributors
// This file is part of Meridian DNS. See LICENSE for details.

#include "api/RateLimiter.hpp"
#include <gtest/gtest.h>
#include <thread>
using dns::api::RateLimiter;

TEST(RateLimiterTest, AllowsWithinLimit) {
  RateLimiter rl(5, std::chrono::seconds(60));
  for (int i = 0; i < 5; ++i) EXPECT_TRUE(rl.allow("192.168.1.1"));
}

TEST(RateLimiterTest, BlocksOverLimit) {
  RateLimiter rl(3, std::chrono::seconds(60));
  EXPECT_TRUE(rl.allow("ip")); EXPECT_TRUE(rl.allow("ip")); EXPECT_TRUE(rl.allow("ip"));
  EXPECT_FALSE(rl.allow("ip"));
}

TEST(RateLimiterTest, DifferentKeysIndependent) {
  RateLimiter rl(2, std::chrono::seconds(60));
  EXPECT_TRUE(rl.allow("a")); EXPECT_TRUE(rl.allow("a")); EXPECT_FALSE(rl.allow("a"));
  EXPECT_TRUE(rl.allow("b")); EXPECT_TRUE(rl.allow("b")); EXPECT_FALSE(rl.allow("b"));
}

TEST(RateLimiterTest, TokensRefillAfterWindow) {
  RateLimiter rl(2, std::chrono::milliseconds(200));
  EXPECT_TRUE(rl.allow("k")); EXPECT_TRUE(rl.allow("k")); EXPECT_FALSE(rl.allow("k"));
  std::this_thread::sleep_for(std::chrono::milliseconds(250));
  EXPECT_TRUE(rl.allow("k"));
}
