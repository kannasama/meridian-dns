// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Meridian DNS Contributors
// This file is part of Meridian DNS. See LICENSE for details.

#include "api/routes/VariableRoutes.hpp"

#include "api/AuthMiddleware.hpp"
#include "api/RequestValidator.hpp"
#include "api/RouteHelpers.hpp"
#include "common/Errors.hpp"
#include "common/Permissions.hpp"
#include "dal/VariableRepository.hpp"

#include <nlohmann/json.hpp>

namespace dns::api::routes {
using namespace dns::common;

VariableRoutes::VariableRoutes(dns::dal::VariableRepository& varRepo,
                               const dns::api::AuthMiddleware& amMiddleware)
    : _varRepo(varRepo), _amMiddleware(amMiddleware) {}

VariableRoutes::~VariableRoutes() = default;

namespace {

nlohmann::json variableRowToJson(const dns::dal::VariableRow& row) {
  nlohmann::json j = {
      {"id", row.iId},
      {"name", row.sName},
      {"value", row.sValue},
      {"type", row.sType},
      {"scope", row.sScope},
      {"created_at", std::chrono::duration_cast<std::chrono::seconds>(
                         row.tpCreatedAt.time_since_epoch())
                         .count()},
      {"updated_at", std::chrono::duration_cast<std::chrono::seconds>(
                         row.tpUpdatedAt.time_since_epoch())
                         .count()},
  };
  if (row.oZoneId.has_value()) {
    j["zone_id"] = *row.oZoneId;
  } else {
    j["zone_id"] = nullptr;
  }
  j["variable_kind"] = row.sVariableKind;
  if (row.osDynamicFormat.has_value()) {
    j["dynamic_format"] = row.osDynamicFormat.value();
  } else {
    j["dynamic_format"] = nullptr;
  }
  return j;
}

}  // namespace

void VariableRoutes::registerRoutes(crow::SimpleApp& app) {
  // GET /api/v1/variables
  CROW_ROUTE(app, "/api/v1/variables").methods("GET"_method)(
      [this](const crow::request& req) -> crow::response {
        try {
          auto rcCtx = authenticate(_amMiddleware, req);
          requirePermission(rcCtx, Permissions::kVariablesView);

          auto sScope = req.url_params.get("scope");
          auto sZoneId = req.url_params.get("zone_id");

          std::vector<dns::dal::VariableRow> vRows;
          if (sZoneId) {
            vRows = _varRepo.listByZoneId(std::stoll(sZoneId));
          } else if (sScope) {
            vRows = _varRepo.listByScope(sScope);
          } else {
            vRows = _varRepo.listAll();
          }

          nlohmann::json jArr = nlohmann::json::array();
          for (const auto& row : vRows) {
            jArr.push_back(variableRowToJson(row));
          }
          return jsonResponse(200, jArr);
        } catch (const common::AppError& e) {
          return errorResponse(e);
        }
      });

  // POST /api/v1/variables
  CROW_ROUTE(app, "/api/v1/variables").methods("POST"_method)(
      [this](const crow::request& req) -> crow::response {
        try {
          auto rcCtx = authenticate(_amMiddleware, req);
          requirePermission(rcCtx, Permissions::kVariablesCreate);

          auto jBody = nlohmann::json::parse(req.body);
          std::string sName = jBody.value("name", "");
          std::string sValue = jBody.value("value", "");
          std::string sType = jBody.value("type", "");
          std::string sScope = jBody.value("scope", "global");

          RequestValidator::validateVariableName(sName);
          RequestValidator::validateVariableValue(sValue);
          RequestValidator::validateRequired(sType, "type");

          std::optional<int64_t> oZoneId;
          if (jBody.contains("zone_id") && !jBody["zone_id"].is_null()) {
            oZoneId = jBody["zone_id"].get<int64_t>();
          }

          std::string sVariableKind = "static";
          if (jBody.contains("variable_kind")) {
            sVariableKind = jBody["variable_kind"].get<std::string>();
            if (sVariableKind != "static" && sVariableKind != "dynamic") {
              return invalidJsonResponse();
            }
          }
          std::optional<std::string> osDynamicFormat = std::nullopt;
          if (jBody.contains("dynamic_format") && !jBody["dynamic_format"].is_null()) {
            osDynamicFormat = jBody["dynamic_format"].get<std::string>();
          }
          if (sVariableKind == "dynamic" && !osDynamicFormat.has_value()) {
            throw common::ValidationError("INVALID_VARIABLE",
                                          "dynamic_format is required for dynamic variables");
          }
          if (sVariableKind == "static" && osDynamicFormat.has_value()) {
            throw common::ValidationError("INVALID_VARIABLE",
                                          "dynamic_format is not allowed for static variables");
          }

          int64_t iId = _varRepo.create(sName, sValue, sType, sScope, oZoneId, sVariableKind,
                                        osDynamicFormat);
          return jsonResponse(201, {{"id", iId}});
        } catch (const common::AppError& e) {
          return errorResponse(e);
        } catch (const nlohmann::json::exception&) {
          return invalidJsonResponse();
        }
      });

  // GET /api/v1/variables/<int>
  CROW_ROUTE(app, "/api/v1/variables/<int>").methods("GET"_method)(
      [this](const crow::request& req, int iId) -> crow::response {
        try {
          auto rcCtx = authenticate(_amMiddleware, req);
          requirePermission(rcCtx, Permissions::kVariablesView);

          auto oRow = _varRepo.findById(iId);
          if (!oRow.has_value()) {
            throw common::NotFoundError("VARIABLE_NOT_FOUND", "Variable not found");
          }
          return jsonResponse(200, variableRowToJson(*oRow));
        } catch (const common::AppError& e) {
          return errorResponse(e);
        }
      });

  // PUT /api/v1/variables/<int>
  CROW_ROUTE(app, "/api/v1/variables/<int>").methods("PUT"_method)(
      [this](const crow::request& req, int iId) -> crow::response {
        try {
          auto rcCtx = authenticate(_amMiddleware, req);
          requirePermission(rcCtx, Permissions::kVariablesEdit);

          auto jBody = nlohmann::json::parse(req.body);
          std::string sValue = jBody.value("value", "");

          RequestValidator::validateVariableValue(sValue);

          std::optional<std::string> osDynamicFormat = std::nullopt;
          if (jBody.contains("dynamic_format") && !jBody["dynamic_format"].is_null()) {
            osDynamicFormat = jBody["dynamic_format"].get<std::string>();
          }

          _varRepo.update(iId, sValue, osDynamicFormat);
          return jsonResponse(200, {{"message", "Variable updated"}});
        } catch (const common::AppError& e) {
          return errorResponse(e);
        } catch (const nlohmann::json::exception&) {
          return invalidJsonResponse();
        }
      });

  // DELETE /api/v1/variables/<int>
  CROW_ROUTE(app, "/api/v1/variables/<int>").methods("DELETE"_method)(
      [this](const crow::request& req, int iId) -> crow::response {
        try {
          auto rcCtx = authenticate(_amMiddleware, req);
          requirePermission(rcCtx, Permissions::kVariablesDelete);

          _varRepo.deleteById(iId);
          return jsonResponse(200, {{"message", "Variable deleted"}});
        } catch (const common::AppError& e) {
          return errorResponse(e);
        }
      });
}

}  // namespace dns::api::routes
