// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Meridian DNS Contributors
// This file is part of Meridian DNS. See LICENSE for details.

#include "core/BackupService.hpp"
#include "dal/ConnectionPool.hpp"
#include "dal/GitRepoRepository.hpp"
#include "dal/GroupRepository.hpp"
#include "dal/IdpRepository.hpp"
#include "dal/ProviderRepository.hpp"
#include "dal/RecordRepository.hpp"
#include "dal/RoleRepository.hpp"
#include "dal/SettingsRepository.hpp"
#include "dal/UserRepository.hpp"
#include "dal/VariableRepository.hpp"
#include "dal/ViewRepository.hpp"
#include "dal/ZoneRepository.hpp"
#include "common/Logger.hpp"
#include "security/CryptoService.hpp"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <cstdlib>
#include <memory>
#include <string>

namespace {

std::string getDbUrl() {
  const char* p = std::getenv("DNS_DB_URL");
  return p ? std::string(p) : "";
}

}  // namespace

class BackupRestoreTest : public ::testing::Test {
 protected:
  void SetUp() override {
    _sDbUrl = getDbUrl();
    if (_sDbUrl.empty()) {
      GTEST_SKIP() << "DNS_DB_URL not set — skipping DB integration test";
    }
    dns::common::Logger::init("warn");

    _cpPool = std::make_unique<dns::dal::ConnectionPool>(_sDbUrl, 2);
    _csService = std::make_unique<dns::security::CryptoService>(
        "0123456789abcdef0123456789abcdef");

    _stRepo = std::make_unique<dns::dal::SettingsRepository>(*_cpPool);
    _rlRepo = std::make_unique<dns::dal::RoleRepository>(*_cpPool);
    _grRepo = std::make_unique<dns::dal::GroupRepository>(*_cpPool);
    _urRepo = std::make_unique<dns::dal::UserRepository>(*_cpPool);
    _irRepo = std::make_unique<dns::dal::IdpRepository>(*_cpPool, *_csService);
    _grRepoGit = std::make_unique<dns::dal::GitRepoRepository>(*_cpPool, *_csService);
    _prRepo = std::make_unique<dns::dal::ProviderRepository>(*_cpPool, *_csService);
    _vrRepo = std::make_unique<dns::dal::ViewRepository>(*_cpPool);
    _zrRepo = std::make_unique<dns::dal::ZoneRepository>(*_cpPool);
    _rrRepo = std::make_unique<dns::dal::RecordRepository>(*_cpPool);
    _varRepo = std::make_unique<dns::dal::VariableRepository>(*_cpPool);

    _bsService = std::make_unique<dns::core::BackupService>(
        *_cpPool, *_stRepo, *_rlRepo, *_grRepo, *_urRepo, *_irRepo,
        *_grRepoGit, *_prRepo, *_vrRepo, *_zrRepo, *_rrRepo, *_varRepo);

    // Clean test data
    auto cg = _cpPool->checkout();
    pqxx::work txn(*cg);
    txn.exec("DELETE FROM records WHERE zone_id IN "
             "(SELECT id FROM zones WHERE name LIKE 'bktest-%')");
    txn.exec("DELETE FROM variables WHERE name LIKE 'bktest_%'");
    txn.exec("DELETE FROM zones WHERE name LIKE 'bktest-%'");
    txn.exec("DELETE FROM view_providers WHERE view_id IN "
             "(SELECT id FROM views WHERE name LIKE 'bktest-%')");
    txn.exec("DELETE FROM views WHERE name LIKE 'bktest-%'");
    txn.exec("DELETE FROM providers WHERE name LIKE 'bktest-%'");
    txn.exec("DELETE FROM git_repos WHERE name LIKE 'bktest-%'");
    txn.exec("DELETE FROM identity_providers WHERE name LIKE 'bktest-%'");
    txn.exec("DELETE FROM group_members WHERE user_id IN "
             "(SELECT id FROM users WHERE username LIKE 'bktest_%')");
    txn.exec("DELETE FROM users WHERE username LIKE 'bktest_%'");
    txn.exec("DELETE FROM groups WHERE name LIKE 'bktest-%'");
    txn.exec("DELETE FROM role_permissions WHERE role_id IN "
             "(SELECT id FROM roles WHERE name LIKE 'bktest-%')");
    txn.exec("DELETE FROM roles WHERE name LIKE 'bktest-%'");
    txn.exec("DELETE FROM system_config WHERE key LIKE 'bktest.%'");
    txn.commit();
  }

  void TearDown() override {
    if (_sDbUrl.empty()) return;
    // Clean up test data
    auto cg = _cpPool->checkout();
    pqxx::work txn(*cg);
    txn.exec("DELETE FROM records WHERE zone_id IN "
             "(SELECT id FROM zones WHERE name LIKE 'bktest-%')");
    txn.exec("DELETE FROM variables WHERE name LIKE 'bktest_%'");
    txn.exec("DELETE FROM zones WHERE name LIKE 'bktest-%'");
    txn.exec("DELETE FROM view_providers WHERE view_id IN "
             "(SELECT id FROM views WHERE name LIKE 'bktest-%')");
    txn.exec("DELETE FROM views WHERE name LIKE 'bktest-%'");
    txn.exec("DELETE FROM providers WHERE name LIKE 'bktest-%'");
    txn.exec("DELETE FROM git_repos WHERE name LIKE 'bktest-%'");
    txn.exec("DELETE FROM identity_providers WHERE name LIKE 'bktest-%'");
    txn.exec("DELETE FROM group_members WHERE user_id IN "
             "(SELECT id FROM users WHERE username LIKE 'bktest_%')");
    txn.exec("DELETE FROM users WHERE username LIKE 'bktest_%'");
    txn.exec("DELETE FROM groups WHERE name LIKE 'bktest-%'");
    txn.exec("DELETE FROM role_permissions WHERE role_id IN "
             "(SELECT id FROM roles WHERE name LIKE 'bktest-%')");
    txn.exec("DELETE FROM roles WHERE name LIKE 'bktest-%'");
    txn.exec("DELETE FROM system_config WHERE key LIKE 'bktest.%'");
    txn.commit();
  }

  std::string _sDbUrl;
  std::unique_ptr<dns::dal::ConnectionPool> _cpPool;
  std::unique_ptr<dns::security::CryptoService> _csService;
  std::unique_ptr<dns::dal::SettingsRepository> _stRepo;
  std::unique_ptr<dns::dal::RoleRepository> _rlRepo;
  std::unique_ptr<dns::dal::GroupRepository> _grRepo;
  std::unique_ptr<dns::dal::UserRepository> _urRepo;
  std::unique_ptr<dns::dal::IdpRepository> _irRepo;
  std::unique_ptr<dns::dal::GitRepoRepository> _grRepoGit;
  std::unique_ptr<dns::dal::ProviderRepository> _prRepo;
  std::unique_ptr<dns::dal::ViewRepository> _vrRepo;
  std::unique_ptr<dns::dal::ZoneRepository> _zrRepo;
  std::unique_ptr<dns::dal::RecordRepository> _rrRepo;
  std::unique_ptr<dns::dal::VariableRepository> _varRepo;
  std::unique_ptr<dns::core::BackupService> _bsService;
};

TEST_F(BackupRestoreTest, ExportProducesValidJson) {
  auto jExport = _bsService->exportSystem("test-admin");

  EXPECT_EQ(jExport["version"], 1);
  EXPECT_TRUE(jExport.contains("exported_at"));
  EXPECT_EQ(jExport["exported_by"], "test-admin");
  EXPECT_TRUE(jExport.contains("meridian_version"));

  // All sections present
  for (const auto& sKey : {"settings", "roles", "groups", "users",
                           "identity_providers", "git_repos", "providers",
                           "views", "zones", "records", "variables"}) {
    EXPECT_TRUE(jExport.contains(sKey)) << "Missing section: " << sKey;
  }
}

TEST_F(BackupRestoreTest, ExportExcludesCredentials) {
  // Create a provider with a token to verify it's excluded
  _prRepo->create("bktest-provider", "powerdns", "http://localhost:8081",
                  "secret-token-123");

  auto jExport = _bsService->exportSystem("test-admin");

  // Check providers section doesn't have password-like fields
  auto sDump = jExport.dump();
  EXPECT_EQ(sDump.find("secret-token-123"), std::string::npos)
      << "Encrypted token should not appear in export";
  EXPECT_EQ(sDump.find("password_hash"), std::string::npos)
      << "password_hash field should not appear in export";
  EXPECT_EQ(sDump.find("encrypted_token"), std::string::npos)
      << "encrypted_token field should not appear in export";
}

TEST_F(BackupRestoreTest, RestorePreviewCountsCorrect) {
  // Create some test entities
  auto iViewId = _vrRepo->create("bktest-view1", "Test view");
  auto iZoneId = _zrRepo->create("bktest-zone1.com", iViewId, std::nullopt);
  _rrRepo->create(iZoneId, "www", "A", 300, "1.2.3.4", 0);

  // Export
  auto jExport = _bsService->exportSystem("test-admin");

  // Modify: add a new record that won't exist in the backup
  _rrRepo->create(iZoneId, "mail", "MX", 300, "mail.example.com", 10);

  // Preview restore
  auto result = _bsService->previewRestore(jExport);

  EXPECT_FALSE(result.bApplied);

  // Find zones summary
  bool bFoundZones = false;
  for (const auto& s : result.vSummaries) {
    if (s.sEntityType == "zones") {
      bFoundZones = true;
      // The zone should be updated (already exists)
      EXPECT_GE(s.iUpdated, 1) << "Zone should count as update";
    }
  }
  EXPECT_TRUE(bFoundZones) << "Should have zones summary";
}

TEST_F(BackupRestoreTest, RestoreApplyIsIdempotent) {
  // Create test entities
  _stRepo->upsert("bktest.key1", "value1");
  auto iViewId = _vrRepo->create("bktest-view1", "Test view");
  auto iZoneId = _zrRepo->create("bktest-zone1.com", iViewId, std::nullopt);
  _rrRepo->create(iZoneId, "www", "A", 300, "1.2.3.4", 0);

  // Export
  auto jExport1 = _bsService->exportSystem("test-admin");

  // Apply restore (should be mostly updates/skips since data already exists)
  auto result1 = _bsService->applyRestore(jExport1);
  EXPECT_TRUE(result1.bApplied);

  // Re-export
  auto jExport2 = _bsService->exportSystem("test-admin");

  // Compare key sections (exclude timestamps)
  EXPECT_EQ(jExport1["settings"], jExport2["settings"]);
  EXPECT_EQ(jExport1["zones"], jExport2["zones"]);
  EXPECT_EQ(jExport1["records"], jExport2["records"]);
  EXPECT_EQ(jExport1["variables"], jExport2["variables"]);
}

TEST_F(BackupRestoreTest, RestoreFlagsCredentialWarnings) {
  // Build a backup with a new provider and git repo
  nlohmann::json jBackup;
  jBackup["version"] = 1;
  jBackup["exported_at"] = "2026-03-15T00:00:00Z";
  jBackup["exported_by"] = "test-admin";
  jBackup["meridian_version"] = "0.1.0";
  jBackup["settings"] = nlohmann::json::array();
  jBackup["roles"] = nlohmann::json::array();
  jBackup["groups"] = nlohmann::json::array();
  jBackup["users"] = nlohmann::json::array();
  jBackup["identity_providers"] = nlohmann::json::array();
  jBackup["views"] = nlohmann::json::array();
  jBackup["zones"] = nlohmann::json::array();
  jBackup["records"] = nlohmann::json::array();
  jBackup["variables"] = nlohmann::json::array();

  jBackup["providers"] = nlohmann::json::array({
      {{"name", "bktest-new-provider"}, {"type", "powerdns"},
       {"api_endpoint", "http://localhost:8081"}},
  });
  jBackup["git_repos"] = nlohmann::json::array({
      {{"name", "bktest-new-repo"}, {"remote_url", "git@example.com:test.git"},
       {"auth_type", "ssh"}, {"default_branch", "main"}},
  });

  auto result = _bsService->previewRestore(jBackup);

  EXPECT_FALSE(result.bApplied);
  ASSERT_GE(result.vCredentialWarnings.size(), 2u);

  bool bFoundProvider = false;
  bool bFoundGitRepo = false;
  for (const auto& sWarn : result.vCredentialWarnings) {
    if (sWarn == "provider:bktest-new-provider") bFoundProvider = true;
    if (sWarn == "git_repo:bktest-new-repo") bFoundGitRepo = true;
  }
  EXPECT_TRUE(bFoundProvider) << "Should warn about new provider credentials";
  EXPECT_TRUE(bFoundGitRepo) << "Should warn about new git repo credentials";
}

TEST_F(BackupRestoreTest, ZoneExportRoundTrip) {
  // Create a zone with records
  auto iViewId = _vrRepo->create("bktest-view1", "Test view");
  auto iZoneId = _zrRepo->create("bktest-zone1.com", iViewId, std::nullopt);
  _rrRepo->create(iZoneId, "www", "A", 300, "1.2.3.4", 0);
  _rrRepo->create(iZoneId, "mail", "MX", 300, "mail.bktest-zone1.com", 10);

  // Export zone
  auto jZoneExport = _bsService->exportZone(iZoneId);

  EXPECT_EQ(jZoneExport["version"], 1);
  EXPECT_EQ(jZoneExport["type"], "zone");
  EXPECT_TRUE(jZoneExport.contains("zone"));
  EXPECT_EQ(jZoneExport["zone"]["name"], "bktest-zone1.com");
  EXPECT_EQ(jZoneExport["records"].size(), 2u);
}

TEST_F(BackupRestoreTest, RestoreCreatesWithEmptyCredentials) {
  nlohmann::json jBackup;
  jBackup["version"] = 1;
  jBackup["exported_at"] = "2026-03-15T00:00:00Z";
  jBackup["exported_by"] = "test-admin";
  jBackup["meridian_version"] = "0.1.0";
  jBackup["settings"] = nlohmann::json::array();
  jBackup["roles"] = nlohmann::json::array();
  jBackup["groups"] = nlohmann::json::array();
  jBackup["users"] = nlohmann::json::array();
  jBackup["identity_providers"] = nlohmann::json::array();
  jBackup["views"] = nlohmann::json::array();
  jBackup["zones"] = nlohmann::json::array();
  jBackup["records"] = nlohmann::json::array();
  jBackup["variables"] = nlohmann::json::array();

  jBackup["providers"] = nlohmann::json::array({
      {{"name", "bktest-empty-cred-provider"}, {"type", "powerdns"},
       {"api_endpoint", "http://localhost:8081"}},
  });
  jBackup["git_repos"] = nlohmann::json::array({
      {{"name", "bktest-empty-cred-repo"}, {"remote_url", "https://example.com/repo.git"},
       {"auth_type", "none"}, {"default_branch", "main"}},
  });

  auto result = _bsService->applyRestore(jBackup);
  EXPECT_TRUE(result.bApplied);

  // Verify providers were created with empty tokens
  auto cg = _cpPool->checkout();
  pqxx::work txn(*cg);
  auto rProv = txn.exec(
      "SELECT encrypted_token FROM providers WHERE name = 'bktest-empty-cred-provider'");
  ASSERT_EQ(rProv.size(), 1u);
  EXPECT_EQ(rProv[0][0].as<std::string>(), "");

  auto rRepo = txn.exec(
      "SELECT encrypted_credentials FROM git_repos WHERE name = 'bktest-empty-cred-repo'");
  ASSERT_EQ(rRepo.size(), 1u);
  EXPECT_TRUE(rRepo[0][0].is_null());
  txn.commit();
}
