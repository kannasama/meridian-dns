// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Meridian DNS Contributors
// This file is part of Meridian DNS. See LICENSE for details.

#include <gtest/gtest.h>

namespace {

class ZoneCaptureTest : public ::testing::Test {
 protected:
  void SetUp() override {
    const char* pDbUrl = std::getenv("DNS_DB_URL");
    if (!pDbUrl) GTEST_SKIP() << "DNS_DB_URL not set — skipping DB integration tests";
  }
};

TEST_F(ZoneCaptureTest, CaptureCreatesDeploymentRecord) {
  GTEST_SKIP() << "Requires full service wiring — tested via E2E";
}

TEST_F(ZoneCaptureTest, CaptureSnapshotHasGeneratedByField) {
  GTEST_SKIP() << "Requires full service wiring — tested via E2E";
}

TEST_F(ZoneCaptureTest, CaptureAuditsWithCorrectOperation) {
  GTEST_SKIP() << "Requires full service wiring — tested via E2E";
}

TEST_F(ZoneCaptureTest, CaptureSkipsWhenZoneLocked) {
  GTEST_SKIP() << "Requires full service wiring — tested via E2E";
}

}  // namespace
