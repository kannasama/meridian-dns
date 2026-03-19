// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Meridian DNS Contributors
// This file is part of Meridian DNS. See LICENSE for details.
#include "dal/ZoneTemplateRepository.hpp"
#include "dal/SnippetRepository.hpp"
#include "dal/SoaPresetRepository.hpp"
#include "common/Errors.hpp"
#include "common/Logger.hpp"
#include "dal/ConnectionPool.hpp"
#include <gtest/gtest.h>
#include <cstdlib>
#include <pqxx/pqxx>

using dns::dal::ConnectionPool;
using dns::dal::SnippetRepository;
using dns::dal::SoaPresetRepository;
using dns::dal::ZoneTemplateRepository;
using dns::dal::ZoneTemplateRow;

namespace {
std::string getDbUrl() {
  const char* p = std::getenv("DNS_DB_URL");
  return p ? std::string(p) : std::string{};
}
}  // namespace

class ZoneTemplateRepositoryTest : public ::testing::Test {
 protected:
  void SetUp() override {
    _sDbUrl = getDbUrl();
    if (_sDbUrl.empty()) GTEST_SKIP() << "DNS_DB_URL not set — skipping integration test";
    dns::common::Logger::init("warn");
    _cpPool  = std::make_unique<ConnectionPool>(_sDbUrl, 2);
    _repo    = std::make_unique<ZoneTemplateRepository>(*_cpPool);
    _snipRepo = std::make_unique<SnippetRepository>(*_cpPool);
    _soaRepo  = std::make_unique<SoaPresetRepository>(*_cpPool);

    auto cg = _cpPool->checkout();
    pqxx::work txn(*cg);
    txn.exec("UPDATE zones SET template_id=NULL, template_check_pending=FALSE "
             "WHERE template_id IS NOT NULL");
    txn.exec("DELETE FROM zone_template_snippets");
    txn.exec("DELETE FROM zone_templates");
    txn.exec("DELETE FROM snippet_records");
    txn.exec("DELETE FROM snippets");
    txn.exec("UPDATE zones SET soa_preset_id=NULL WHERE soa_preset_id IS NOT NULL");
    txn.exec("DELETE FROM soa_presets");
    txn.commit();
  }

  std::string _sDbUrl;
  std::unique_ptr<ConnectionPool> _cpPool;
  std::unique_ptr<ZoneTemplateRepository> _repo;
  std::unique_ptr<SnippetRepository> _snipRepo;
  std::unique_ptr<SoaPresetRepository> _soaRepo;
};

TEST_F(ZoneTemplateRepositoryTest, CreateAndFindById) {
  int64_t iId = _repo->create("basic-template", "A basic template", std::nullopt);
  EXPECT_GT(iId, 0);

  auto oRow = _repo->findById(iId);
  ASSERT_TRUE(oRow.has_value());
  EXPECT_EQ(oRow->sName, "basic-template");
  EXPECT_EQ(oRow->sDescription, "A basic template");
  EXPECT_FALSE(oRow->oSoaPresetId.has_value());
  EXPECT_TRUE(oRow->vSnippetIds.empty());
}

TEST_F(ZoneTemplateRepositoryTest, CreateWithSoaPreset) {
  int64_t iPresetId = _soaRepo->create("preset-for-tmpl", "ns1.example.com.",
                                        "hostmaster.example.com.",
                                        3600, 900, 604800, 300, 3600);

  int64_t iId = _repo->create("preset-template", "Template with SOA preset", iPresetId);
  EXPECT_GT(iId, 0);

  auto oRow = _repo->findById(iId);
  ASSERT_TRUE(oRow.has_value());
  ASSERT_TRUE(oRow->oSoaPresetId.has_value());
  EXPECT_EQ(*oRow->oSoaPresetId, iPresetId);
}

TEST_F(ZoneTemplateRepositoryTest, SetAndGetSnippets) {
  int64_t iSnipId = _snipRepo->create("snip-one", "First snippet");
  int64_t iTmplId = _repo->create("snip-template", "Template with snippets", std::nullopt);

  _repo->setSnippets(iTmplId, {iSnipId});

  auto oRow = _repo->findById(iTmplId);
  ASSERT_TRUE(oRow.has_value());
  ASSERT_EQ(oRow->vSnippetIds.size(), 1u);
  EXPECT_EQ(oRow->vSnippetIds[0], iSnipId);
}

TEST_F(ZoneTemplateRepositoryTest, SnippetOrderPreserved) {
  int64_t iSnipId1 = _snipRepo->create("snip-alpha", "Snippet alpha");
  int64_t iSnipId2 = _snipRepo->create("snip-beta", "Snippet beta");
  int64_t iTmplId  = _repo->create("order-template", "Order test template", std::nullopt);

  // Intentionally pass id2 before id1 to verify order is preserved
  _repo->setSnippets(iTmplId, {iSnipId2, iSnipId1});

  auto oRow = _repo->findById(iTmplId);
  ASSERT_TRUE(oRow.has_value());
  ASSERT_EQ(oRow->vSnippetIds.size(), 2u);
  EXPECT_EQ(oRow->vSnippetIds[0], iSnipId2);
  EXPECT_EQ(oRow->vSnippetIds[1], iSnipId1);
}

TEST_F(ZoneTemplateRepositoryTest, DuplicateNameThrows) {
  _repo->create("dup-template", "First", std::nullopt);
  EXPECT_THROW(
      _repo->create("dup-template", "Second", std::nullopt),
      dns::common::ConflictError);
}

TEST_F(ZoneTemplateRepositoryTest, DeleteById) {
  int64_t iId = _repo->create("temp-template", "Will be deleted", std::nullopt);
  _repo->deleteById(iId);
  EXPECT_FALSE(_repo->findById(iId).has_value());
}
