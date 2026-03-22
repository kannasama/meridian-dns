// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Meridian DNS Contributors
// This file is part of Meridian DNS. See LICENSE for details.

#include "api/routes/PreferenceRoutes.hpp"

#include "api/AuthMiddleware.hpp"
#include "api/RouteHelpers.hpp"
#include "common/Errors.hpp"
#include "dal/UserPreferenceRepository.hpp"

#include <nlohmann/json.hpp>

namespace dns::api::routes {
using namespace dns::common;

PreferenceRoutes::PreferenceRoutes(
    dns::dal::UserPreferenceRepository& uprRepo,
    const dns::api::AuthMiddleware& amMiddleware)
    : _uprRepo(uprRepo), _amMiddleware(amMiddleware) {}

PreferenceRoutes::~PreferenceRoutes() = default;

void PreferenceRoutes::registerRoutes(crow::SimpleApp& app) {
  // GET /api/v1/preferences
  CROW_ROUTE(app, "/api/v1/preferences").methods("GET"_method)(
      [this](const crow::request& req) -> crow::response {
        try {
          auto rcCtx = authenticate(_amMiddleware, req);

          auto mPrefs = _uprRepo.getAll(rcCtx.iUserId);
          nlohmann::json jResult = nlohmann::json::object();
          for (const auto& [sKey, jValue] : mPrefs) {
            jResult[sKey] = jValue;
          }
          return jsonResponse(200, jResult);
        } catch (const common::AppError& e) {
          return errorResponse(e);
        }
      });

  // PUT /api/v1/preferences
  CROW_ROUTE(app, "/api/v1/preferences").methods("PUT"_method)(
      [this](const crow::request& req) -> crow::response {
        try {
          auto rcCtx = authenticate(_amMiddleware, req);
          enforceBodyLimit(req);
          auto jBody = nlohmann::json::parse(req.body);

          if (!jBody.is_object()) {
            throw common::ValidationError("INVALID_BODY",
                "Request body must be a JSON object");
          }

          // Validate key count and key lengths
          if (jBody.size() > 50) {
            throw common::ValidationError("TOO_MANY_KEYS",
                "Maximum 50 preference keys allowed");
          }

          std::map<std::string, nlohmann::json> mPrefs;
          for (auto& [sKey, jValue] : jBody.items()) {
            if (sKey.empty() || sKey.size() > 64) {
              throw common::ValidationError("INVALID_KEY",
                  "Preference key must be 1-64 characters");
            }
            mPrefs[sKey] = jValue;
          }

          _uprRepo.setAll(rcCtx.iUserId, mPrefs);
          return jsonResponse(200, {{"message", "Preferences updated"}});
        } catch (const common::AppError& e) {
          return errorResponse(e);
        } catch (const nlohmann::json::exception&) {
          return invalidJsonResponse();
        }
      });
}

}  // namespace dns::api::routes
