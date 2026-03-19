// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Meridian DNS Contributors
// This file is part of Meridian DNS. See LICENSE for details.

#include "api/routes/SnippetRoutes.hpp"

#include "api/AuthMiddleware.hpp"
#include "api/RequestValidator.hpp"
#include "api/RouteHelpers.hpp"
#include "common/Errors.hpp"
#include "common/Permissions.hpp"
#include "dal/AuditRepository.hpp"
#include "dal/RecordRepository.hpp"
#include "dal/SnippetRepository.hpp"
#include "dal/ZoneRepository.hpp"

#include <nlohmann/json.hpp>

namespace dns::api::routes {
using namespace dns::common;

SnippetRoutes::SnippetRoutes(dns::dal::SnippetRepository& srRepo,
                              dns::dal::ZoneRepository& zrRepo,
                              dns::dal::RecordRepository& rrRepo,
                              dns::dal::AuditRepository& arRepo,
                              const dns::api::AuthMiddleware& amMiddleware)
    : _srRepo(srRepo), _zrRepo(zrRepo), _rrRepo(rrRepo), _arRepo(arRepo),
      _amMiddleware(amMiddleware) {}

SnippetRoutes::~SnippetRoutes() = default;

namespace {

nlohmann::json snippetRecordToJson(const dns::dal::SnippetRecordRow& r) {
  return {
    {"id", r.iId},
    {"name", r.sName},
    {"type", r.sType},
    {"ttl", r.iTtl},
    {"value_template", r.sValueTemplate},
    {"priority", r.iPriority},
    {"sort_order", r.iSortOrder},
  };
}

nlohmann::json snippetToJson(const dns::dal::SnippetRow& s, bool bWithRecords) {
  nlohmann::json j = {
    {"id", s.iId},
    {"name", s.sName},
    {"description", s.sDescription},
    {"created_at", std::chrono::duration_cast<std::chrono::seconds>(
                       s.tpCreatedAt.time_since_epoch()).count()},
    {"updated_at", std::chrono::duration_cast<std::chrono::seconds>(
                       s.tpUpdatedAt.time_since_epoch()).count()},
  };
  if (bWithRecords) {
    nlohmann::json jRecords = nlohmann::json::array();
    for (const auto& r : s.vRecords) {
      jRecords.push_back(snippetRecordToJson(r));
    }
    j["records"] = jRecords;
  }
  return j;
}

std::vector<dns::dal::SnippetRecordRow> parseRecords(const nlohmann::json& jRecords) {
  std::vector<dns::dal::SnippetRecordRow> vRecords;
  for (size_t i = 0; i < jRecords.size(); ++i) {
    const auto& jr = jRecords[i];
    dns::dal::SnippetRecordRow r;
    r.iId = 0;
    r.iSnippetId = 0;
    r.sName = jr.value("name", "");
    r.sType = jr.value("type", "");
    r.iTtl = jr.value("ttl", 300);
    r.sValueTemplate = jr.value("value_template", "");
    r.iPriority = jr.value("priority", 0);
    r.iSortOrder = jr.value("sort_order", static_cast<int>(i));
    if (r.sName.empty() || r.sType.empty() || r.sValueTemplate.empty()) {
      throw dns::common::ValidationError("MISSING_FIELDS",
                                         "record name, type, and value_template are required");
    }
    vRecords.push_back(r);
  }
  return vRecords;
}

}  // namespace

void SnippetRoutes::registerRoutes(crow::SimpleApp& app) {
  // GET /api/v1/snippets
  CROW_ROUTE(app, "/api/v1/snippets").methods("GET"_method)(
      [this](const crow::request& req) -> crow::response {
        try {
          auto rcCtx = authenticate(_amMiddleware, req);
          requirePermission(rcCtx, Permissions::kSnippetsView);

          auto vRows = _srRepo.listAll();
          nlohmann::json jArr = nlohmann::json::array();
          for (const auto& row : vRows) {
            jArr.push_back(snippetToJson(row, false));
          }
          return jsonResponse(200, jArr);
        } catch (const common::AppError& e) {
          return errorResponse(e);
        }
      });

  // POST /api/v1/snippets
  CROW_ROUTE(app, "/api/v1/snippets").methods("POST"_method)(
      [this](const crow::request& req) -> crow::response {
        try {
          auto rcCtx = authenticate(_amMiddleware, req);
          requirePermission(rcCtx, Permissions::kSnippetsCreate);
          enforceBodyLimit(req);

          auto jBody = nlohmann::json::parse(req.body);
          std::string sName = jBody.value("name", "");
          std::string sDescription = jBody.value("description", "");

          if (sName.empty()) {
            throw common::ValidationError("MISSING_FIELDS", "name is required");
          }
          dns::api::RequestValidator::validateStringLength(sName, "name", 255);

          int64_t iId = _srRepo.create(sName, sDescription);

          if (jBody.contains("records") && jBody["records"].is_array()) {
            auto vRecords = parseRecords(jBody["records"]);
            _srRepo.replaceRecords(iId, vRecords);
          }

          return jsonResponse(201, {{"id", iId}});
        } catch (const common::AppError& e) {
          return errorResponse(e);
        } catch (const nlohmann::json::exception&) {
          return invalidJsonResponse();
        }
      });

  // GET /api/v1/snippets/<int>
  CROW_ROUTE(app, "/api/v1/snippets/<int>").methods("GET"_method)(
      [this](const crow::request& req, int iId) -> crow::response {
        try {
          auto rcCtx = authenticate(_amMiddleware, req);
          requirePermission(rcCtx, Permissions::kSnippetsView);

          auto oRow = _srRepo.findById(static_cast<int64_t>(iId));
          if (!oRow.has_value()) {
            throw common::NotFoundError("SNIPPET_NOT_FOUND", "Snippet not found");
          }
          return jsonResponse(200, snippetToJson(*oRow, true));
        } catch (const common::AppError& e) {
          return errorResponse(e);
        }
      });

  // PUT /api/v1/snippets/<int>
  CROW_ROUTE(app, "/api/v1/snippets/<int>").methods("PUT"_method)(
      [this](const crow::request& req, int iId) -> crow::response {
        try {
          auto rcCtx = authenticate(_amMiddleware, req);
          requirePermission(rcCtx, Permissions::kSnippetsEdit);
          enforceBodyLimit(req);

          auto jBody = nlohmann::json::parse(req.body);
          std::string sName = jBody.value("name", "");
          std::string sDescription = jBody.value("description", "");

          if (sName.empty()) {
            throw common::ValidationError("MISSING_FIELDS", "name is required");
          }
          dns::api::RequestValidator::validateStringLength(sName, "name", 255);

          _srRepo.update(static_cast<int64_t>(iId), sName, sDescription);

          if (jBody.contains("records") && jBody["records"].is_array()) {
            auto vRecords = parseRecords(jBody["records"]);
            _srRepo.replaceRecords(static_cast<int64_t>(iId), vRecords);
          }

          return jsonResponse(200, {{"message", "Snippet updated"}});
        } catch (const common::AppError& e) {
          return errorResponse(e);
        } catch (const nlohmann::json::exception&) {
          return invalidJsonResponse();
        }
      });

  // DELETE /api/v1/snippets/<int>
  CROW_ROUTE(app, "/api/v1/snippets/<int>").methods("DELETE"_method)(
      [this](const crow::request& req, int iId) -> crow::response {
        try {
          auto rcCtx = authenticate(_amMiddleware, req);
          requirePermission(rcCtx, Permissions::kSnippetsDelete);

          _srRepo.deleteById(static_cast<int64_t>(iId));
          return jsonResponse(200, {{"message", "Snippet deleted"}});
        } catch (const common::AppError& e) {
          return errorResponse(e);
        }
      });

  // POST /api/v1/zones/<int>/snippets/<int>/apply
  CROW_ROUTE(app, "/api/v1/zones/<int>/snippets/<int>/apply").methods("POST"_method)(
      [this](const crow::request& req, int iZoneId, int iSnippetId) -> crow::response {
        try {
          auto rcCtx = authenticate(_amMiddleware, req);
          requirePermission(rcCtx, Permissions::kRecordsCreate);
          enforceBodyLimit(req);

          // Verify zone exists
          auto oZone = _zrRepo.findById(static_cast<int64_t>(iZoneId));
          if (!oZone.has_value()) {
            throw common::NotFoundError("ZONE_NOT_FOUND", "Zone not found");
          }

          // Verify snippet exists (with records)
          auto oSnippet = _srRepo.findById(static_cast<int64_t>(iSnippetId));
          if (!oSnippet.has_value()) {
            throw common::NotFoundError("SNIPPET_NOT_FOUND", "Snippet not found");
          }

          // Get current records for the zone
          auto vExisting = _rrRepo.listByZoneId(static_cast<int64_t>(iZoneId));

          // Apply each snippet record — update if same name+type exists, else create
          for (const auto& record : oSnippet->vRecords) {
            bool bFound = false;
            for (const auto& existing : vExisting) {
              if (existing.sName == record.sName && existing.sType == record.sType) {
                _rrRepo.update(existing.iId, record.sName, record.sType,
                               record.iTtl, record.sValueTemplate, record.iPriority);
                bFound = true;
                break;
              }
            }
            if (!bFound) {
              _rrRepo.create(static_cast<int64_t>(iZoneId), record.sName, record.sType,
                             record.iTtl, record.sValueTemplate, record.iPriority);
            }
          }

          // Audit log
          int iRecordsApplied = static_cast<int>(oSnippet->vRecords.size());
          nlohmann::json jNewValue = {
            {"snippet_id", iSnippetId},
            {"snippet_name", oSnippet->sName},
            {"records_applied", iRecordsApplied},
          };
          _arRepo.insert("zone", static_cast<int64_t>(iZoneId), "snippet_apply",
                         std::nullopt, jNewValue,
                         formatAuditIdentity(rcCtx), rcCtx.sAuthMethod, rcCtx.sIpAddress);

          return jsonResponse(200, {
            {"message", "Snippet applied"},
            {"records_applied", iRecordsApplied},
          });
        } catch (const common::AppError& e) {
          return errorResponse(e);
        }
      });
}

}  // namespace dns::api::routes
