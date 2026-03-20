// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Meridian DNS Contributors
// This file is part of Meridian DNS. See LICENSE for details.

#include "dal/RecordRepository.hpp"

#include "common/Errors.hpp"
#include "common/Logger.hpp"
#include "dal/ConnectionPool.hpp"
#include "dal/ViewRepository.hpp"
#include "dal/ZoneRepository.hpp"

#include <gtest/gtest.h>

#include <cstdlib>
#include <string>

using dns::dal::ConnectionPool;
using dns::dal::RecordRepository;
using dns::dal::RecordRow;
using dns::dal::ViewRepository;
using dns::dal::ZoneRepository;

namespace {

std::string getDbUrl() {
  const char* pUrl = std::getenv("DNS_DB_URL");
  return pUrl ? std::string(pUrl) : std::string{};
}

}  // namespace

class RecordRepositoryTest : public ::testing::Test {
 protected:
  void SetUp() override {
    _sDbUrl = getDbUrl();
    if (_sDbUrl.empty()) {
      GTEST_SKIP() << "DNS_DB_URL not set — skipping integration test";
    }
    dns::common::Logger::init("warn");
    _cpPool = std::make_unique<ConnectionPool>(_sDbUrl, 2);
    _vrRepo = std::make_unique<ViewRepository>(*_cpPool);
    _zrRepo = std::make_unique<ZoneRepository>(*_cpPool);
    _rrRepo = std::make_unique<RecordRepository>(*_cpPool);

    auto cg = _cpPool->checkout();
    pqxx::work txn(*cg);
    txn.exec("DELETE FROM records");
    txn.exec("DELETE FROM variables");
    txn.exec("DELETE FROM deployments");
    txn.exec("DELETE FROM zones");
    txn.exec("DELETE FROM view_providers");
    txn.exec("DELETE FROM views");
    txn.commit();

    _iViewId = _vrRepo->create("rec-view", "For record tests");
    _iZoneId = _zrRepo->create("example.com", _iViewId, std::nullopt);
  }

  std::string _sDbUrl;
  std::unique_ptr<ConnectionPool> _cpPool;
  std::unique_ptr<ViewRepository> _vrRepo;
  std::unique_ptr<ZoneRepository> _zrRepo;
  std::unique_ptr<RecordRepository> _rrRepo;
  int64_t _iViewId = 0;
  int64_t _iZoneId = 0;
};

TEST_F(RecordRepositoryTest, CreateAndFindById) {
  int64_t iId = _rrRepo->create(_iZoneId, "www", "A", 300, "{{LB_VIP}}", 0);
  EXPECT_GT(iId, 0);

  auto oRow = _rrRepo->findById(iId);
  ASSERT_TRUE(oRow.has_value());
  EXPECT_EQ(oRow->sName, "www");
  EXPECT_EQ(oRow->sType, "A");
  EXPECT_EQ(oRow->iTtl, 300);
  EXPECT_EQ(oRow->sValueTemplate, "{{LB_VIP}}");
  EXPECT_EQ(oRow->iPriority, 0);
}

TEST_F(RecordRepositoryTest, ListByZoneId) {
  int64_t iZone2 = _zrRepo->create("other.com", _iViewId, std::nullopt);
  _rrRepo->create(_iZoneId, "www", "A", 300, "1.2.3.4", 0);
  _rrRepo->create(_iZoneId, "mail", "MX", 300, "mail.example.com", 10);
  _rrRepo->create(iZone2, "ns1", "NS", 300, "ns1.other.com", 0);

  auto vRows = _rrRepo->listByZoneId(_iZoneId);
  EXPECT_EQ(vRows.size(), 2u);
}

TEST_F(RecordRepositoryTest, Update) {
  int64_t iId = _rrRepo->create(_iZoneId, "www", "A", 300, "1.2.3.4", 0);

  _rrRepo->update(iId, "www", "A", 600, "5.6.7.8", 0);

  auto oRow = _rrRepo->findById(iId);
  ASSERT_TRUE(oRow.has_value());
  EXPECT_EQ(oRow->iTtl, 600);
  EXPECT_EQ(oRow->sValueTemplate, "5.6.7.8");
}

TEST_F(RecordRepositoryTest, DeleteById) {
  int64_t iId = _rrRepo->create(_iZoneId, "del", "A", 300, "1.1.1.1", 0);
  _rrRepo->deleteById(iId);

  // deleteById is a soft-delete: record is marked pending_delete, not removed
  auto oRow = _rrRepo->findById(iId);
  ASSERT_TRUE(oRow.has_value());
  EXPECT_TRUE(oRow->bPendingDelete);
}

TEST_F(RecordRepositoryTest, InvalidZoneIdThrows) {
  EXPECT_THROW(_rrRepo->create(999999, "bad", "A", 300, "1.1.1.1", 0),
               dns::common::ValidationError);
}

TEST_F(RecordRepositoryTest, DeleteAllByZoneId) {
  _rrRepo->create(_iZoneId, "www", "A", 300, "1.2.3.4", 0);
  _rrRepo->create(_iZoneId, "mail", "MX", 300, "mx.example.com.", 10);

  int64_t iZone2 = _zrRepo->create("other.com", _iViewId, std::nullopt);
  _rrRepo->create(iZone2, "ns", "NS", 300, "ns1.other.com.", 0);

  int iDeleted = _rrRepo->deleteAllByZoneId(_iZoneId);
  EXPECT_EQ(iDeleted, 2);

  auto vRows = _rrRepo->listByZoneId(_iZoneId);
  EXPECT_TRUE(vRows.empty());

  // Other zone's records unaffected
  auto vOther = _rrRepo->listByZoneId(iZone2);
  EXPECT_EQ(vOther.size(), 1u);
}

TEST_F(RecordRepositoryTest, UpsertById_UpdateExisting) {
  int64_t iId = _rrRepo->create(_iZoneId, "www", "A", 300, "1.2.3.4", 0);

  int64_t iResult = _rrRepo->upsertById(iId, _iZoneId, "www", "A", 600, "5.6.7.8", 0);
  EXPECT_EQ(iResult, iId);

  auto oRow = _rrRepo->findById(iId);
  ASSERT_TRUE(oRow.has_value());
  EXPECT_EQ(oRow->iTtl, 600);
  EXPECT_EQ(oRow->sValueTemplate, "5.6.7.8");
}

TEST_F(RecordRepositoryTest, UpsertById_InsertNew) {
  // Use a non-existent ID — should insert a new record (ignoring the ID)
  int64_t iResult = _rrRepo->upsertById(999999, _iZoneId, "new", "AAAA", 300, "::1", 0);
  EXPECT_GT(iResult, 0);

  auto oRow = _rrRepo->findById(iResult);
  ASSERT_TRUE(oRow.has_value());
  EXPECT_EQ(oRow->sName, "new");
  EXPECT_EQ(oRow->sType, "AAAA");
  EXPECT_EQ(oRow->sValueTemplate, "::1");
}

class RecordSearchTest : public ::testing::Test {
 protected:
  void SetUp() override {
    _sDbUrl = getDbUrl();
    if (_sDbUrl.empty()) {
      GTEST_SKIP() << "DNS_DB_URL not set — skipping integration test";
    }
    dns::common::Logger::init("warn");
    _cpPool = std::make_unique<ConnectionPool>(_sDbUrl, 2);
    _vrRepo = std::make_unique<ViewRepository>(*_cpPool);
    _zrRepo = std::make_unique<ZoneRepository>(*_cpPool);
    _rrRepo = std::make_unique<RecordRepository>(*_cpPool);

    auto cg = _cpPool->checkout();
    pqxx::work txn(*cg);
    txn.exec("DELETE FROM records");
    txn.exec("DELETE FROM variables");
    txn.exec("DELETE FROM deployments");
    txn.exec("DELETE FROM zones");
    txn.exec("DELETE FROM view_providers");
    txn.exec("DELETE FROM views");
    txn.commit();

    _iViewId  = _vrRepo->create("search-view", "For search tests");
    _iZoneId  = _zrRepo->create("example.com", _iViewId, std::nullopt);
    _iZone2Id = _zrRepo->create("other.net", _iViewId, std::nullopt);
  }

  std::string _sDbUrl;
  std::unique_ptr<ConnectionPool> _cpPool;
  std::unique_ptr<ViewRepository> _vrRepo;
  std::unique_ptr<ZoneRepository> _zrRepo;
  std::unique_ptr<RecordRepository> _rrRepo;
  int64_t _iViewId  = 0;
  int64_t _iZoneId  = 0;
  int64_t _iZone2Id = 0;
};

TEST_F(RecordSearchTest, Search_ByRecordName) {
  _rrRepo->create(_iZoneId, "www", "A", 300, "1.2.3.4", 0);
  _rrRepo->create(_iZoneId, "mail", "MX", 300, "mail.example.com.", 10);

  auto vResults = _rrRepo->search("www", std::nullopt, std::nullopt, std::nullopt);
  ASSERT_EQ(vResults.size(), 1u);
  EXPECT_EQ(vResults[0].sName, "www");
  EXPECT_EQ(vResults[0].sType, "A");
}

TEST_F(RecordSearchTest, Search_ByValueTemplate) {
  _rrRepo->create(_iZoneId, "www", "A", 300, "192.168.1.1", 0);
  _rrRepo->create(_iZoneId, "ftp", "A", 300, "10.0.0.1", 0);

  auto vResults = _rrRepo->search("192.168", std::nullopt, std::nullopt, std::nullopt);
  ASSERT_EQ(vResults.size(), 1u);
  EXPECT_EQ(vResults[0].sName, "www");
}

TEST_F(RecordSearchTest, Search_WithTypeFilter) {
  _rrRepo->create(_iZoneId, "www", "A", 300, "1.2.3.4", 0);
  _rrRepo->create(_iZoneId, "mail", "MX", 300, "mail.example.com.", 10);
  _rrRepo->create(_iZoneId, "v6", "AAAA", 300, "::1", 0);

  auto vResults = _rrRepo->search("", std::make_optional<std::string>("A"),
                                  std::nullopt, std::nullopt);
  EXPECT_EQ(vResults.size(), 1u);
  EXPECT_EQ(vResults[0].sType, "A");
}

TEST_F(RecordSearchTest, Search_ReturnsZoneAndViewName) {
  _rrRepo->create(_iZoneId, "www", "A", 300, "1.2.3.4", 0);

  auto vResults = _rrRepo->search("www", std::nullopt, std::nullopt, std::nullopt);
  ASSERT_EQ(vResults.size(), 1u);
  EXPECT_EQ(vResults[0].sZoneName, "example.com");
  EXPECT_EQ(vResults[0].sViewName, "search-view");
  EXPECT_EQ(vResults[0].iZoneId, _iZoneId);
}

class BulkTtlTest : public ::testing::Test {
 protected:
  void SetUp() override {
    _sDbUrl = getDbUrl();
    if (_sDbUrl.empty()) {
      GTEST_SKIP() << "DNS_DB_URL not set — skipping integration test";
    }
    dns::common::Logger::init("warn");
    _cpPool = std::make_unique<ConnectionPool>(_sDbUrl, 2);
    _vrRepo = std::make_unique<ViewRepository>(*_cpPool);
    _zrRepo = std::make_unique<ZoneRepository>(*_cpPool);
    _rrRepo = std::make_unique<RecordRepository>(*_cpPool);

    auto cg = _cpPool->checkout();
    pqxx::work txn(*cg);
    txn.exec("DELETE FROM records");
    txn.exec("DELETE FROM variables");
    txn.exec("DELETE FROM deployments");
    txn.exec("DELETE FROM zones");
    txn.exec("DELETE FROM view_providers");
    txn.exec("DELETE FROM views");
    txn.commit();

    _iViewId = _vrRepo->create("ttl-view", "For bulk TTL tests");
    _iZoneId = _zrRepo->create("ttl.example.com", _iViewId, std::nullopt);
  }

  std::string _sDbUrl;
  std::unique_ptr<ConnectionPool> _cpPool;
  std::unique_ptr<ViewRepository> _vrRepo;
  std::unique_ptr<ZoneRepository> _zrRepo;
  std::unique_ptr<RecordRepository> _rrRepo;
  int64_t _iViewId = 0;
  int64_t _iZoneId = 0;
};

TEST_F(BulkTtlTest, BulkUpdateTtl_UpdatesAll) {
  _rrRepo->create(_iZoneId, "www", "A", 300, "1.2.3.4", 0);
  _rrRepo->create(_iZoneId, "mail", "MX", 300, "mail.ttl.example.com.", 10);

  int iCount = _rrRepo->bulkUpdateTtl(_iZoneId, 3600, std::nullopt);
  EXPECT_EQ(iCount, 2);

  auto vRows = _rrRepo->listByZoneId(_iZoneId);
  for (const auto& row : vRows) {
    EXPECT_EQ(row.iTtl, 3600);
  }
}

TEST_F(BulkTtlTest, BulkUpdateTtl_FilterByType) {
  _rrRepo->create(_iZoneId, "www", "A", 300, "1.2.3.4", 0);
  _rrRepo->create(_iZoneId, "mail", "MX", 300, "mail.ttl.example.com.", 10);

  int iCount = _rrRepo->bulkUpdateTtl(_iZoneId, 7200,
                                       std::make_optional<std::string>("A"));
  EXPECT_EQ(iCount, 1);

  auto vRows = _rrRepo->listByZoneId(_iZoneId);
  for (const auto& row : vRows) {
    if (row.sType == "A") {
      EXPECT_EQ(row.iTtl, 7200);
    } else {
      EXPECT_EQ(row.iTtl, 300);
    }
  }
}

TEST_F(BulkTtlTest, BulkUpdateTtl_ReturnsAffectedCount) {
  _rrRepo->create(_iZoneId, "www", "A", 300, "1.2.3.4", 0);

  int iCount = _rrRepo->bulkUpdateTtl(_iZoneId, 600, std::nullopt);
  EXPECT_EQ(iCount, 1);
}
