// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Meridian DNS Contributors
// This file is part of Meridian DNS. See LICENSE for details.
#include "dal/SnippetRepository.hpp"
#include "common/Errors.hpp"
#include "common/Logger.hpp"
#include "dal/ConnectionPool.hpp"
#include <gtest/gtest.h>
#include <cstdlib>
#include <pqxx/pqxx>

using dns::dal::ConnectionPool;
using dns::dal::SnippetRepository;
using dns::dal::SnippetRow;
using dns::dal::SnippetRecordRow;

namespace {
std::string getDbUrl() {
  const char* p = std::getenv("DNS_DB_URL");
  return p ? std::string(p) : std::string{};
}
}  // namespace

class SnippetRepositoryTest : public ::testing::Test {
 protected:
  void SetUp() override {
    _sDbUrl = getDbUrl();
    if (_sDbUrl.empty()) GTEST_SKIP() << "DNS_DB_URL not set — skipping integration test";
    dns::common::Logger::init("warn");
    _cpPool = std::make_unique<ConnectionPool>(_sDbUrl, 2);
    _repo   = std::make_unique<SnippetRepository>(*_cpPool);
    auto cg = _cpPool->checkout();
    pqxx::work txn(*cg);
    txn.exec("DELETE FROM zone_template_snippets");
    txn.exec("DELETE FROM snippet_records");
    txn.exec("DELETE FROM snippets");
    txn.commit();
  }
  std::string _sDbUrl;
  std::unique_ptr<ConnectionPool> _cpPool;
  std::unique_ptr<SnippetRepository> _repo;
};

TEST_F(SnippetRepositoryTest, CreateAndFindById) {
  int64_t iId = _repo->create("mail-records", "MX and SPF for mail");
  EXPECT_GT(iId, 0);
  auto oRow = _repo->findById(iId);
  ASSERT_TRUE(oRow.has_value());
  EXPECT_EQ(oRow->sName, "mail-records");
  EXPECT_EQ(oRow->sDescription, "MX and SPF for mail");
  EXPECT_TRUE(oRow->vRecords.empty());
}

TEST_F(SnippetRepositoryTest, ReplaceAndListRecords) {
  int64_t iId = _repo->create("web", "Web records");
  std::vector<SnippetRecordRow> vRecords;
  {
    SnippetRecordRow r;
    r.sName = "www"; r.sType = "CNAME"; r.iTtl = 300;
    r.sValueTemplate = "example.com."; r.iPriority = 0; r.iSortOrder = 0;
    vRecords.push_back(r);
  }
  {
    SnippetRecordRow r;
    r.sName = "@"; r.sType = "A"; r.iTtl = 60;
    r.sValueTemplate = "1.2.3.4"; r.iPriority = 0; r.iSortOrder = 1;
    vRecords.push_back(r);
  }
  _repo->replaceRecords(iId, vRecords);
  auto oRow = _repo->findById(iId);
  ASSERT_TRUE(oRow.has_value());
  ASSERT_EQ(oRow->vRecords.size(), 2u);
  EXPECT_EQ(oRow->vRecords[0].sName, "www");
  EXPECT_EQ(oRow->vRecords[1].sName, "@");
}

TEST_F(SnippetRepositoryTest, DuplicateNameThrows) {
  _repo->create("dup", "");
  EXPECT_THROW(_repo->create("dup", ""), dns::common::ConflictError);
}

TEST_F(SnippetRepositoryTest, UpdateSnippet) {
  int64_t iId = _repo->create("original", "desc");
  _repo->update(iId, "renamed", "new desc");
  auto oRow = _repo->findById(iId);
  ASSERT_TRUE(oRow.has_value());
  EXPECT_EQ(oRow->sName, "renamed");
  EXPECT_EQ(oRow->sDescription, "new desc");
}

TEST_F(SnippetRepositoryTest, DeleteById) {
  int64_t iId = _repo->create("temp", "");
  _repo->deleteById(iId);
  EXPECT_FALSE(_repo->findById(iId).has_value());
}

TEST_F(SnippetRepositoryTest, ListAll) {
  _repo->create("alpha", "");
  _repo->create("beta", "");
  auto vRows = _repo->listAll();
  EXPECT_GE(vRows.size(), 2u);
}
