// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Meridian DNS Contributors
// This file is part of Meridian DNS. See LICENSE for details.

#include "security/FederatedAuthService.hpp"

#include <gtest/gtest.h>

#include <string>
#include <vector>

using dns::security::FederatedAuthService;

TEST(FederatedAuthServiceTest, MatchGroupMappingExact) {
  nlohmann::json jMappings = {
      {"rules", nlohmann::json::array({
          {{"idp_group", "dns-admins"}, {"meridian_group_id", 1}},
          {{"idp_group", "dns-operators"}, {"meridian_group_id", 2}},
      })},
  };

  std::vector<std::string> vIdpGroups = {"dns-admins", "other-group"};
  auto vResult = FederatedAuthService::matchGroups(jMappings, vIdpGroups, 0);

  ASSERT_EQ(vResult.size(), 1u);
  EXPECT_EQ(vResult[0], 1);
}

TEST(FederatedAuthServiceTest, MatchGroupMappingWildcard) {
  nlohmann::json jMappings = {
      {"rules", nlohmann::json::array({
          {{"idp_group", "platform-*"}, {"meridian_group_id", 3}},
      })},
  };

  std::vector<std::string> vIdpGroups = {"platform-team", "platform-ops"};
  auto vResult = FederatedAuthService::matchGroups(jMappings, vIdpGroups, 0);

  // Should match once (deduplicated)
  ASSERT_EQ(vResult.size(), 1u);
  EXPECT_EQ(vResult[0], 3);
}

TEST(FederatedAuthServiceTest, MatchGroupMappingNoMatch) {
  nlohmann::json jMappings = {
      {"rules", nlohmann::json::array({
          {{"idp_group", "dns-admins"}, {"meridian_group_id", 1}},
      })},
  };

  std::vector<std::string> vIdpGroups = {"unrelated-group"};
  auto vResult = FederatedAuthService::matchGroups(jMappings, vIdpGroups, 5);

  // Should fall back to default group
  ASSERT_EQ(vResult.size(), 1u);
  EXPECT_EQ(vResult[0], 5);
}

TEST(FederatedAuthServiceTest, MatchGroupMappingEmpty) {
  nlohmann::json jMappings = {
      {"rules", nlohmann::json::array()},
  };

  std::vector<std::string> vIdpGroups;  // no groups from IdP
  auto vResult = FederatedAuthService::matchGroups(jMappings, vIdpGroups, 10);

  // Should fall back to default group
  ASSERT_EQ(vResult.size(), 1u);
  EXPECT_EQ(vResult[0], 10);
}

TEST(FederatedAuthServiceTest, MatchGroupMappingNoDefaultNoMatch) {
  nlohmann::json jMappings = {
      {"rules", nlohmann::json::array({
          {{"idp_group", "dns-admins"}, {"meridian_group_id", 1}},
      })},
  };

  std::vector<std::string> vIdpGroups = {"unrelated-group"};
  auto vResult = FederatedAuthService::matchGroups(jMappings, vIdpGroups, 0);

  // No match and no default → empty
  EXPECT_TRUE(vResult.empty());
}

TEST(FederatedAuthServiceTest, MatchGroupMappingMultipleRules) {
  nlohmann::json jMappings = {
      {"rules", nlohmann::json::array({
          {{"idp_group", "dns-admins"}, {"meridian_group_id", 1}},
          {{"idp_group", "dns-operators"}, {"meridian_group_id", 2}},
          {{"idp_group", "platform-*"}, {"meridian_group_id", 3}},
      })},
  };

  std::vector<std::string> vIdpGroups = {"dns-admins", "platform-team"};
  auto vResult = FederatedAuthService::matchGroups(jMappings, vIdpGroups, 0);

  ASSERT_EQ(vResult.size(), 2u);
  // Should contain both 1 and 3
  EXPECT_TRUE(std::find(vResult.begin(), vResult.end(), 1) != vResult.end());
  EXPECT_TRUE(std::find(vResult.begin(), vResult.end(), 3) != vResult.end());
}

TEST(FederatedAuthServiceTest, MatchGroupMappingNullMappings) {
  nlohmann::json jMappings;  // null

  std::vector<std::string> vIdpGroups = {"some-group"};
  auto vResult = FederatedAuthService::matchGroups(jMappings, vIdpGroups, 7);

  // Null mappings → fall back to default
  ASSERT_EQ(vResult.size(), 1u);
  EXPECT_EQ(vResult[0], 7);
}
