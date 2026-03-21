// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Meridian DNS Contributors
// This file is part of Meridian DNS. See LICENSE for details.

#include "api/AuthMiddleware.hpp"
#include "api/routes/RoleRoutes.hpp"
#include "common/Logger.hpp"
#include "dal/ApiKeyRepository.hpp"
#include "dal/ConnectionPool.hpp"
#include "dal/RoleRepository.hpp"
#include "dal/SessionRepository.hpp"
#include "dal/UserRepository.hpp"
#include "security/AuthService.hpp"
#include "security/CryptoService.hpp"
#include "security/HmacJwtSigner.hpp"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <cstdlib>
#include <string>

using dns::dal::ConnectionPool;
using dns::dal::RoleRepository;
using dns::dal::UserRepository;

namespace {

std::string getDbUrl() {
  const char* pUrl = std::getenv("DNS_DB_URL");
  return pUrl ? std::string(pUrl) : std::string{};
}

const std::string kTestJwtSecret =
    "test-jwt-secret-that-is-at-least-32-bytes-long!!";

}  // namespace

class RoleRoutesTest : public ::testing::Test {
 protected:
  void SetUp() override {
    _sDbUrl = getDbUrl();
    if (_sDbUrl.empty()) {
      GTEST_SKIP() << "DNS_DB_URL not set — skipping integration test";
    }
    dns::common::Logger::init("warn");
    _cpPool = std::make_unique<ConnectionPool>(_sDbUrl, 2);
    _jsSigner = std::make_unique<dns::security::HmacJwtSigner>(kTestJwtSecret);

    _urRepo = std::make_unique<dns::dal::UserRepository>(*_cpPool);
    _srRepo = std::make_unique<dns::dal::SessionRepository>(*_cpPool);
    _akrRepo = std::make_unique<dns::dal::ApiKeyRepository>(*_cpPool);
    _roleRepo = std::make_unique<dns::dal::RoleRepository>(*_cpPool);

    _amMiddleware = std::make_unique<dns::api::AuthMiddleware>(
        *_jsSigner, *_srRepo, *_akrRepo, *_urRepo, *_roleRepo, 3600, 86400, 300);
    _asService = std::make_unique<dns::security::AuthService>(
        *_urRepo, *_srRepo, *_roleRepo, *_jsSigner, 3600, 86400);

    _roleRoutes = std::make_unique<dns::api::routes::RoleRoutes>(*_roleRepo, *_amMiddleware);

    // Clean test data
    auto cg = _cpPool->checkout();
    pqxx::work txn(*cg);
    txn.exec("DELETE FROM sessions");
    txn.exec("DELETE FROM api_keys");
    txn.exec("DELETE FROM group_members");
    txn.exec("DELETE FROM deployments");
    txn.exec("DELETE FROM users");
    txn.exec("DELETE FROM groups");
    txn.exec("DELETE FROM role_permissions WHERE role_id IN "
             "(SELECT id FROM roles WHERE is_system = false)");
    txn.exec("DELETE FROM roles WHERE is_system = false");

    // Create admin user
    std::string sHash = dns::security::CryptoService::hashPassword("admin123");
    auto rUser = txn.exec(
        "INSERT INTO users (username, email, password_hash, auth_method) "
        "VALUES ('admin', 'admin@test.com', $1, 'local') RETURNING id",
        pqxx::params{sHash});
    int64_t iUserId = rUser[0][0].as<int64_t>();

    auto rRole = txn.exec("SELECT id FROM roles WHERE name = 'Admin'").one_row();
    int64_t iRoleId = rRole[0].as<int64_t>();
    auto rGroup = txn.exec(
        "INSERT INTO groups (name, role_id) VALUES ('admins', $1) RETURNING id",
        pqxx::params{iRoleId});
    int64_t iGroupId = rGroup[0][0].as<int64_t>();
    txn.exec("INSERT INTO group_members (user_id, group_id) VALUES ($1, $2)",
             pqxx::params{iUserId, iGroupId});
    txn.commit();

    _sToken = _asService->authenticateLocal("admin", "admin123");

    // Set up Crow app and register routes
    _roleRoutes->registerRoutes(_app);
    _app.validate();
  }

  crow::response handle(const std::string& sMethod, const std::string& sUrl,
                        const std::string& sBody = "") {
    crow::request req;
    auto iQmark = sUrl.find('?');
    if (iQmark != std::string::npos) {
      req.url = sUrl.substr(0, iQmark);
      req.raw_url = sUrl;
      req.url_params = crow::query_string(sUrl);
    } else {
      req.url = sUrl;
      req.raw_url = sUrl;
    }
    req.body = sBody;
    if (sMethod == "GET") req.method = crow::HTTPMethod::GET;
    else if (sMethod == "POST") req.method = crow::HTTPMethod::POST;
    else if (sMethod == "PUT") req.method = crow::HTTPMethod::PUT;
    else if (sMethod == "DELETE") req.method = crow::HTTPMethod::DELETE;
    req.add_header("Authorization", "Bearer " + _sToken);
    req.add_header("Content-Type", "application/json");

    crow::response resp;
    _app.handle_full(req, resp);
    return resp;
  }

  std::string _sDbUrl;
  std::unique_ptr<ConnectionPool> _cpPool;
  std::unique_ptr<dns::security::HmacJwtSigner> _jsSigner;
  std::unique_ptr<dns::dal::UserRepository> _urRepo;
  std::unique_ptr<dns::dal::SessionRepository> _srRepo;
  std::unique_ptr<dns::dal::ApiKeyRepository> _akrRepo;
  std::unique_ptr<dns::dal::RoleRepository> _roleRepo;
  std::unique_ptr<dns::api::AuthMiddleware> _amMiddleware;
  std::unique_ptr<dns::security::AuthService> _asService;
  std::unique_ptr<dns::api::routes::RoleRoutes> _roleRoutes;
  crow::SimpleApp _app;
  std::string _sToken;
};

TEST_F(RoleRoutesTest, ListRolesReturnsSystemRoles) {
  auto resp = handle("GET", "/api/v1/roles");
  EXPECT_EQ(resp.code, 200);
  auto jArr = nlohmann::json::parse(resp.body);
  ASSERT_TRUE(jArr.is_array());
  EXPECT_GE(jArr.size(), 3u);  // Admin, Operator, Viewer

  bool bFoundAdmin = false;
  for (const auto& r : jArr) {
    if (r["name"] == "Admin") {
      bFoundAdmin = true;
      EXPECT_TRUE(r["is_system"].get<bool>());
    }
  }
  EXPECT_TRUE(bFoundAdmin);
}

TEST_F(RoleRoutesTest, CreateCustomRoleSucceeds) {
  nlohmann::json jBody = {
      {"name", "Deployer"},
      {"description", "Can deploy zones"},
      {"permissions", {"zones.view", "zones.deploy"}},
  };
  auto resp = handle("POST", "/api/v1/roles", jBody.dump());
  EXPECT_EQ(resp.code, 201);
  auto jResp = nlohmann::json::parse(resp.body);
  EXPECT_TRUE(jResp.contains("id"));
}

TEST_F(RoleRoutesTest, CreateDuplicateNameReturns409) {
  nlohmann::json jBody = {{"name", "Deployer"}, {"description", ""}};
  handle("POST", "/api/v1/roles", jBody.dump());

  auto resp = handle("POST", "/api/v1/roles", jBody.dump());
  EXPECT_EQ(resp.code, 409);
}

TEST_F(RoleRoutesTest, GetRoleByIdIncludesPermissions) {
  nlohmann::json jBody = {
      {"name", "TestRole"},
      {"description", "Test"},
      {"permissions", {"zones.view", "records.view"}},
  };
  auto resp = handle("POST", "/api/v1/roles", jBody.dump());
  auto iId = nlohmann::json::parse(resp.body)["id"].get<int>();

  resp = handle("GET", "/api/v1/roles/" + std::to_string(iId));
  EXPECT_EQ(resp.code, 200);
  auto jRole = nlohmann::json::parse(resp.body);
  EXPECT_EQ(jRole["name"], "TestRole");
  EXPECT_TRUE(jRole.contains("permissions"));
  EXPECT_EQ(jRole["permissions"].size(), 2u);
}

TEST_F(RoleRoutesTest, UpdateCustomRoleSucceeds) {
  nlohmann::json jBody = {{"name", "OldName"}, {"description", "old"}};
  auto resp = handle("POST", "/api/v1/roles", jBody.dump());
  auto iId = nlohmann::json::parse(resp.body)["id"].get<int>();

  nlohmann::json jUpdate = {{"name", "NewName"}, {"description", "new"}};
  resp = handle("PUT", "/api/v1/roles/" + std::to_string(iId), jUpdate.dump());
  EXPECT_EQ(resp.code, 200);

  resp = handle("GET", "/api/v1/roles/" + std::to_string(iId));
  auto jRole = nlohmann::json::parse(resp.body);
  EXPECT_EQ(jRole["name"], "NewName");
  EXPECT_EQ(jRole["description"], "new");
}

TEST_F(RoleRoutesTest, RenameSystemRoleReturns400) {
  // Get Admin role ID
  auto resp = handle("GET", "/api/v1/roles");
  auto jArr = nlohmann::json::parse(resp.body);
  int iAdminId = 0;
  for (const auto& r : jArr) {
    if (r["name"] == "Admin") { iAdminId = r["id"].get<int>(); break; }
  }
  ASSERT_GT(iAdminId, 0);

  nlohmann::json jUpdate = {{"name", "SuperAdmin"}, {"description", "changed"}};
  resp = handle("PUT", "/api/v1/roles/" + std::to_string(iAdminId), jUpdate.dump());
  EXPECT_EQ(resp.code, 400);
}

TEST_F(RoleRoutesTest, DeleteCustomRoleSucceeds) {
  nlohmann::json jBody = {{"name", "ToDelete"}, {"description", ""}};
  auto resp = handle("POST", "/api/v1/roles", jBody.dump());
  auto iId = nlohmann::json::parse(resp.body)["id"].get<int>();

  resp = handle("DELETE", "/api/v1/roles/" + std::to_string(iId));
  EXPECT_EQ(resp.code, 200);

  resp = handle("GET", "/api/v1/roles/" + std::to_string(iId));
  EXPECT_EQ(resp.code, 404);
}

TEST_F(RoleRoutesTest, DeleteSystemRoleReturns409) {
  auto resp = handle("GET", "/api/v1/roles");
  auto jArr = nlohmann::json::parse(resp.body);
  int iAdminId = 0;
  for (const auto& r : jArr) {
    if (r["name"] == "Admin") { iAdminId = r["id"].get<int>(); break; }
  }
  ASSERT_GT(iAdminId, 0);

  resp = handle("DELETE", "/api/v1/roles/" + std::to_string(iAdminId));
  EXPECT_EQ(resp.code, 409);
}

TEST_F(RoleRoutesTest, SetPermissionsReplacesAll) {
  nlohmann::json jBody = {
      {"name", "PermTest"},
      {"description", ""},
      {"permissions", {"zones.view"}},
  };
  auto resp = handle("POST", "/api/v1/roles", jBody.dump());
  auto iId = nlohmann::json::parse(resp.body)["id"].get<int>();
  std::string sUrl = "/api/v1/roles/" + std::to_string(iId) + "/permissions";

  // Replace with different set
  nlohmann::json jPerms = {{"permissions", {"records.view", "records.create"}}};
  resp = handle("PUT", sUrl, jPerms.dump());
  EXPECT_EQ(resp.code, 200);

  // Verify
  resp = handle("GET", sUrl);
  EXPECT_EQ(resp.code, 200);
  auto jResult = nlohmann::json::parse(resp.body);
  EXPECT_EQ(jResult.size(), 2u);
}

TEST_F(RoleRoutesTest, SetInvalidPermissionReturns400) {
  nlohmann::json jBody = {{"name", "BadPerm"}, {"description", ""}};
  auto resp = handle("POST", "/api/v1/roles", jBody.dump());
  auto iId = nlohmann::json::parse(resp.body)["id"].get<int>();

  nlohmann::json jPerms = {{"permissions", {"zones.view", "fake.permission"}}};
  resp = handle("PUT", "/api/v1/roles/" + std::to_string(iId) + "/permissions", jPerms.dump());
  EXPECT_EQ(resp.code, 400);
}

TEST_F(RoleRoutesTest, ListPermissionsReturnsAllCategories) {
  auto resp = handle("GET", "/api/v1/permissions");
  EXPECT_EQ(resp.code, 200);
  auto jArr = nlohmann::json::parse(resp.body);
  ASSERT_TRUE(jArr.is_array());
  EXPECT_EQ(jArr.size(), 12u);  // 12 categories

  // Check first category has expected structure
  EXPECT_TRUE(jArr[0].contains("name"));
  EXPECT_TRUE(jArr[0].contains("permissions"));
  EXPECT_EQ(jArr[0]["name"], "Zones");
}
