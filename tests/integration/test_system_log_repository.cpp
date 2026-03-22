// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Meridian DNS Contributors
// This file is part of Meridian DNS. See LICENSE for details.

#include "dal/SystemLogRepository.hpp"

#include <gtest/gtest.h>

#include <pqxx/pqxx>

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

    // Create a real view + zone for FK-safe testing
    auto cg = pool_->checkout();
    pqxx::work txn(*cg);
    auto vr = txn.exec(
        "INSERT INTO views (name, description) VALUES ('slr-test', 'test') RETURNING id");
    iViewId_ = vr[0][0].as<int64_t>();
    auto zr = txn.exec(
        "INSERT INTO zones (name, view_id) VALUES ('slr-test.example.com', $1) RETURNING id",
        pqxx::params{iViewId_});
    iZoneId_ = zr[0][0].as<int64_t>();
    txn.commit();
  }

  void TearDown() override {
    if (!pool_) return;
    auto cg = pool_->checkout();
    pqxx::work txn(*cg);
    txn.exec("DELETE FROM system_logs WHERE zone_id = $1 OR zone_id IS NULL",
             pqxx::params{iZoneId_});
    txn.exec("DELETE FROM zones WHERE id = $1", pqxx::params{iZoneId_});
    txn.exec("DELETE FROM views WHERE id = $1", pqxx::params{iViewId_});
    txn.commit();
  }

  std::unique_ptr<dns::dal::ConnectionPool> pool_;
  std::unique_ptr<dns::dal::SystemLogRepository> repo_;
  int64_t iViewId_ = 0;
  int64_t iZoneId_ = 0;
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
                           iZoneId_, std::nullopt,
                           "create_record", "www.example.com.", "A",
                           false, 422,
                           "RRset www.example.com. CNAME already exists");
  EXPECT_GT(id, 0);
  auto rows = repo_->query(std::nullopt, "error");
  ASSERT_FALSE(rows.empty());
}

TEST_F(SystemLogRepositoryTest, QueryByZoneId) {
  repo_->insert("deployment", "info", "Test zone filter", iZoneId_);
  auto rows = repo_->query(std::nullopt, std::nullopt, iZoneId_);
  ASSERT_FALSE(rows.empty());
}

TEST_F(SystemLogRepositoryTest, Purge) {
  repo_->insert("system", "info", "Old entry to be purged");
  auto iDeleted = repo_->purge(0);
  EXPECT_GE(iDeleted, 1);
}
