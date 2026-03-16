// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Meridian DNS Contributors
// This file is part of Meridian DNS. See LICENSE for details.

#include "api/routes/SetupRoutes.hpp"

#include "common/Logger.hpp"
#include "dal/ConnectionPool.hpp"
#include "dal/MigrationRunner.hpp"
#include "dal/UserRepository.hpp"
#include "security/HmacJwtSigner.hpp"

#include <gtest/gtest.h>
#include <pqxx/pqxx>

#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>

namespace fs = std::filesystem;

using dns::api::routes::SetupRoutes;
using dns::dal::ConnectionPool;
using dns::dal::MigrationRunner;
using dns::dal::UserRepository;
using dns::security::HmacJwtSigner;

namespace {

std::string getDbUrl() {
  const char* pUrl = std::getenv("DNS_DB_URL");
  return pUrl ? std::string(pUrl) : std::string{};
}

}  // namespace

class SetupRoutesTest : public ::testing::Test {
 protected:
  void SetUp() override {
    _sDbUrl = getDbUrl();
    if (_sDbUrl.empty()) {
      GTEST_SKIP() << "DNS_DB_URL not set — skipping integration test";
    }
    dns::common::Logger::init("warn");

    // Bootstrap the schema_version and system_config tables
    // Use a temp empty migrations dir so only bootstrap runs
    auto sTmpDir = fs::temp_directory_path() / "meridian_test_setup";
    fs::create_directories(sTmpDir);
    MigrationRunner mrRunner(_sDbUrl, sTmpDir.string());
    mrRunner.migrate();
    fs::remove_all(sTmpDir);

    // Verify required tables exist before proceeding
    if (!tablesExist()) {
      GTEST_SKIP() << "Required tables (users, groups, group_members) not found — skipping";
    }

    // Clean up test data
    cleanupDb();

    // Create dependencies
    _upPool = std::make_unique<ConnectionPool>(_sDbUrl, 2);
    _upUserRepo = std::make_unique<UserRepository>(*_upPool);
    _upSigner = std::make_unique<HmacJwtSigner>("test-secret-key-for-testing-only");
  }

  void TearDown() override {
    _upUserRepo.reset();
    _upPool.reset();
    cleanupDb();
  }

  void cleanupDb() {
    try {
      pqxx::connection conn(_sDbUrl);
      pqxx::work txn(conn);
      txn.exec("DELETE FROM system_config WHERE key = 'setup_completed'");
      txn.exec("DELETE FROM group_members");
      txn.exec("DELETE FROM groups WHERE name = 'Admins'");
      txn.exec("DELETE FROM users");
      txn.commit();
    } catch (...) {
    }
  }

  bool tablesExist() {
    try {
      pqxx::connection conn(_sDbUrl);
      pqxx::nontransaction ntxn(conn);
      auto result = ntxn.exec(
          "SELECT COUNT(*) FROM information_schema.tables "
          "WHERE table_name IN ('users', 'groups', 'group_members', 'system_config')");
      return result[0][0].as<int>() >= 4;
    } catch (...) {
      return false;
    }
  }

  std::string _sDbUrl;
  std::unique_ptr<ConnectionPool> _upPool;
  std::unique_ptr<UserRepository> _upUserRepo;
  std::unique_ptr<HmacJwtSigner> _upSigner;
};

TEST_F(SetupRoutesTest, LoadSetupStateDetectsIncompleteSetup) {
  SetupRoutes srRoutes(*_upPool, *_upUserRepo, *_upSigner);
  srRoutes.loadSetupState();

  EXPECT_FALSE(srRoutes.isSetupCompleted());
}

TEST_F(SetupRoutesTest, LoadSetupStateDetectsCompletedSetup) {
  // Insert setup_completed flag directly
  {
    pqxx::connection conn(_sDbUrl);
    pqxx::work txn(conn);
    txn.exec(
        "INSERT INTO system_config (key, value) VALUES ('setup_completed', 'true') "
        "ON CONFLICT (key) DO UPDATE SET value = 'true'");
    txn.commit();
  }

  SetupRoutes srRoutes(*_upPool, *_upUserRepo, *_upSigner);
  srRoutes.loadSetupState();

  EXPECT_TRUE(srRoutes.isSetupCompleted());
}

TEST_F(SetupRoutesTest, SetupTokenCanBeSetAndCleared) {
  SetupRoutes srRoutes(*_upPool, *_upUserRepo, *_upSigner);

  // Initially setup is not completed
  EXPECT_FALSE(srRoutes.isSetupCompleted());

  // Set a token — smoke test that it doesn't throw
  srRoutes.setSetupToken("test-token-value");

  // Clear the token
  srRoutes.setSetupToken("");
}
