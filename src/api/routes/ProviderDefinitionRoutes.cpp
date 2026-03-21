// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Meridian DNS Contributors
// This file is part of Meridian DNS. See LICENSE for details.

#include "api/routes/ProviderDefinitionRoutes.hpp"

#include "api/AuthMiddleware.hpp"
#include "api/RequestValidator.hpp"
#include "api/RouteHelpers.hpp"
#include "common/Errors.hpp"
#include "common/Permissions.hpp"
#include "dal/ProviderDefinitionRepository.hpp"

#include <nlohmann/json.hpp>

namespace dns::api::routes {
using namespace dns::common;

namespace {

nlohmann::json definitionToJson(const dns::dal::ProviderDefinitionRow& row) {
  return {
    {"id", row.iId},
    {"name", row.sName},
    {"type_slug", row.sTypeSlug},
    {"version", row.sVersion},
    {"definition", row.jDefinition},
    {"source_url", row.sSourceUrl},
    {"active_instance_count", row.iActiveInstanceCount},
    {"imported_at", std::chrono::duration_cast<std::chrono::seconds>(
                        row.tpImportedAt.time_since_epoch()).count()},
    {"updated_at", std::chrono::duration_cast<std::chrono::seconds>(
                        row.tpUpdatedAt.time_since_epoch()).count()},
  };
}

}  // namespace

ProviderDefinitionRoutes::ProviderDefinitionRoutes(
    dns::dal::ProviderDefinitionRepository& pdrRepo,
    const dns::api::AuthMiddleware& amMiddleware)
    : _pdrRepo(pdrRepo), _amMiddleware(amMiddleware) {}

ProviderDefinitionRoutes::~ProviderDefinitionRoutes() = default;

void ProviderDefinitionRoutes::registerRoutes(crow::SimpleApp& app) {
  // GET /api/v1/provider-definitions
  CROW_ROUTE(app, "/api/v1/provider-definitions").methods("GET"_method)(
      [this](const crow::request& req) -> crow::response {
        try {
          auto rcCtx = authenticate(_amMiddleware, req);
          requirePermission(rcCtx, Permissions::kProviderDefinitionsView);

          auto vRows = _pdrRepo.listAll();
          nlohmann::json jArr = nlohmann::json::array();
          for (const auto& row : vRows) {
            jArr.push_back(definitionToJson(row));
          }
          return jsonResponse(200, jArr);
        } catch (const common::AppError& e) {
          return errorResponse(e);
        }
      });

  // POST /api/v1/provider-definitions
  CROW_ROUTE(app, "/api/v1/provider-definitions").methods("POST"_method)(
      [this](const crow::request& req) -> crow::response {
        try {
          auto rcCtx = authenticate(_amMiddleware, req);
          requirePermission(rcCtx, Permissions::kProviderDefinitionsCreate);
          enforceBodyLimit(req);

          auto jBody = nlohmann::json::parse(req.body);
          std::string sName      = jBody.value("name", "");
          std::string sTypeSlug  = jBody.value("type_slug", "");
          std::string sVersion   = jBody.value("version", "");
          std::string sSourceUrl = jBody.value("source_url", "");
          nlohmann::json jDefinition = jBody.value("definition", nlohmann::json::object());

          if (sName.empty() || sTypeSlug.empty() || sVersion.empty()) {
            throw common::ValidationError("MISSING_FIELDS",
                                          "name, type_slug, and version are required");
          }
          dns::api::RequestValidator::validateStringLength(sName, "name", 255);
          dns::api::RequestValidator::validateStringLength(sTypeSlug, "type_slug", 100);
          dns::api::RequestValidator::validateStringLength(sVersion, "version", 50);
          if (!sSourceUrl.empty()) {
            dns::api::RequestValidator::validateStringLength(sSourceUrl, "source_url", 500);
          }

          try {
            int64_t iId = _pdrRepo.create(sName, sTypeSlug, sVersion, jDefinition, sSourceUrl);
            return jsonResponse(201, {{"id", iId}, {"updated", false}});
          } catch (const common::ConflictError& ce) {
            if (ce._sErrorCode != "DEFINITION_EXISTS") throw;
            // Upsert: find existing by type_slug and update
            auto oExisting = _pdrRepo.findByTypeSlug(sTypeSlug);
            if (!oExisting.has_value()) {
              throw common::NotFoundError("DEFINITION_NOT_FOUND",
                                          "Provider definition not found after conflict");
            }
            _pdrRepo.update(oExisting->iId, sName, sVersion, jDefinition, sSourceUrl);
            return jsonResponse(200, {{"id", oExisting->iId}, {"updated", true}});
          }
        } catch (const common::AppError& e) {
          return errorResponse(e);
        } catch (const nlohmann::json::exception&) {
          return invalidJsonResponse();
        }
      });

  // GET /api/v1/provider-definitions/<int>
  CROW_ROUTE(app, "/api/v1/provider-definitions/<int>").methods("GET"_method)(
      [this](const crow::request& req, int iId) -> crow::response {
        try {
          auto rcCtx = authenticate(_amMiddleware, req);
          requirePermission(rcCtx, Permissions::kProviderDefinitionsView);

          auto oRow = _pdrRepo.findById(static_cast<int64_t>(iId));
          if (!oRow.has_value()) {
            throw common::NotFoundError("DEFINITION_NOT_FOUND",
                                        "Provider definition not found");
          }
          return jsonResponse(200, definitionToJson(*oRow));
        } catch (const common::AppError& e) {
          return errorResponse(e);
        }
      });

  // PUT /api/v1/provider-definitions/<int>
  CROW_ROUTE(app, "/api/v1/provider-definitions/<int>").methods("PUT"_method)(
      [this](const crow::request& req, int iId) -> crow::response {
        try {
          auto rcCtx = authenticate(_amMiddleware, req);
          requirePermission(rcCtx, Permissions::kProviderDefinitionsEdit);
          enforceBodyLimit(req);

          auto jBody = nlohmann::json::parse(req.body);
          std::string sName      = jBody.value("name", "");
          std::string sVersion   = jBody.value("version", "");
          std::string sSourceUrl = jBody.value("source_url", "");
          nlohmann::json jDefinition = jBody.value("definition", nlohmann::json::object());

          if (sName.empty() || sVersion.empty()) {
            throw common::ValidationError("MISSING_FIELDS", "name and version are required");
          }
          dns::api::RequestValidator::validateStringLength(sName, "name", 255);
          dns::api::RequestValidator::validateStringLength(sVersion, "version", 50);
          if (!sSourceUrl.empty()) {
            dns::api::RequestValidator::validateStringLength(sSourceUrl, "source_url", 500);
          }

          _pdrRepo.update(static_cast<int64_t>(iId), sName, sVersion, jDefinition, sSourceUrl);
          return jsonResponse(200, {{"message", "Provider definition updated"}});
        } catch (const common::AppError& e) {
          return errorResponse(e);
        } catch (const nlohmann::json::exception&) {
          return invalidJsonResponse();
        }
      });

  // DELETE /api/v1/provider-definitions/<int>
  CROW_ROUTE(app, "/api/v1/provider-definitions/<int>").methods("DELETE"_method)(
      [this](const crow::request& req, int iId) -> crow::response {
        try {
          auto rcCtx = authenticate(_amMiddleware, req);
          requirePermission(rcCtx, Permissions::kProviderDefinitionsDelete);

          _pdrRepo.deleteById(static_cast<int64_t>(iId));
          return jsonResponse(200, {{"message", "Provider definition deleted"}});
        } catch (const common::AppError& e) {
          return errorResponse(e);
        }
      });

  // GET /api/v1/provider-definitions/<int>/export
  CROW_ROUTE(app, "/api/v1/provider-definitions/<int>/export").methods("GET"_method)(
      [this](const crow::request& req, int iId) -> crow::response {
        try {
          auto rcCtx = authenticate(_amMiddleware, req);
          requirePermission(rcCtx, Permissions::kProviderDefinitionsView);

          auto oRow = _pdrRepo.findById(static_cast<int64_t>(iId));
          if (!oRow.has_value()) {
            throw common::NotFoundError("DEFINITION_NOT_FOUND",
                                        "Provider definition not found");
          }
          auto sSafeSlug = sanitizeFilename(oRow->sTypeSlug, "definition") + ".json";

          crow::response resp(200, oRow->jDefinition.dump(2));
          resp.set_header("Content-Type", "application/json");
          resp.set_header("Content-Disposition",
                          "attachment; filename=\"" + sSafeSlug + "\"");
          resp.set_header("X-Content-Type-Options", "nosniff");
          resp.set_header("X-Frame-Options", "DENY");
          resp.set_header("Referrer-Policy", "strict-origin-when-cross-origin");
          return resp;
        } catch (const common::AppError& e) {
          return errorResponse(e);
        }
      });
}

}  // namespace dns::api::routes
