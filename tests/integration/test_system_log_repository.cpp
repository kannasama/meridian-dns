// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Meridian DNS Contributors
// This file is part of Meridian DNS. See LICENSE for details.

#include "dal/SystemLogRepository.hpp"

#include <gtest/gtest.h>

#include "dal/ConnectionPool.hpp"

namespace {
const char* getDbUrl() {
  const char* url = std::getenv("DNS_DB_URL");
  return url ? url : nullptr;
}
}  // namespace

class SystemLogRepositoryTest : public ::testing::Test {
 protected:
  void SetUp() override {
    const char* dbUrl = getDbUrl();
    if (!dbUrl) GTEST_SKIP() << "DNS_DB_URL not set";
    pool_ = std::make_unique<dns::dal::ConnectionPool>(dbUrl, 2);
    repo_ = std::make_unique<dns::dal::SystemLogRepository>(*pool_);
  }
  std::unique_ptr<dns::dal::ConnectionPool> pool_;
  std::unique_ptr<dns::dal::SystemLogRepository> repo_;
};

TEST_F(SystemLogRepositoryTest, InsertAndQuery) {
  auto id = repo_->insert("deployment", "info", "Created record www.example.com. A",
                           std::nullopt, std::nullopt,
                           "create_record", "www.example.com.", "A",
                           true, 200, std::nullopt);
  EXPECT_GT(id, 0);

  auto rows = repo_->query("deployment");
  ASSERT_FALSE(rows.empty());
  auto& row = rows[0];
  EXPECT_EQ(row.sCategory, "deployment");
  EXPECT_EQ(row.sSeverity, "info");
}

TEST_F(SystemLogRepositoryTest, InsertError) {
  auto id = repo_->insert("provider", "error", "PowerDNS returned status 422",
                           42, 7,
                           "create_record", "www.example.com.", "A",
                           false, 422,
                           "RRset www.example.com. CNAME already exists");
  EXPECT_GT(id, 0);
  auto rows = repo_->query(std::nullopt, "error");
  ASSERT_FALSE(rows.empty());
}

TEST_F(SystemLogRepositoryTest, QueryByZoneId) {
  repo_->insert("deployment", "info", "Test zone filter", 99);
  auto rows = repo_->query(std::nullopt, std::nullopt, 99);
  ASSERT_FALSE(rows.empty());
}

TEST_F(SystemLogRepositoryTest, Purge) {
  repo_->insert("system", "info", "Old entry to be purged");
  auto iDeleted = repo_->purge(0);
  EXPECT_GE(iDeleted, 1);
}
