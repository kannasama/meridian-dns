// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Meridian DNS Contributors
// This file is part of Meridian DNS. See LICENSE for details.

#include "providers/AdGuardHomeProvider.hpp"

#include <gtest/gtest.h>
#include <cstdlib>
#include <string>

#include "common/Types.hpp"

using dns::common::DnsRecord;
using dns::providers::AdGuardHomeProvider;

// Integration tests require a live AdGuard Home instance.
// Set DNS_AGH_URL=http://host:port and DNS_AGH_TOKEN={"username":"...","password":"..."}
// to run these tests; they are skipped when the env var is absent.

static std::string getAghUrl() {
  const char* p = std::getenv("DNS_AGH_URL");
  return p ? p : "";
}

static std::string getAghToken() {
  const char* p = std::getenv("DNS_AGH_TOKEN");
  return p ? p : R"({"username":"admin","password":"password"})";
}

class AdGuardIntegrationTest : public ::testing::Test {
 protected:
  void SetUp() override {
    if (getAghUrl().empty()) {
      GTEST_SKIP() << "DNS_AGH_URL not set — skipping AdGuard Home integration tests";
    }
  }
};

TEST_F(AdGuardIntegrationTest, CrudCycle) {
  AdGuardHomeProvider provider{getAghUrl(), getAghToken()};

  // Create
  DnsRecord drCreate;
  drCreate.sName = "meridian-test.example.com.";
  drCreate.sType = "A";
  drCreate.sValue = "192.168.99.99";
  auto createResult = provider.createRecord("example.com", drCreate);
  ASSERT_TRUE(createResult.bSuccess) << createResult.sErrorMessage;
  EXPECT_FALSE(createResult.sProviderRecordId.empty());

  // List — record should be present
  auto vRecords = provider.listRecords("example.com");
  bool bFound = false;
  for (const auto& dr : vRecords) {
    if (dr.sName == "meridian-test.example.com." && dr.sValue == "192.168.99.99") {
      bFound = true;
      break;
    }
  }
  EXPECT_TRUE(bFound) << "Created record not found in list";

  // Update (delete-then-add)
  DnsRecord drUpdate;
  drUpdate.sName = "meridian-test.example.com.";
  drUpdate.sType = "A";
  drUpdate.sValue = "192.168.99.100";
  drUpdate.sProviderRecordId = createResult.sProviderRecordId;
  auto updateResult = provider.updateRecord("example.com", drUpdate);
  ASSERT_TRUE(updateResult.bSuccess) << updateResult.sErrorMessage;

  // Delete
  auto delResult = provider.deleteRecord("example.com", updateResult.sProviderRecordId);
  ASSERT_TRUE(delResult.bSuccess) << delResult.sErrorMessage;

  // List — record should be gone
  vRecords = provider.listRecords("example.com");
  for (const auto& dr : vRecords) {
    EXPECT_FALSE(dr.sName == "meridian-test.example.com." && dr.sValue == "192.168.99.100")
        << "Deleted record still present in list";
  }
}

TEST_F(AdGuardIntegrationTest, TypeInferenceRoundTrip) {
  AdGuardHomeProvider provider{getAghUrl(), getAghToken()};

  DnsRecord drCreate;
  drCreate.sName = "meridian-type-test.example.com.";
  drCreate.sType = "A";
  drCreate.sValue = "10.20.30.40";
  auto createResult = provider.createRecord("example.com", drCreate);
  ASSERT_TRUE(createResult.bSuccess) << createResult.sErrorMessage;

  auto vRecords = provider.listRecords("example.com");
  bool bFoundA = false;
  for (const auto& dr : vRecords) {
    if (dr.sName == "meridian-type-test.example.com." && dr.sValue == "10.20.30.40") {
      EXPECT_EQ(dr.sType, "A");
      bFoundA = true;
      break;
    }
  }
  EXPECT_TRUE(bFoundA);

  provider.deleteRecord("example.com", createResult.sProviderRecordId);
}

TEST_F(AdGuardIntegrationTest, ConnectivityOk) {
  AdGuardHomeProvider provider{getAghUrl(), getAghToken()};
  auto status = provider.testConnectivity();
  EXPECT_EQ(status, dns::common::HealthStatus::Ok);
}
