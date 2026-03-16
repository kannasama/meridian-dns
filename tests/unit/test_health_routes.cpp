// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Meridian DNS Contributors
// This file is part of Meridian DNS. See LICENSE for details.

#include "api/routes/HealthRoutes.hpp"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <crow.h>
#include <cstdlib>
#include <string>

#include "dal/ConnectionPool.hpp"
#include "dal/GitRepoRepository.hpp"
#include "dal/ProviderRepository.hpp"
#include "security/CryptoService.hpp"

namespace {
std::string getDbUrl() {
  const char* pUrl = std::getenv("DNS_DB_URL");
  return pUrl ? std::string(pUrl) : std::string{};
}

const std::string kTestMasterKey =
    "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
}  // namespace

class HealthRoutesTest : public ::testing::Test {
 protected:
  void SetUp() override {
    _sDbUrl = getDbUrl();
    if (_sDbUrl.empty()) {
      GTEST_SKIP() << "DNS_DB_URL not set — skipping integration test";
    }

    _cpPool = std::make_unique<dns::dal::ConnectionPool>(_sDbUrl, 2);
    _csService = std::make_unique<dns::security::CryptoService>(kTestMasterKey);
    _grRepo = std::make_unique<dns::dal::GitRepoRepository>(*_cpPool, *_csService);
    _prRepo = std::make_unique<dns::dal::ProviderRepository>(*_cpPool, *_csService);

    _healthRoutes = std::make_unique<dns::api::routes::HealthRoutes>(
        *_cpPool, *_grRepo, *_prRepo);
    _app = std::make_unique<crow::SimpleApp>();
    _healthRoutes->registerRoutes(*_app);
    _app->validate();
  }

  crow::response handle(const std::string& sMethod, const std::string& sUrl) {
    crow::request req;
    req.url = sUrl;
    req.raw_url = sUrl;
    if (sMethod == "GET") req.method = crow::HTTPMethod::GET;
    crow::response resp;
    _app->handle_full(req, resp);
    return resp;
  }

  std::string _sDbUrl;
  std::unique_ptr<dns::dal::ConnectionPool> _cpPool;
  std::unique_ptr<dns::security::CryptoService> _csService;
  std::unique_ptr<dns::dal::GitRepoRepository> _grRepo;
  std::unique_ptr<dns::dal::ProviderRepository> _prRepo;
  std::unique_ptr<dns::api::routes::HealthRoutes> _healthRoutes;
  std::unique_ptr<crow::SimpleApp> _app;
};

TEST_F(HealthRoutesTest, LiveProbeReturnsAliveAndVersion) {
  auto resp = handle("GET", "/api/v1/health/live");
  EXPECT_EQ(resp.code, 200);
  auto j = nlohmann::json::parse(resp.body);
  EXPECT_EQ(j["status"], "alive");
  EXPECT_FALSE(j["version"].get<std::string>().empty());
}

TEST_F(HealthRoutesTest, ReadyProbeReturns200WhenDbHealthy) {
  auto resp = handle("GET", "/api/v1/health/ready");
  EXPECT_EQ(resp.code, 200);
  auto j = nlohmann::json::parse(resp.body);
  EXPECT_EQ(j["status"], "healthy");
  EXPECT_FALSE(j["version"].get<std::string>().empty());
  EXPECT_TRUE(j.contains("components"));
  EXPECT_EQ(j["components"]["database"], "healthy");
  EXPECT_TRUE(j["components"].contains("git_repos"));
  EXPECT_TRUE(j["components"].contains("providers"));
}

TEST_F(HealthRoutesTest, HealthEndpointIncludesVersion) {
  auto resp = handle("GET", "/api/v1/health");
  EXPECT_EQ(resp.code, 200);
  auto j = nlohmann::json::parse(resp.body);
  EXPECT_EQ(j["status"], "ok");
  EXPECT_FALSE(j["version"].get<std::string>().empty());
}
