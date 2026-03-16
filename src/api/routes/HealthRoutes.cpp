// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Meridian DNS Contributors
// This file is part of Meridian DNS. See LICENSE for details.

#include "api/routes/HealthRoutes.hpp"

#include "api/RouteHelpers.hpp"
#include "common/Version.hpp"
#include "dal/ConnectionPool.hpp"
#include "dal/GitRepoRepository.hpp"
#include "dal/ProviderRepository.hpp"

#include <crow.h>
#include <nlohmann/json.hpp>
#include <pqxx/pqxx>

namespace dns::api::routes {

HealthRoutes::HealthRoutes(dns::dal::ConnectionPool& cpPool,
                           dns::dal::GitRepoRepository& grRepo,
                           dns::dal::ProviderRepository& prRepo)
    : _cpPool(cpPool), _grRepo(grRepo), _prRepo(prRepo) {}

HealthRoutes::~HealthRoutes() = default;

void HealthRoutes::registerRoutes(crow::SimpleApp& app) {
  // GET /api/v1/health — basic health check with version
  CROW_ROUTE(app, "/api/v1/health")
      .methods(crow::HTTPMethod::GET)([](const crow::request& /*req*/) {
        return jsonResponse(200, {{"status", "ok"}, {"version", MERIDIAN_VERSION}});
      });

  // GET /api/v1/health/live — liveness probe (no dependencies)
  CROW_ROUTE(app, "/api/v1/health/live")
      .methods(crow::HTTPMethod::GET)([](const crow::request& /*req*/) {
        return jsonResponse(200, {
          {"status", "alive"},
          {"version", MERIDIAN_VERSION}
        });
      });

  // GET /api/v1/health/ready — readiness probe (checks DB + repos)
  CROW_ROUTE(app, "/api/v1/health/ready")
      .methods(crow::HTTPMethod::GET)([this](const crow::request& /*req*/) {
        nlohmann::json jComponents;

        // 1. Database check
        std::string sDbStatus = "unhealthy";
        try {
          auto cg = _cpPool.checkout();
          pqxx::work txn(*cg);
          txn.exec("SELECT 1").one_row();
          txn.commit();
          sDbStatus = "healthy";
        } catch (...) {
          // DB unreachable
        }
        jComponents["database"] = sDbStatus;

        // 2. Git repos sync status
        int iGitHealthy = 0, iGitFailed = 0;
        try {
          auto vRepos = _grRepo.listEnabled();
          for (const auto& repo : vRepos) {
            if (repo.sLastSyncStatus == "failed") ++iGitFailed;
            else ++iGitHealthy;  // success or never synced
          }
        } catch (...) {
          // If we can't query, don't crash the probe
        }
        jComponents["git_repos"] = {{"healthy", iGitHealthy}, {"failed", iGitFailed}};

        // 3. Providers count
        int iProviders = 0;
        try {
          auto vProviders = _prRepo.listAll();
          iProviders = static_cast<int>(vProviders.size());
        } catch (...) {}
        jComponents["providers"] = {{"healthy", iProviders}, {"degraded", 0}};

        // Overall status
        std::string sOverall = "healthy";
        if (sDbStatus == "unhealthy") {
          sOverall = "unhealthy";
        } else if (iGitFailed > 0) {
          sOverall = "degraded";
        }

        int iHttpCode = (sDbStatus == "healthy") ? 200 : 503;
        return jsonResponse(iHttpCode, {
          {"status", sOverall},
          {"version", MERIDIAN_VERSION},
          {"components", jComponents}
        });
      });
}

}  // namespace dns::api::routes
