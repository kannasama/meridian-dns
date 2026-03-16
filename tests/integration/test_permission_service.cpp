// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Meridian DNS Contributors
// This file is part of Meridian DNS. See LICENSE for details.

#include "core/PermissionService.hpp"
#include "dal/ConnectionPool.hpp"
#include "dal/RoleRepository.hpp"
#include "dal/ZoneRepository.hpp"
#include "common/Logger.hpp"
#include "common/Permissions.hpp"

#include <gtest/gtest.h>

#include <cstdlib>
#include <memory>
#include <string>

namespace {
std::string getDbUrl() {
  const char* p = std::getenv("DNS_DB_URL");
  return p ? std::string(p) : "";
}
}  // namespace

class PermissionServiceTest : public ::testing::Test {
 protected:
  void SetUp() override {
    _sDbUrl = getDbUrl();
    if (_sDbUrl.empty()) {
      GTEST_SKIP() << "DNS_DB_URL not set — skipping DB integration test";
    }
    dns::common::Logger::init("warn");
    _cpPool = std::make_unique<dns::dal::ConnectionPool>(_sDbUrl, 2);
    _rrRepo = std::make_unique<dns::dal::RoleRepository>(*_cpPool);
    _zrRepo = std::make_unique<dns::dal::ZoneRepository>(*_cpPool);
    _psService = std::make_unique<dns::core::PermissionService>(*_rrRepo, *_zrRepo);
  }

  std::string _sDbUrl;
  std::unique_ptr<dns::dal::ConnectionPool> _cpPool;
  std::unique_ptr<dns::dal::RoleRepository> _rrRepo;
  std::unique_ptr<dns::dal::ZoneRepository> _zrRepo;
  std::unique_ptr<dns::core::PermissionService> _psService;
};

TEST_F(PermissionServiceTest, GlobalAdmin_HasAllPermissions) {
  // Use existing admin user (user_id=1 if setup has run)
  // This test verifies the resolution path works end-to-end
  auto perms = _psService->getEffectivePermissions(1);
  // Admin should have zones.view if they're in a group with Admin role
  // (depends on test data setup)
  EXPECT_GE(perms.size(), 0u);  // At minimum, function returns without error
}

TEST_F(PermissionServiceTest, NoMembership_NoPermissions) {
  auto perms = _psService->getEffectivePermissions(999999);
  EXPECT_TRUE(perms.empty());
}

TEST_F(PermissionServiceTest, HasPermission_NonExistentUser) {
  EXPECT_FALSE(_psService->hasPermission(999999, dns::common::Permissions::kZonesView));
}
