// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Meridian DNS Contributors
// This file is part of Meridian DNS. See LICENSE for details.

#include "core/DeploymentEngine.hpp"

#include <gtest/gtest.h>

#include <map>
#include <string>
#include <vector>

#include "common/Types.hpp"

using dns::common::DiffAction;
using dns::common::RecordDiff;
using dns::core::DeploymentEngine;

namespace {
RecordDiff makeDiff(DiffAction action, const std::string& sName,
                    const std::string& sType, const std::string& sValue = "",
                    const std::string& sProviderRecordId = "") {
  RecordDiff rd;
  rd.action = action;
  rd.sName = sName;
  rd.sType = sType;
  rd.sSourceValue = (action == DiffAction::Add || action == DiffAction::Update) ? sValue : "";
  rd.sProviderValue = (action == DiffAction::Delete || action == DiffAction::Drift ||
                       action == DiffAction::Update) ? sValue : "";
  rd.sProviderRecordId = sProviderRecordId;
  return rd;
}
}  // namespace

TEST(DeploymentOrderingTest, DeletesBeforeAdds) {
  std::vector<RecordDiff> vDiffs = {
      makeDiff(DiffAction::Add, "www.example.com.", "A", "1.2.3.4"),
      makeDiff(DiffAction::Delete, "www.example.com.", "CNAME", "old.example.com.", "pdns:www:CNAME:old"),
  };
  std::map<std::string, std::string> mDriftActions;
  auto vOrdered = DeploymentEngine::partitionDiffsForExecution(vDiffs, mDriftActions);
  ASSERT_EQ(vOrdered.size(), 2u);
  EXPECT_EQ(vOrdered[0].action, DiffAction::Delete);
  EXPECT_EQ(vOrdered[1].action, DiffAction::Add);
}

TEST(DeploymentOrderingTest, DriftDeleteBeforeAdd) {
  std::vector<RecordDiff> vDiffs = {
      makeDiff(DiffAction::Add, "www.example.com.", "A", "1.2.3.4"),
      makeDiff(DiffAction::Drift, "www.example.com.", "CNAME", "old.example.com.", "pdns:www:CNAME:old"),
  };
  std::map<std::string, std::string> mDriftActions = {
      {"www.example.com.\tCNAME", "delete"},
  };
  auto vOrdered = DeploymentEngine::partitionDiffsForExecution(vDiffs, mDriftActions);
  ASSERT_EQ(vOrdered.size(), 2u);
  EXPECT_EQ(vOrdered[0].action, DiffAction::Drift);
  EXPECT_EQ(vOrdered[1].action, DiffAction::Add);
}

TEST(DeploymentOrderingTest, DriftAdoptAfterAdd) {
  std::vector<RecordDiff> vDiffs = {
      makeDiff(DiffAction::Add, "new.example.com.", "A", "1.2.3.4"),
      makeDiff(DiffAction::Drift, "extra.example.com.", "TXT", "some-value", "pdns:extra:TXT:some"),
  };
  std::map<std::string, std::string> mDriftActions = {
      {"extra.example.com.\tTXT", "adopt"},
  };
  auto vOrdered = DeploymentEngine::partitionDiffsForExecution(vDiffs, mDriftActions);
  ASSERT_EQ(vOrdered.size(), 2u);
  EXPECT_EQ(vOrdered[0].action, DiffAction::Add);
  EXPECT_EQ(vOrdered[1].action, DiffAction::Drift);
}

TEST(DeploymentOrderingTest, FullOrderingDeleteUpdateAddDrift) {
  std::vector<RecordDiff> vDiffs = {
      makeDiff(DiffAction::Add, "new.example.com.", "A", "1.2.3.4"),
      makeDiff(DiffAction::Drift, "drift.example.com.", "CNAME", "old.target.", "pdns:drift:CNAME:old"),
      makeDiff(DiffAction::Update, "existing.example.com.", "A", "5.6.7.8"),
      makeDiff(DiffAction::Delete, "gone.example.com.", "MX", "mail.example.com.", "pdns:gone:MX:mail"),
  };
  std::map<std::string, std::string> mDriftActions = {
      {"drift.example.com.\tCNAME", "delete"},
  };
  auto vOrdered = DeploymentEngine::partitionDiffsForExecution(vDiffs, mDriftActions);
  ASSERT_EQ(vOrdered.size(), 4u);
  EXPECT_EQ(vOrdered[0].action, DiffAction::Delete);
  EXPECT_EQ(vOrdered[1].action, DiffAction::Drift);
  EXPECT_EQ(vOrdered[2].action, DiffAction::Update);
  EXPECT_EQ(vOrdered[3].action, DiffAction::Add);
}

TEST(DeploymentOrderingTest, EmptyDiffs) {
  std::vector<RecordDiff> vDiffs;
  std::map<std::string, std::string> mDriftActions;
  auto vOrdered = DeploymentEngine::partitionDiffsForExecution(vDiffs, mDriftActions);
  EXPECT_TRUE(vOrdered.empty());
}

TEST(DeploymentOrderingTest, DriftIgnoreDoesNotMoveToDeletes) {
  std::vector<RecordDiff> vDiffs = {
      makeDiff(DiffAction::Add, "new.example.com.", "A", "1.2.3.4"),
      makeDiff(DiffAction::Drift, "extra.example.com.", "TXT", "some-value", "pdns:extra:TXT:some"),
  };
  std::map<std::string, std::string> mDriftActions = {
      {"extra.example.com.\tTXT", "ignore"},
  };
  auto vOrdered = DeploymentEngine::partitionDiffsForExecution(vDiffs, mDriftActions);
  ASSERT_EQ(vOrdered.size(), 2u);
  EXPECT_EQ(vOrdered[0].action, DiffAction::Add);
  EXPECT_EQ(vOrdered[1].action, DiffAction::Drift);
}
