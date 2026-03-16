// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Meridian DNS Contributors
// This file is part of Meridian DNS. See LICENSE for details.

#include "api/routes/SettingsRoutes.hpp"
#include "api/RouteHelpers.hpp"
#include "common/Config.hpp"
#include "common/SettingsDef.hpp"
#include "dal/ConnectionPool.hpp"
#include "dal/SettingsRepository.hpp"
#include "common/Logger.hpp"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <cstdlib>
#include <memory>
#include <string>

namespace {

std::string getDbUrl() {
  const char* p = std::getenv("DNS_DB_URL");
  return p ? std::string(p) : "";
}

}  // namespace

class SettingsRoutesTest : public ::testing::Test {
 protected:
  void SetUp() override {
    _sDbUrl = getDbUrl();
    if (_sDbUrl.empty()) {
      GTEST_SKIP() << "DNS_DB_URL not set — skipping DB integration test";
    }
    dns::common::Logger::init("warn");
    _cpPool = std::make_unique<dns::dal::ConnectionPool>(_sDbUrl, 2);
    _srRepo = std::make_unique<dns::dal::SettingsRepository>(*_cpPool);

    // Seed settings so GET returns data
    dns::common::Config::seedToDb(*_srRepo);
  }

  std::string _sDbUrl;
  std::unique_ptr<dns::dal::ConnectionPool> _cpPool;
  std::unique_ptr<dns::dal::SettingsRepository> _srRepo;
};

TEST_F(SettingsRoutesTest, ListAll_ReturnsAllConfigurableSettings) {
  auto vRows = _srRepo->listAll();

  // Filter to only known config settings (same logic as GET endpoint)
  int iConfigKeys = 0;
  for (const auto& row : vRows) {
    for (const auto& def : dns::common::kSettings) {
      if (def.sKey == row.sKey) {
        ++iConfigKeys;
        break;
      }
    }
  }
  EXPECT_EQ(iConfigKeys, static_cast<int>(dns::common::kSettings.size()));
}

TEST_F(SettingsRoutesTest, Upsert_UpdatesExistingSetting) {
  _srRepo->upsert("deployment.retention_count", "99");
  auto oRow = _srRepo->findByKey("deployment.retention_count");
  ASSERT_TRUE(oRow.has_value());
  EXPECT_EQ(oRow->sValue, "99");
}

TEST_F(SettingsRoutesTest, ValidateKnownSettingKey) {
  bool bKnown = false;
  for (const auto& def : dns::common::kSettings) {
    if (def.sKey == "deployment.retention_count") {
      bKnown = true;
      break;
    }
  }
  EXPECT_TRUE(bKnown);

  bKnown = false;
  for (const auto& def : dns::common::kSettings) {
    if (def.sKey == "bogus.setting") {
      bKnown = true;
      break;
    }
  }
  EXPECT_FALSE(bKnown);
}
