// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Meridian DNS Contributors
// This file is part of Meridian DNS. See LICENSE for details.

#include "api/routes/SoaPresetRoutes.hpp"

#include "api/AuthMiddleware.hpp"
#include "api/RequestValidator.hpp"
#include "api/RouteHelpers.hpp"
#include "common/Errors.hpp"
#include "common/Permissions.hpp"
#include "dal/SoaPresetRepository.hpp"

#include <nlohmann/json.hpp>

namespace dns::api::routes {
using namespace dns::common;

SoaPresetRoutes::SoaPresetRoutes(dns::dal::SoaPresetRepository& sprRepo,
                                  const dns::api::AuthMiddleware& amMiddleware)
    : _sprRepo(sprRepo), _amMiddleware(amMiddleware) {}

SoaPresetRoutes::~SoaPresetRoutes() = default;

namespace {

nlohmann::json soaPresetToJson(const dns::dal::SoaPresetRow& r) {
  return {
    {"id", r.iId},
    {"name", r.sName},
    {"mname_template", r.sMnameTemplate},
    {"rname_template", r.sRnameTemplate},
    {"refresh", r.iRefresh},
    {"retry", r.iRetry},
    {"expire", r.iExpire},
    {"minimum", r.iMinimum},
    {"default_ttl", r.iDefaultTtl},
    {"created_at", std::chrono::duration_cast<std::chrono::seconds>(
                       r.tpCreatedAt.time_since_epoch()).count()},
    {"updated_at", std::chrono::duration_cast<std::chrono::seconds>(
                       r.tpUpdatedAt.time_since_epoch()).count()},
  };
}

}  // namespace

void SoaPresetRoutes::registerRoutes(crow::SimpleApp& app) {
  // GET /api/v1/soa-presets
  CROW_ROUTE(app, "/api/v1/soa-presets").methods("GET"_method)(
      [this](const crow::request& req) -> crow::response {
        try {
          auto rcCtx = authenticate(_amMiddleware, req);
          requirePermission(rcCtx, Permissions::kSoaPresetsView);

          auto vRows = _sprRepo.listAll();
          nlohmann::json jArr = nlohmann::json::array();
          for (const auto& row : vRows) {
            jArr.push_back(soaPresetToJson(row));
          }
          return jsonResponse(200, jArr);
        } catch (const common::AppError& e) {
          return errorResponse(e);
        }
      });

  // POST /api/v1/soa-presets
  CROW_ROUTE(app, "/api/v1/soa-presets").methods("POST"_method)(
      [this](const crow::request& req) -> crow::response {
        try {
          auto rcCtx = authenticate(_amMiddleware, req);
          requirePermission(rcCtx, Permissions::kSoaPresetsCreate);
          enforceBodyLimit(req);

          auto jBody = nlohmann::json::parse(req.body);
          std::string sName = jBody.value("name", "");
          std::string sMnameTemplate = jBody.value("mname_template", "");
          std::string sRnameTemplate = jBody.value("rname_template", "");

          if (sName.empty() || sMnameTemplate.empty() || sRnameTemplate.empty()) {
            throw common::ValidationError("MISSING_FIELDS",
                "name, mname_template, and rname_template are required");
          }
          dns::api::RequestValidator::validateStringLength(sName, "name", 255);
          dns::api::RequestValidator::validateStringLength(sMnameTemplate, "mname_template", 255);
          dns::api::RequestValidator::validateStringLength(sRnameTemplate, "rname_template", 255);

          int iRefresh    = jBody.value("refresh", 3600);
          int iRetry      = jBody.value("retry", 900);
          int iExpire     = jBody.value("expire", 604800);
          int iMinimum    = jBody.value("minimum", 300);
          int iDefaultTtl = jBody.value("default_ttl", 3600);

          int64_t iId = _sprRepo.create(sName, sMnameTemplate, sRnameTemplate,
                                        iRefresh, iRetry, iExpire, iMinimum, iDefaultTtl);
          return jsonResponse(201, {{"id", iId}});
        } catch (const common::AppError& e) {
          return errorResponse(e);
        } catch (const nlohmann::json::exception&) {
          return invalidJsonResponse();
        }
      });

  // GET /api/v1/soa-presets/<int>
  CROW_ROUTE(app, "/api/v1/soa-presets/<int>").methods("GET"_method)(
      [this](const crow::request& req, int iId) -> crow::response {
        try {
          auto rcCtx = authenticate(_amMiddleware, req);
          requirePermission(rcCtx, Permissions::kSoaPresetsView);

          auto oRow = _sprRepo.findById(static_cast<int64_t>(iId));
          if (!oRow.has_value()) {
            throw common::NotFoundError("SOA_PRESET_NOT_FOUND", "SOA preset not found");
          }
          return jsonResponse(200, soaPresetToJson(*oRow));
        } catch (const common::AppError& e) {
          return errorResponse(e);
        }
      });

  // PUT /api/v1/soa-presets/<int>
  CROW_ROUTE(app, "/api/v1/soa-presets/<int>").methods("PUT"_method)(
      [this](const crow::request& req, int iId) -> crow::response {
        try {
          auto rcCtx = authenticate(_amMiddleware, req);
          requirePermission(rcCtx, Permissions::kSoaPresetsEdit);
          enforceBodyLimit(req);

          auto jBody = nlohmann::json::parse(req.body);
          std::string sName = jBody.value("name", "");
          std::string sMnameTemplate = jBody.value("mname_template", "");
          std::string sRnameTemplate = jBody.value("rname_template", "");

          if (sName.empty() || sMnameTemplate.empty() || sRnameTemplate.empty()) {
            throw common::ValidationError("MISSING_FIELDS",
                "name, mname_template, and rname_template are required");
          }
          dns::api::RequestValidator::validateStringLength(sName, "name", 255);
          dns::api::RequestValidator::validateStringLength(sMnameTemplate, "mname_template", 255);
          dns::api::RequestValidator::validateStringLength(sRnameTemplate, "rname_template", 255);

          int iRefresh    = jBody.value("refresh", 3600);
          int iRetry      = jBody.value("retry", 900);
          int iExpire     = jBody.value("expire", 604800);
          int iMinimum    = jBody.value("minimum", 300);
          int iDefaultTtl = jBody.value("default_ttl", 3600);

          _sprRepo.update(static_cast<int64_t>(iId), sName, sMnameTemplate, sRnameTemplate,
                          iRefresh, iRetry, iExpire, iMinimum, iDefaultTtl);
          return jsonResponse(200, {{"message", "SOA preset updated"}});
        } catch (const common::AppError& e) {
          return errorResponse(e);
        } catch (const nlohmann::json::exception&) {
          return invalidJsonResponse();
        }
      });

  // DELETE /api/v1/soa-presets/<int>
  CROW_ROUTE(app, "/api/v1/soa-presets/<int>").methods("DELETE"_method)(
      [this](const crow::request& req, int iId) -> crow::response {
        try {
          auto rcCtx = authenticate(_amMiddleware, req);
          requirePermission(rcCtx, Permissions::kSoaPresetsDelete);

          _sprRepo.deleteById(static_cast<int64_t>(iId));
          return jsonResponse(200, {{"message", "SOA preset deleted"}});
        } catch (const common::AppError& e) {
          return errorResponse(e);
        }
      });
}

}  // namespace dns::api::routes
