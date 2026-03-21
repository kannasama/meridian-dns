// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Meridian DNS Contributors
// This file is part of Meridian DNS. See LICENSE for details.
#include "dal/SoaPresetRepository.hpp"
#include "common/Errors.hpp"
#include "common/Logger.hpp"
#include "dal/ConnectionPool.hpp"
#include <gtest/gtest.h>
#include <cstdlib>
#include <pqxx/pqxx>

using dns::dal::ConnectionPool;
using dns::dal::SoaPresetRepository;
using dns::dal::SoaPresetRow;

namespace {
std::string getDbUrl() {
  const char* p = std::getenv("DNS_DB_URL");
  return p ? std::string(p) : std::string{};
}
}  // namespace

class SoaPresetRepositoryTest : public ::testing::Test {
 protected:
  void SetUp() override {
    _sDbUrl = getDbUrl();
    if (_sDbUrl.empty()) GTEST_SKIP() << "DNS_DB_URL not set — skipping integration test";
    dns::common::Logger::init("warn");
    _cpPool = std::make_unique<ConnectionPool>(_sDbUrl, 2);
    _repo   = std::make_unique<SoaPresetRepository>(*_cpPool);
    auto cg = _cpPool->checkout();
    pqxx::work txn(*cg);
    txn.exec("UPDATE zones SET soa_preset_id = NULL WHERE soa_preset_id IS NOT NULL");
    txn.exec("UPDATE zone_templates SET soa_preset_id = NULL WHERE soa_preset_id IS NOT NULL");
    txn.exec("DELETE FROM soa_presets");
    txn.commit();
  }
  std::string _sDbUrl;
  std::unique_ptr<ConnectionPool> _cpPool;
  std::unique_ptr<SoaPresetRepository> _repo;
};

TEST_F(SoaPresetRepositoryTest, CreateAndFindById) {
  int64_t iId = _repo->create("default-soa", "ns1.example.com.", "hostmaster.example.com.",
                               3600, 900, 604800, 300, 3600);
  EXPECT_GT(iId, 0);
  auto oRow = _repo->findById(iId);
  ASSERT_TRUE(oRow.has_value());
  EXPECT_EQ(oRow->sName, "default-soa");
  EXPECT_EQ(oRow->sMnameTemplate, "ns1.example.com.");
  EXPECT_EQ(oRow->sRnameTemplate, "hostmaster.example.com.");
  EXPECT_EQ(oRow->iRefresh, 3600);
  EXPECT_EQ(oRow->iRetry, 900);
  EXPECT_EQ(oRow->iExpire, 604800);
  EXPECT_EQ(oRow->iMinimum, 300);
  EXPECT_EQ(oRow->iDefaultTtl, 3600);
}

TEST_F(SoaPresetRepositoryTest, DuplicateNameThrows) {
  _repo->create("dup-soa", "ns1.example.com.", "hostmaster.example.com.",
                3600, 900, 604800, 300, 3600);
  EXPECT_THROW(
      _repo->create("dup-soa", "ns2.example.com.", "admin.example.com.",
                    7200, 1800, 1209600, 600, 7200),
      dns::common::ConflictError);
}

TEST_F(SoaPresetRepositoryTest, ListAll) {
  _repo->create("alpha-soa", "ns1.alpha.com.", "hostmaster.alpha.com.",
                3600, 900, 604800, 300, 3600);
  _repo->create("beta-soa", "ns1.beta.com.", "hostmaster.beta.com.",
                7200, 1800, 1209600, 600, 7200);
  auto vRows = _repo->listAll();
  EXPECT_GE(vRows.size(), 2u);
}

TEST_F(SoaPresetRepositoryTest, UpdatePreset) {
  int64_t iId = _repo->create("original-soa", "ns1.example.com.", "hostmaster.example.com.",
                               3600, 900, 604800, 300, 3600);
  auto oOriginal = _repo->findById(iId);
  ASSERT_TRUE(oOriginal.has_value());

  _repo->update(iId, "updated-soa", "ns2.example.com.", "hostmaster.example.com.",
                7200, 900, 604800, 300, 3600);

  auto oUpdated = _repo->findById(iId);
  ASSERT_TRUE(oUpdated.has_value());
  EXPECT_EQ(oUpdated->sName, "updated-soa");
  EXPECT_EQ(oUpdated->sMnameTemplate, "ns2.example.com.");
  EXPECT_EQ(oUpdated->iRefresh, 7200);
  EXPECT_GE(oUpdated->tpUpdatedAt, oOriginal->tpCreatedAt);
}

TEST_F(SoaPresetRepositoryTest, DeleteById) {
  int64_t iId = _repo->create("temp-soa", "ns1.example.com.", "hostmaster.example.com.",
                               3600, 900, 604800, 300, 3600);
  _repo->deleteById(iId);
  EXPECT_FALSE(_repo->findById(iId).has_value());
}

TEST_F(SoaPresetRepositoryTest, DeleteThrowsWhenInUse) {
  // Create a preset and a zone_template that references it
  int64_t iPresetId = _repo->create("in-use-preset", "ns1.", "admin.",
                                     3600, 900, 604800, 300, 3600);
  {
    auto cg = _cpPool->checkout();
    pqxx::work txn(*cg);
    txn.exec("INSERT INTO zone_templates (name, description, soa_preset_id) "
             "VALUES ('test-tmpl', '', $1)",
             pqxx::params{iPresetId});
    txn.commit();
  }
  EXPECT_THROW(_repo->deleteById(iPresetId), dns::common::ConflictError);

  // Cleanup
  {
    auto cg = _cpPool->checkout();
    pqxx::work txn(*cg);
    txn.exec("DELETE FROM zone_templates WHERE name = 'test-tmpl'");
    txn.commit();
  }
}
