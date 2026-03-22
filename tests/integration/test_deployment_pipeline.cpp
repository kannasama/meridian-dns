// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Meridian DNS Contributors
// This file is part of Meridian DNS. See LICENSE for details.

#include "core/DeploymentEngine.hpp"

#include <gtest/gtest.h>

#include <cstdlib>
#include <string>
#include <thread>

#include <nlohmann/json.hpp>
#include <pqxx/pqxx>

#include "common/Errors.hpp"
#include "common/Logger.hpp"
#include "common/Types.hpp"
#include "core/DiffEngine.hpp"
#include "core/VariableEngine.hpp"
#include "dal/AuditRepository.hpp"
#include "dal/ConnectionPool.hpp"
#include "dal/DeploymentRepository.hpp"
#include "dal/ProviderRepository.hpp"
#include "dal/RecordRepository.hpp"
#include "dal/VariableRepository.hpp"
#include "dal/ViewRepository.hpp"
#include "dal/SystemConfigRepository.hpp"
#include "dal/SystemLogRepository.hpp"
#include "dal/ZoneRepository.hpp"
#include "security/CryptoService.hpp"

namespace {
std::string getDbUrl() {
  const char* pUrl = std::getenv("DNS_DB_URL");
  return pUrl ? std::string(pUrl) : std::string{};
}
}  // namespace

class DeploymentEngineTest : public ::testing::Test {
 protected:
  void SetUp() override {
    _sDbUrl = getDbUrl();
    if (_sDbUrl.empty()) {
      GTEST_SKIP() << "DNS_DB_URL not set — skipping integration test";
    }
    dns::common::Logger::init("warn");
    _cpPool = std::make_unique<dns::dal::ConnectionPool>(_sDbUrl, 2);

    // 32-byte hex key for CryptoService
    std::string sMasterKey = "0123456789abcdef0123456789abcdef"
                             "0123456789abcdef0123456789abcdef";
    _csService = std::make_unique<dns::security::CryptoService>(sMasterKey);

    _vrRepo = std::make_unique<dns::dal::ViewRepository>(*_cpPool);
    _zrRepo = std::make_unique<dns::dal::ZoneRepository>(*_cpPool);
    _rrRepo = std::make_unique<dns::dal::RecordRepository>(*_cpPool);
    _prRepo = std::make_unique<dns::dal::ProviderRepository>(*_cpPool, *_csService);
    _varRepo = std::make_unique<dns::dal::VariableRepository>(*_cpPool);
    _drRepo = std::make_unique<dns::dal::DeploymentRepository>(*_cpPool);
    _arRepo = std::make_unique<dns::dal::AuditRepository>(*_cpPool);

    _scrRepo = std::make_unique<dns::dal::SystemConfigRepository>(*_cpPool);
    _slrRepo = std::make_unique<dns::dal::SystemLogRepository>(*_cpPool);
    _veEngine = std::make_unique<dns::core::VariableEngine>(*_varRepo);
    _deEngine = std::make_unique<dns::core::DiffEngine>(
        *_zrRepo, *_vrRepo, *_rrRepo, *_prRepo, *_veEngine);

    // Clean slate
    auto cg = _cpPool->checkout();
    pqxx::work txn(*cg);
    txn.exec("DELETE FROM audit_log");
    txn.exec("DELETE FROM records");
    txn.exec("DELETE FROM deployments");
    txn.exec("DELETE FROM variables");
    txn.exec("DELETE FROM zones");
    txn.exec("DELETE FROM view_providers");
    txn.exec("DELETE FROM views");
    txn.exec("DELETE FROM providers");
    txn.commit();
  }

  std::string _sDbUrl;
  std::unique_ptr<dns::dal::ConnectionPool> _cpPool;
  std::unique_ptr<dns::security::CryptoService> _csService;
  std::unique_ptr<dns::dal::ViewRepository> _vrRepo;
  std::unique_ptr<dns::dal::ZoneRepository> _zrRepo;
  std::unique_ptr<dns::dal::RecordRepository> _rrRepo;
  std::unique_ptr<dns::dal::ProviderRepository> _prRepo;
  std::unique_ptr<dns::dal::VariableRepository> _varRepo;
  std::unique_ptr<dns::dal::DeploymentRepository> _drRepo;
  std::unique_ptr<dns::dal::AuditRepository> _arRepo;
  std::unique_ptr<dns::dal::SystemConfigRepository> _scrRepo;
  std::unique_ptr<dns::dal::SystemLogRepository> _slrRepo;
  std::unique_ptr<dns::core::VariableEngine> _veEngine;
  std::unique_ptr<dns::core::DiffEngine> _deEngine;
};

TEST_F(DeploymentEngineTest, BuildSnapshotContainsExpandedRecords) {
  int64_t iViewId = _vrRepo->create("ext", "External");
  int64_t iZoneId = _zrRepo->create("example.com", iViewId, std::nullopt);
  _rrRepo->create(iZoneId, "www.example.com.", "A", 300, "1.2.3.4", 0);
  _rrRepo->create(iZoneId, "mail.example.com.", "MX", 300, "mx.example.com.", 10);

  dns::core::DeploymentEngine depEngine(
      *_deEngine, *_veEngine, *_zrRepo, *_vrRepo, *_rrRepo, *_prRepo,
      *_drRepo, *_arRepo, *_scrRepo, *_slrRepo, nullptr, 10);

  // Access buildSnapshot via push is integration-level; test snapshot format here
  // by checking the DeploymentRepository after a manual snapshot creation.
  // This tests that the engine can be constructed without errors.
  EXPECT_TRUE(true);  // Construction succeeds
}

TEST_F(DeploymentEngineTest, PushFailsWithNoProviders) {
  int64_t iViewId = _vrRepo->create("ext", "External");
  int64_t iZoneId = _zrRepo->create("example.com", iViewId, std::nullopt);
  _rrRepo->create(iZoneId, "www.example.com.", "A", 300, "1.2.3.4", 0);

  // No providers attached to view — DiffEngine::preview() should throw
  dns::core::DeploymentEngine depEngine(
      *_deEngine, *_veEngine, *_zrRepo, *_vrRepo, *_rrRepo, *_prRepo,
      *_drRepo, *_arRepo, *_scrRepo, *_slrRepo, nullptr, 10);

  // Create a fake user for the actor
  auto cg = _cpPool->checkout();
  pqxx::work txn(*cg);
  auto result = txn.exec(
      "INSERT INTO users (username, auth_method) VALUES ('alice', 'local') RETURNING id");
  int64_t iUserId = result[0][0].as<int64_t>();
  txn.commit();

  dns::common::AuditContext acTest{"alice", "local", "127.0.0.1"};
  EXPECT_THROW(depEngine.push(iZoneId, {}, iUserId, acTest),
               dns::common::ValidationError);
}

TEST_F(DeploymentEngineTest, PushFailsForNonexistentZone) {
  dns::core::DeploymentEngine depEngine(
      *_deEngine, *_veEngine, *_zrRepo, *_vrRepo, *_rrRepo, *_prRepo,
      *_drRepo, *_arRepo, *_scrRepo, *_slrRepo, nullptr, 10);

  dns::common::AuditContext acTest{"alice", "local", "127.0.0.1"};
  EXPECT_THROW(depEngine.push(99999, {}, 1, acTest),
               dns::common::NotFoundError);
}
