// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Meridian DNS Contributors
// This file is part of Meridian DNS. See LICENSE for details.

#include "core/RecordValidator.hpp"
#include "dal/RecordRepository.hpp"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

using dns::common::ValidationWarning;
using dns::dal::RecordRow;
using testing::Return;

// MockRecordRepository: subclass with virtual listByZoneId mocked.
// The base RecordRepository ctor takes a ConnectionPool&; we pass a nullptr cast
// since the mock never calls the real implementation.
class MockRecordRepository : public dns::dal::RecordRepository {
 public:
  MockRecordRepository()
      : dns::dal::RecordRepository(*reinterpret_cast<dns::dal::ConnectionPool*>(1)) {}

  MOCK_METHOD(std::vector<RecordRow>, listByZoneId, (int64_t), (override));
};

namespace {

RecordRow makeRow(int64_t iId, const std::string& sName, const std::string& sType,
                  bool bPendingDelete = false) {
  RecordRow r;
  r.iId = iId;
  r.sName = sName;
  r.sType = sType;
  r.bPendingDelete = bPendingDelete;
  return r;
}

}  // namespace

class RecordValidatorTest : public ::testing::Test {
 protected:
  MockRecordRepository _mockRepo;
};

TEST_F(RecordValidatorTest, NoWarnings_ForSimpleARecord) {
  EXPECT_CALL(_mockRepo, listByZoneId(1)).WillOnce(Return(std::vector<RecordRow>{}));

  dns::core::RecordValidator rv(_mockRepo);
  auto vW = rv.validate(1, "www", "A", "1.2.3.4");
  EXPECT_TRUE(vW.empty());
}

TEST_F(RecordValidatorTest, CnameCoexistence_ErrorWhenOtherTypeExists) {
  EXPECT_CALL(_mockRepo, listByZoneId(1))
      .WillOnce(Return(std::vector<RecordRow>{makeRow(10, "www", "A")}));

  dns::core::RecordValidator rv(_mockRepo);
  auto vW = rv.validate(1, "www", "CNAME", "example.com.");
  ASSERT_FALSE(vW.empty());
  bool bFound = false;
  for (const auto& w : vW) {
    if (w.sCode == "CNAME_COEXISTENCE" && w.sSeverity == "error") bFound = true;
  }
  EXPECT_TRUE(bFound);
}

TEST_F(RecordValidatorTest, CnameCoexistence_ErrorWhenAddingNonCnameNextToCname) {
  EXPECT_CALL(_mockRepo, listByZoneId(1))
      .WillOnce(Return(std::vector<RecordRow>{makeRow(10, "www", "CNAME")}));

  dns::core::RecordValidator rv(_mockRepo);
  auto vW = rv.validate(1, "www", "A", "1.2.3.4");
  ASSERT_FALSE(vW.empty());
  EXPECT_EQ(vW[0].sCode, "CNAME_COEXISTENCE");
  EXPECT_EQ(vW[0].sSeverity, "error");
}

TEST_F(RecordValidatorTest, MultipleCnames_ErrorWhenCnameAlreadyExists) {
  EXPECT_CALL(_mockRepo, listByZoneId(1))
      .WillOnce(Return(std::vector<RecordRow>{makeRow(10, "www", "CNAME")}));

  dns::core::RecordValidator rv(_mockRepo);
  auto vW = rv.validate(1, "www", "CNAME", "example.com.");
  bool bFound = false;
  for (const auto& w : vW) {
    if (w.sCode == "MULTIPLE_CNAMES" && w.sSeverity == "error") bFound = true;
  }
  EXPECT_TRUE(bFound);
}

TEST_F(RecordValidatorTest, MissingTrailingDot_WarnForCnameWithDotValue) {
  EXPECT_CALL(_mockRepo, listByZoneId(1)).WillOnce(Return(std::vector<RecordRow>{}));

  dns::core::RecordValidator rv(_mockRepo);
  auto vW = rv.validate(1, "alias", "CNAME", "foo.example.com");
  bool bFound = false;
  for (const auto& w : vW) {
    if (w.sCode == "MISSING_TRAILING_DOT" && w.sSeverity == "warning") bFound = true;
  }
  EXPECT_TRUE(bFound);
}

TEST_F(RecordValidatorTest, NoWarning_WhenCnameValueHasTrailingDot) {
  EXPECT_CALL(_mockRepo, listByZoneId(1)).WillOnce(Return(std::vector<RecordRow>{}));

  dns::core::RecordValidator rv(_mockRepo);
  auto vW = rv.validate(1, "alias", "CNAME", "foo.example.com.");
  bool bTrailingDotWarning = false;
  for (const auto& w : vW) {
    if (w.sCode == "MISSING_TRAILING_DOT") bTrailingDotWarning = true;
  }
  EXPECT_FALSE(bTrailingDotWarning);
}

TEST_F(RecordValidatorTest, NoWarning_ForRelativeCnameLabel) {
  EXPECT_CALL(_mockRepo, listByZoneId(1)).WillOnce(Return(std::vector<RecordRow>{}));

  dns::core::RecordValidator rv(_mockRepo);
  // "www" has no dot — should not trigger trailing dot warning
  auto vW = rv.validate(1, "alias", "CNAME", "www");
  bool bTrailingDotWarning = false;
  for (const auto& w : vW) {
    if (w.sCode == "MISSING_TRAILING_DOT") bTrailingDotWarning = true;
  }
  EXPECT_FALSE(bTrailingDotWarning);
}

TEST_F(RecordValidatorTest, NoWarning_MxWithTrailingDot) {
  EXPECT_CALL(_mockRepo, listByZoneId(1)).WillOnce(Return(std::vector<RecordRow>{}));

  dns::core::RecordValidator rv(_mockRepo);
  auto vW = rv.validate(1, "@", "MX", "mail.example.com.");
  bool bTrailingDotWarning = false;
  for (const auto& w : vW) {
    if (w.sCode == "MISSING_TRAILING_DOT") bTrailingDotWarning = true;
  }
  EXPECT_FALSE(bTrailingDotWarning);
}

TEST_F(RecordValidatorTest, MultipleSoaWarningWhenSoaExists) {
  EXPECT_CALL(_mockRepo, listByZoneId(1))
      .WillOnce(Return(std::vector<RecordRow>{makeRow(5, "@", "SOA")}));

  dns::core::RecordValidator rv(_mockRepo);
  auto vW = rv.validate(1, "@", "SOA", "ns1.example.com. hostmaster.example.com. 1 3600 900 604800 300");
  bool bFound = false;
  for (const auto& w : vW) {
    if (w.sCode == "MULTIPLE_SOA" && w.sSeverity == "warning") bFound = true;
  }
  EXPECT_TRUE(bFound);
}
