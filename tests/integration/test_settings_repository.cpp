// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Meridian DNS Contributors
// This file is part of Meridian DNS. See LICENSE for details.

#include "dal/SettingsRepository.hpp"
#include "dal/ConnectionPool.hpp"
#include "common/Logger.hpp"

#include <gtest/gtest.h>

#include <cstdlib>
#include <memory>
#include <string>

namespace {

std::string getDbUrl() {
  const char* p = std::getenv("DNS_DB_URL");
  return p ? std::string(p) : "";
}

}  // namespace

class SettingsRepositoryTest : public ::testing::Test {
 protected:
  void SetUp() override {
    _sDbUrl = getDbUrl();
    if (_sDbUrl.empty()) {
      GTEST_SKIP() << "DNS_DB_URL not set — skipping DB integration test";
    }
    dns::common::Logger::init("warn");
    _cpPool = std::make_unique<dns::dal::ConnectionPool>(_sDbUrl, 2);
    _srRepo = std::make_unique<dns::dal::SettingsRepository>(*_cpPool);

    // Ensure system_config exists (created by MigrationRunner::bootstrap in production;
    // guard here so tests run independently of test execution order)
    auto cg = _cpPool->checkout();
    pqxx::work txn(*cg);
    txn.exec("CREATE TABLE IF NOT EXISTS system_config ("
             "  key TEXT PRIMARY KEY, value TEXT NOT NULL,"
             "  description TEXT, updated_at TIMESTAMPTZ DEFAULT now())");
    // Clean test keys (leave setup_completed alone)
    txn.exec("DELETE FROM system_config WHERE key LIKE 'test.%'");
    txn.commit();
  }

  std::string _sDbUrl;
  std::unique_ptr<dns::dal::ConnectionPool> _cpPool;
  std::unique_ptr<dns::dal::SettingsRepository> _srRepo;
};

TEST_F(SettingsRepositoryTest, SeedIfMissing_InsertsNewKey) {
  bool bInserted = _srRepo->seedIfMissing("test.key1", "value1", "A test setting");
  EXPECT_TRUE(bInserted);

  auto oRow = _srRepo->findByKey("test.key1");
  ASSERT_TRUE(oRow.has_value());
  EXPECT_EQ(oRow->sValue, "value1");
  EXPECT_EQ(oRow->sDescription, "A test setting");
}

TEST_F(SettingsRepositoryTest, SeedIfMissing_DoesNotOverwriteExisting) {
  _srRepo->upsert("test.key2", "original", "desc");

  bool bInserted = _srRepo->seedIfMissing("test.key2", "seeded_value", "new desc");
  EXPECT_FALSE(bInserted);

  auto oRow = _srRepo->findByKey("test.key2");
  ASSERT_TRUE(oRow.has_value());
  EXPECT_EQ(oRow->sValue, "original");
}

TEST_F(SettingsRepositoryTest, Upsert_InsertsAndUpdates) {
  _srRepo->upsert("test.key3", "v1", "initial");
  auto oRow = _srRepo->findByKey("test.key3");
  ASSERT_TRUE(oRow.has_value());
  EXPECT_EQ(oRow->sValue, "v1");

  _srRepo->upsert("test.key3", "v2", "updated");
  oRow = _srRepo->findByKey("test.key3");
  ASSERT_TRUE(oRow.has_value());
  EXPECT_EQ(oRow->sValue, "v2");
  EXPECT_EQ(oRow->sDescription, "updated");
}

TEST_F(SettingsRepositoryTest, FindByKey_ReturnsNulloptForMissing) {
  auto oRow = _srRepo->findByKey("test.nonexistent");
  EXPECT_FALSE(oRow.has_value());
}

TEST_F(SettingsRepositoryTest, GetValue_ReturnsDefaultForMissing) {
  std::string sVal = _srRepo->getValue("test.missing", "fallback");
  EXPECT_EQ(sVal, "fallback");
}

TEST_F(SettingsRepositoryTest, GetInt_ParsesValueOrReturnsDefault) {
  _srRepo->upsert("test.int_key", "42");
  EXPECT_EQ(_srRepo->getInt("test.int_key", 0), 42);

  _srRepo->upsert("test.bad_int", "not_a_number");
  EXPECT_EQ(_srRepo->getInt("test.bad_int", 99), 99);

  EXPECT_EQ(_srRepo->getInt("test.missing_int", 7), 7);
}

TEST_F(SettingsRepositoryTest, GetBool_ParsesValueOrReturnsDefault) {
  _srRepo->upsert("test.bool_true", "true");
  EXPECT_TRUE(_srRepo->getBool("test.bool_true", false));

  _srRepo->upsert("test.bool_one", "1");
  EXPECT_TRUE(_srRepo->getBool("test.bool_one", false));

  _srRepo->upsert("test.bool_false", "false");
  EXPECT_FALSE(_srRepo->getBool("test.bool_false", true));

  EXPECT_TRUE(_srRepo->getBool("test.missing_bool", true));
}

TEST_F(SettingsRepositoryTest, ListAll_ReturnsAllSettings) {
  _srRepo->upsert("test.list_a", "a");
  _srRepo->upsert("test.list_b", "b");

  auto vAll = _srRepo->listAll();
  // Should contain at least our 2 test keys (plus setup_completed if present)
  int iTestKeys = 0;
  for (const auto& sr : vAll) {
    if (sr.sKey == "test.list_a" || sr.sKey == "test.list_b") {
      ++iTestKeys;
    }
  }
  EXPECT_EQ(iTestKeys, 2);
}

TEST_F(SettingsRepositoryTest, DeleteByKey_RemovesKey) {
  _srRepo->upsert("test.delete_me", "bye");
  EXPECT_TRUE(_srRepo->findByKey("test.delete_me").has_value());

  _srRepo->deleteByKey("test.delete_me");
  EXPECT_FALSE(_srRepo->findByKey("test.delete_me").has_value());
}
