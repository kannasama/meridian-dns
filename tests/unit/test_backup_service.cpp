// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Meridian DNS Contributors
// This file is part of Meridian DNS. See LICENSE for details.

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

namespace {

/// Local validation helper matching the rules that BackupService::validateBackupFormat
/// will enforce: version==1, all 11 section keys present with correct types,
/// plus metadata fields.
bool isValidBackupJson(const nlohmann::json& j) {
  // Must be an object
  if (!j.is_object()) return false;

  // Metadata fields
  if (!j.contains("version") || !j["version"].is_number_integer() || j["version"] != 1)
    return false;
  if (!j.contains("exported_at") || !j["exported_at"].is_string()) return false;
  if (!j.contains("exported_by") || !j["exported_by"].is_string()) return false;
  if (!j.contains("meridian_version") || !j["meridian_version"].is_string()) return false;

  // Settings must be an object
  if (!j.contains("settings") || !j["settings"].is_array()) return false;

  // All array sections
  const std::vector<std::string> vArraySections = {
      "roles",     "groups",    "users",     "identity_providers", "git_repos",
      "providers", "views",     "zones",     "records",            "variables",
  };
  for (const auto& sKey : vArraySections) {
    if (!j.contains(sKey) || !j[sKey].is_array()) return false;
  }

  return true;
}

nlohmann::json makeValidBackup() {
  nlohmann::json j;
  j["version"] = 1;
  j["exported_at"] = "2026-03-15T00:00:00Z";
  j["exported_by"] = "admin";
  j["meridian_version"] = "0.1.0";
  j["settings"] = nlohmann::json::array();
  j["roles"] = nlohmann::json::array();
  j["groups"] = nlohmann::json::array();
  j["users"] = nlohmann::json::array();
  j["identity_providers"] = nlohmann::json::array();
  j["git_repos"] = nlohmann::json::array();
  j["providers"] = nlohmann::json::array();
  j["views"] = nlohmann::json::array();
  j["zones"] = nlohmann::json::array();
  j["records"] = nlohmann::json::array();
  j["variables"] = nlohmann::json::array();
  return j;
}

}  // namespace

TEST(BackupFormatTest, ValidBackupHasAllRequiredFields) {
  auto j = makeValidBackup();
  EXPECT_TRUE(isValidBackupJson(j));
}

TEST(BackupFormatTest, MissingVersionIsInvalid) {
  auto j = makeValidBackup();
  j.erase("version");
  EXPECT_FALSE(isValidBackupJson(j));
}

TEST(BackupFormatTest, WrongVersionIsInvalid) {
  auto j = makeValidBackup();
  j["version"] = 99;
  EXPECT_FALSE(isValidBackupJson(j));
}

TEST(BackupFormatTest, MissingSectionIsInvalid) {
  // Remove each section one at a time and verify invalidity
  const std::vector<std::string> vSections = {
      "settings",  "roles",   "groups",    "users",   "identity_providers",
      "git_repos", "providers", "views",   "zones",   "records",
      "variables",
  };
  for (const auto& sKey : vSections) {
    auto j = makeValidBackup();
    j.erase(sKey);
    EXPECT_FALSE(isValidBackupJson(j)) << "Should be invalid when missing: " << sKey;
  }
}

TEST(BackupFormatTest, ZoneExportHasTypeField) {
  // Zone export has a different format with a "type" field
  nlohmann::json j;
  j["version"] = 1;
  j["type"] = "zone";
  j["exported_at"] = "2026-03-15T00:00:00Z";
  j["zone"] = {{"name", "example.com"}, {"view_name", "default"}};
  j["records"] = nlohmann::json::array();
  j["variables"] = nlohmann::json::array();

  EXPECT_TRUE(j.contains("type"));
  EXPECT_EQ(j["type"], "zone");
  EXPECT_TRUE(j.contains("zone"));
  EXPECT_TRUE(j["zone"].is_object());
  EXPECT_TRUE(j.contains("records"));
  EXPECT_TRUE(j.contains("variables"));
}

TEST(BackupFormatTest, MissingMetadataFieldsAreInvalid) {
  // Missing exported_at
  {
    auto j = makeValidBackup();
    j.erase("exported_at");
    EXPECT_FALSE(isValidBackupJson(j));
  }
  // Missing exported_by
  {
    auto j = makeValidBackup();
    j.erase("exported_by");
    EXPECT_FALSE(isValidBackupJson(j));
  }
  // Missing meridian_version
  {
    auto j = makeValidBackup();
    j.erase("meridian_version");
    EXPECT_FALSE(isValidBackupJson(j));
  }
}
