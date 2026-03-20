// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Meridian DNS Contributors
// This file is part of Meridian DNS. See LICENSE for details.

#include "dal/TagRepository.hpp"

#include "common/Errors.hpp"
#include "common/Logger.hpp"
#include "dal/ConnectionPool.hpp"
#include "dal/ViewRepository.hpp"
#include "dal/ZoneRepository.hpp"

#include <gtest/gtest.h>

#include <cstdlib>
#include <string>

using dns::dal::ConnectionPool;
using dns::dal::TagRepository;
using dns::dal::TagRow;
using dns::dal::ViewRepository;
using dns::dal::ZoneRepository;

namespace {

std::string getDbUrl() {
  const char* pUrl = std::getenv("DNS_DB_URL");
  return pUrl ? std::string(pUrl) : std::string{};
}

}  // namespace

class TagRepositoryTest : public ::testing::Test {
 protected:
  void SetUp() override {
    _sDbUrl = getDbUrl();
    if (_sDbUrl.empty()) {
      GTEST_SKIP() << "DNS_DB_URL not set — skipping integration test";
    }
    dns::common::Logger::init("warn");
    _cpPool  = std::make_unique<ConnectionPool>(_sDbUrl, 2);
    _vrRepo  = std::make_unique<ViewRepository>(*_cpPool);
    _zrRepo  = std::make_unique<ZoneRepository>(*_cpPool);
    _trRepo  = std::make_unique<TagRepository>(*_cpPool);

    // Clean test data
    auto cg = _cpPool->checkout();
    pqxx::work txn(*cg);
    txn.exec("DELETE FROM records");
    txn.exec("DELETE FROM variables");
    txn.exec("DELETE FROM deployments");
    txn.exec("DELETE FROM zones");
    txn.exec("DELETE FROM view_providers");
    txn.exec("DELETE FROM views");
    txn.exec("DELETE FROM tags");
    txn.commit();

    _iViewId = _vrRepo->create("tag-test-view", "For tag tests");
  }

  std::string _sDbUrl;
  std::unique_ptr<ConnectionPool> _cpPool;
  std::unique_ptr<ViewRepository> _vrRepo;
  std::unique_ptr<ZoneRepository> _zrRepo;
  std::unique_ptr<TagRepository> _trRepo;
  int64_t _iViewId = 0;
};

TEST_F(TagRepositoryTest, UpsertVocabulary_InsertsNewTags) {
  _trRepo->upsertVocabulary({"prod", "staging"});
  auto vRows = _trRepo->listWithCounts();
  ASSERT_EQ(vRows.size(), 2u);
  EXPECT_EQ(vRows[0].sName, "prod");
  EXPECT_EQ(vRows[1].sName, "staging");
}

TEST_F(TagRepositoryTest, UpsertVocabulary_IgnoresDuplicates) {
  _trRepo->upsertVocabulary({"prod"});
  _trRepo->upsertVocabulary({"prod", "staging"});
  auto vRows = _trRepo->listWithCounts();
  EXPECT_EQ(vRows.size(), 2u);
}

TEST_F(TagRepositoryTest, ListWithCounts_ReturnsZoneCount) {
  _trRepo->upsertVocabulary({"prod"});
  int64_t iZoneId = _zrRepo->create("example.com", _iViewId, std::nullopt);
  _zrRepo->updateTags(iZoneId, {"prod"});

  auto vRows = _trRepo->listWithCounts();
  ASSERT_EQ(vRows.size(), 1u);
  EXPECT_EQ(vRows[0].sName, "prod");
  EXPECT_EQ(vRows[0].iZoneCount, 1);
}

TEST_F(TagRepositoryTest, Rename_UpdatesTagAndZones) {
  _trRepo->upsertVocabulary({"old-name"});
  int64_t iZoneId = _zrRepo->create("example.com", _iViewId, std::nullopt);
  _zrRepo->updateTags(iZoneId, {"old-name"});

  auto vRows = _trRepo->listWithCounts();
  ASSERT_EQ(vRows.size(), 1u);
  int64_t iTagId = vRows[0].iId;

  _trRepo->rename(iTagId, "new-name");

  // Tag vocabulary updated
  auto oTag = _trRepo->findById(iTagId);
  ASSERT_TRUE(oTag.has_value());
  EXPECT_EQ(oTag->sName, "new-name");

  // Zone's tags array updated
  auto oZone = _zrRepo->findById(iZoneId);
  ASSERT_TRUE(oZone.has_value());
  ASSERT_EQ(oZone->vTags.size(), 1u);
  EXPECT_EQ(oZone->vTags[0], "new-name");
}

TEST_F(TagRepositoryTest, Rename_ThrowsNotFoundOnMissingId) {
  EXPECT_THROW(_trRepo->rename(999999999, "x"), dns::common::NotFoundError);
}

TEST_F(TagRepositoryTest, DeleteTag_RemovesFromZones) {
  _trRepo->upsertVocabulary({"removeme"});
  int64_t iZoneId = _zrRepo->create("example.com", _iViewId, std::nullopt);
  _zrRepo->updateTags(iZoneId, {"removeme"});

  auto vRows = _trRepo->listWithCounts();
  ASSERT_EQ(vRows.size(), 1u);
  _trRepo->deleteTag(vRows[0].iId);

  // Tag vocabulary empty
  EXPECT_TRUE(_trRepo->listWithCounts().empty());

  // Zone tags cleared
  auto oZone = _zrRepo->findById(iZoneId);
  ASSERT_TRUE(oZone.has_value());
  EXPECT_TRUE(oZone->vTags.empty());
}

TEST_F(TagRepositoryTest, DeleteTag_ThrowsNotFoundOnMissingId) {
  EXPECT_THROW(_trRepo->deleteTag(999999999), dns::common::NotFoundError);
}
