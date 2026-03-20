// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Meridian DNS Contributors
// This file is part of Meridian DNS. See LICENSE for details.

#include "dal/SystemConfigRepository.hpp"
#include "dal/ConnectionPool.hpp"

#include <gtest/gtest.h>
#include <cstdlib>
#include <regex>
#include <string>

static const char* kDbUrl = std::getenv("DNS_DB_URL");

class SystemConfigRepoTest : public ::testing::Test {
 protected:
  void SetUp() override {
    if (!kDbUrl) GTEST_SKIP() << "DNS_DB_URL not set";
    _cpPool = std::make_unique<dns::dal::ConnectionPool>(kDbUrl, 1);
    _repo   = std::make_unique<dns::dal::SystemConfigRepository>(*_cpPool);
  }
  std::unique_ptr<dns::dal::ConnectionPool> _cpPool;
  std::unique_ptr<dns::dal::SystemConfigRepository> _repo;
};

TEST_F(SystemConfigRepoTest, ReturnsValidSerialFormat) {
  std::string sSerial = _repo->getAndIncrementSerial();
  ASSERT_EQ(sSerial.size(), 10u);
  static const std::regex reSerial(R"(\d{8}\d{2})");
  EXPECT_TRUE(std::regex_match(sSerial, reSerial));
}

TEST_F(SystemConfigRepoTest, SequentialCallsIncrement) {
  std::string s1 = _repo->getAndIncrementSerial();
  std::string s2 = _repo->getAndIncrementSerial();
  // Same date prefix
  EXPECT_EQ(s1.substr(0, 8), s2.substr(0, 8));
  // Suffix increments by exactly 1
  int iSeq1 = std::stoi(s1.substr(8));
  int iSeq2 = std::stoi(s2.substr(8));
  EXPECT_EQ(iSeq2, iSeq1 + 1);
}
