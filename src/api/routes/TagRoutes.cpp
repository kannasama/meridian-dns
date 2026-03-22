// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Meridian DNS Contributors
// This file is part of Meridian DNS. See LICENSE for details.

#include "api/routes/TagRoutes.hpp"

#include "api/AuthMiddleware.hpp"
#include "api/RouteHelpers.hpp"
#include "common/Errors.hpp"
#include "common/Permissions.hpp"
#include "dal/TagRepository.hpp"

#include <chrono>
#include <nlohmann/json.hpp>

namespace dns::api::routes {
using namespace dns::common;

namespace {

nlohmann::json tagRowToJson(const dns::dal::TagRow& row) {
  return {
      {"id", row.iId},
      {"name", row.sName},
      {"zone_count", row.iZoneCount},
      {"created_at", std::chrono::duration_cast<std::chrono::seconds>(
                         row.tpCreatedAt.time_since_epoch()).count()},
  };
}

}  // namespace

TagRoutes::TagRoutes(dns::dal::TagRepository& trRepo,
                     const dns::api::AuthMiddleware& amMiddleware)
    : _trRepo(trRepo), _amMiddleware(amMiddleware) {}

TagRoutes::~TagRoutes() = default;

void TagRoutes::registerRoutes(crow::SimpleApp& app) {
  // GET /api/v1/tags
  CROW_ROUTE(app, "/api/v1/tags").methods("GET"_method)(
      [this](const crow::request& req) -> crow::response {
        try {
          auto rcCtx = authenticate(_amMiddleware, req);
          requirePermission(rcCtx, Permissions::kZonesView);

          auto vTags = _trRepo.listWithCounts();
          nlohmann::json jArr = nlohmann::json::array();
          for (const auto& t : vTags) {
            jArr.push_back(tagRowToJson(t));
          }
          return jsonResponse(200, jArr);
        } catch (const common::AppError& e) {
          return errorResponse(e);
        }
      });

  // POST /api/v1/tags
  CROW_ROUTE(app, "/api/v1/tags").methods("POST"_method)(
      [this](const crow::request& req) -> crow::response {
        try {
          auto rcCtx = authenticate(_amMiddleware, req);
          requirePermission(rcCtx, Permissions::kUsersEdit);
          enforceBodyLimit(req);
          auto jBody = nlohmann::json::parse(req.body);

          std::string sName = jBody.value("name", "");
          if (sName.empty() || sName.size() > 64) {
            throw common::ValidationError("INVALID_TAG",
                "Tag name must be 1-64 characters");
          }

          auto trRow = _trRepo.create(sName);
          return jsonResponse(201, tagRowToJson(trRow));
        } catch (const common::AppError& e) {
          return errorResponse(e);
        } catch (const nlohmann::json::exception&) {
          return invalidJsonResponse();
        }
      });

  // PUT /api/v1/tags/<int>
  CROW_ROUTE(app, "/api/v1/tags/<int>").methods("PUT"_method)(
      [this](const crow::request& req, int iTagId) -> crow::response {
        try {
          auto rcCtx = authenticate(_amMiddleware, req);
          requirePermission(rcCtx, Permissions::kUsersEdit);
          enforceBodyLimit(req);
          auto jBody = nlohmann::json::parse(req.body);

          std::string sName = jBody.value("name", "");
          if (sName.empty() || sName.size() > 64) {
            throw common::ValidationError("INVALID_TAG",
                "Tag name must be 1-64 characters");
          }

          _trRepo.rename(iTagId, sName);
          auto oRow = _trRepo.findById(iTagId);
          return jsonResponse(200, tagRowToJson(*oRow));
        } catch (const common::AppError& e) {
          return errorResponse(e);
        } catch (const nlohmann::json::exception&) {
          return invalidJsonResponse();
        }
      });

  // DELETE /api/v1/tags/<int>
  CROW_ROUTE(app, "/api/v1/tags/<int>").methods("DELETE"_method)(
      [this](const crow::request& req, int iTagId) -> crow::response {
        try {
          auto rcCtx = authenticate(_amMiddleware, req);
          requirePermission(rcCtx, Permissions::kUsersEdit);

          _trRepo.deleteTag(iTagId);
          crow::response res{204};
          return res;
        } catch (const common::AppError& e) {
          return errorResponse(e);
        }
      });
}

}  // namespace dns::api::routes
