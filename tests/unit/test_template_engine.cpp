// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Meridian DNS Contributors
// This file is part of Meridian DNS. See LICENSE for details.
#include "core/TemplateEngine.hpp"
#include "common/Types.hpp"
#include <gtest/gtest.h>

using dns::core::TemplateEngine;
using dns::common::DiffAction;
using dns::dal::RecordRow;
using dns::dal::SnippetRecordRow;

namespace {

SnippetRecordRow makeExpected(const std::string& sName, const std::string& sType,
                               int iTtl, const std::string& sValue, int iPriority = 0) {
  SnippetRecordRow r;
  r.sName = sName; r.sType = sType; r.iTtl = iTtl;
  r.sValueTemplate = sValue; r.iPriority = iPriority;
  return r;
}

RecordRow makeZoneRecord(int64_t iId, const std::string& sName, const std::string& sType,
                          int iTtl, const std::string& sValue, int iPriority = 0) {
  RecordRow r;
  r.iId = iId; r.sName = sName; r.sType = sType;
  r.iTtl = iTtl; r.sValueTemplate = sValue; r.iPriority = iPriority;
  return r;
}

}  // namespace

TEST(TemplateEngineTest, MissingRecordProducesAddDiff) {
  auto result = TemplateEngine::computeComplianceDiff(
      1, "test.com",
      {makeExpected("www", "CNAME", 300, "example.com.")},
      {});
  ASSERT_EQ(result.vDiffs.size(), 1u);
  EXPECT_EQ(result.vDiffs[0].action, DiffAction::Add);
  EXPECT_EQ(result.vDiffs[0].sName, "www");
  EXPECT_EQ(result.vDiffs[0].sType, "CNAME");
  EXPECT_EQ(result.vDiffs[0].sSourceValue, "example.com.");
}

TEST(TemplateEngineTest, DifferentValueProducesUpdateDiff) {
  auto result = TemplateEngine::computeComplianceDiff(
      1, "test.com",
      {makeExpected("@", "A", 60, "1.2.3.4")},
      {makeZoneRecord(1, "@", "A", 60, "9.9.9.9")});
  ASSERT_EQ(result.vDiffs.size(), 1u);
  EXPECT_EQ(result.vDiffs[0].action, DiffAction::Update);
  EXPECT_EQ(result.vDiffs[0].sSourceValue, "1.2.3.4");
  EXPECT_EQ(result.vDiffs[0].sProviderValue, "9.9.9.9");
}

TEST(TemplateEngineTest, DifferentTtlProducesUpdateDiff) {
  auto result = TemplateEngine::computeComplianceDiff(
      1, "test.com",
      {makeExpected("@", "A", 300, "1.2.3.4")},
      {makeZoneRecord(1, "@", "A", 60, "1.2.3.4")});
  ASSERT_EQ(result.vDiffs.size(), 1u);
  EXPECT_EQ(result.vDiffs[0].action, DiffAction::Update);
}

TEST(TemplateEngineTest, DifferentPriorityProducesUpdateDiff) {
  auto result = TemplateEngine::computeComplianceDiff(
      1, "test.com",
      {makeExpected("@", "MX", 300, "mail.example.com.", 10)},
      {makeZoneRecord(1, "@", "MX", 300, "mail.example.com.", 20)});
  ASSERT_EQ(result.vDiffs.size(), 1u);
  EXPECT_EQ(result.vDiffs[0].action, DiffAction::Update);
}

TEST(TemplateEngineTest, MatchingRecordProducesNoDiff) {
  auto result = TemplateEngine::computeComplianceDiff(
      1, "test.com",
      {makeExpected("@", "A", 300, "1.2.3.4")},
      {makeZoneRecord(1, "@", "A", 300, "1.2.3.4")});
  EXPECT_TRUE(result.vDiffs.empty());
}

TEST(TemplateEngineTest, ExtraZoneRecordsNotFlagged) {
  auto result = TemplateEngine::computeComplianceDiff(
      1, "test.com",
      {makeExpected("@", "A", 300, "1.2.3.4")},
      {makeZoneRecord(1, "@", "A", 300, "1.2.3.4"),
       makeZoneRecord(2, "mail", "MX", 300, "10 mx.example.com.")});  // extra
  EXPECT_TRUE(result.vDiffs.empty());
}

TEST(TemplateEngineTest, MultipleExpectedCheckedIndependently) {
  auto result = TemplateEngine::computeComplianceDiff(
      1, "test.com",
      {makeExpected("www",  "CNAME", 300, "example.com."),
       makeExpected("mail", "CNAME", 300, "mail.example.com.")},
      {makeZoneRecord(1, "www", "CNAME", 300, "example.com.")});  // mail missing
  ASSERT_EQ(result.vDiffs.size(), 1u);
  EXPECT_EQ(result.vDiffs[0].sName, "mail");
  EXPECT_EQ(result.vDiffs[0].action, DiffAction::Add);
}
