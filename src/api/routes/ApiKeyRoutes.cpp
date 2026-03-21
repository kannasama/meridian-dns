// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Meridian DNS Contributors
// This file is part of Meridian DNS. See LICENSE for details.

#include "api/routes/ApiKeyRoutes.hpp"

#include "api/AuthMiddleware.hpp"
#include "api/RequestValidator.hpp"
#include "api/RouteHelpers.hpp"
#include "common/Errors.hpp"
#include "dal/ApiKeyRepository.hpp"
#include "security/CryptoService.hpp"

#include <nlohmann/json.hpp>

namespace dns::api::routes {

ApiKeyRoutes::ApiKeyRoutes(dns::dal::ApiKeyRepository& akrRepo,
                           const dns::api::AuthMiddleware& amMiddleware)
    : _akrRepo(akrRepo), _amMiddleware(amMiddleware) {}

ApiKeyRoutes::~ApiKeyRoutes() = default;

namespace {

nlohmann::json apiKeyRowToJson(const dns::dal::ApiKeyRow& row) {
  nlohmann::json j = {
      {"id", row.iId},
      {"user_id", row.iUserId},
      {"description", row.sDescription},
      {"prefix", row.sKeyPrefix.empty() ? row.sKeyHash.substr(0, 8) : row.sKeyPrefix},
      {"created_at", std::chrono::duration_cast<std::chrono::seconds>(
                         row.tpCreatedAt.time_since_epoch())
                         .count()},
  };
  if (row.oExpiresAt.has_value()) {
    j["expires_at"] = std::chrono::duration_cast<std::chrono::seconds>(
                          row.oExpiresAt->time_since_epoch())
                          .count();
  } else {
    j["expires_at"] = nullptr;
  }
  if (row.oLastUsedAt.has_value()) {
    j["last_used_at"] = std::chrono::duration_cast<std::chrono::seconds>(
                            row.oLastUsedAt->time_since_epoch())
                            .count();
  } else {
    j["last_used_at"] = nullptr;
  }
  return j;
}

}  // namespace

void ApiKeyRoutes::registerRoutes(crow::SimpleApp& app) {
  // GET /api/v1/api-keys
  CROW_ROUTE(app, "/api/v1/api-keys").methods("GET"_method)(
      [this](const crow::request& req) -> crow::response {
        try {
          auto rcCtx = authenticate(_amMiddleware, req);

          std::vector<dns::dal::ApiKeyRow> vKeys;
          if (rcCtx.sRole == "admin") {
            vKeys = _akrRepo.listAll();
          } else {
            vKeys = _akrRepo.listByUser(rcCtx.iUserId);
          }

          nlohmann::json jArr = nlohmann::json::array();
          for (const auto& key : vKeys) {
            jArr.push_back(apiKeyRowToJson(key));
          }
          return jsonResponse(200, jArr);
        } catch (const common::AppError& e) {
          return errorResponse(e);
        }
      });

  // POST /api/v1/api-keys
  CROW_ROUTE(app, "/api/v1/api-keys").methods("POST"_method)(
      [this](const crow::request& req) -> crow::response {
        try {
          auto rcCtx = authenticate(_amMiddleware, req);
          enforceBodyLimit(req);

          auto jBody = nlohmann::json::parse(req.body);
          std::string sDescription = jBody.value("description", "");
          RequestValidator::validateApiKeyDescription(sDescription);

          std::optional<std::chrono::system_clock::time_point> oExpiresAt;
          if (jBody.contains("expires_in_days") && !jBody["expires_in_days"].is_null()) {
            int iDays = jBody["expires_in_days"].get<int>();
            oExpiresAt = std::chrono::system_clock::now() + std::chrono::hours(24 * iDays);
          }

          std::string sRawKey = dns::security::CryptoService::generateApiKey();
          std::string sKeyHash = dns::security::CryptoService::hashApiKey(sRawKey);

          int64_t iKeyId = _akrRepo.create(rcCtx.iUserId, sKeyHash, sDescription, oExpiresAt);

          return jsonResponse(201, {
              {"id", iKeyId},
              {"key", sRawKey},
              {"prefix", sKeyHash.substr(0, 8)},
          });
        } catch (const common::AppError& e) {
          return errorResponse(e);
        } catch (const nlohmann::json::exception&) {
          return invalidJsonResponse();
        }
      });

  // DELETE /api/v1/api-keys/<int>
  CROW_ROUTE(app, "/api/v1/api-keys/<int>").methods("DELETE"_method)(
      [this](const crow::request& req, int iKeyId) -> crow::response {
        try {
          auto rcCtx = authenticate(_amMiddleware, req);

          // Non-admin can only delete own keys
          if (rcCtx.sRole != "admin") {
            auto vUserKeys = _akrRepo.listByUser(rcCtx.iUserId);
            bool bOwnsKey = false;
            for (const auto& key : vUserKeys) {
              if (key.iId == iKeyId) {
                bOwnsKey = true;
                break;
              }
            }
            if (!bOwnsKey) {
              throw common::AuthorizationError("FORBIDDEN",
                                               "You can only revoke your own API keys");
            }
          }

          _akrRepo.scheduleDelete(iKeyId, 300);
          return jsonResponse(200, {{"message", "API key scheduled for deletion"}});
        } catch (const common::AppError& e) {
          return errorResponse(e);
        }
      });
}

}  // namespace dns::api::routes
