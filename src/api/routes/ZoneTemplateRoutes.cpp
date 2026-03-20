// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Meridian DNS Contributors
// This file is part of Meridian DNS. See LICENSE for details.

#include "api/routes/ZoneTemplateRoutes.hpp"

#include "api/AuthMiddleware.hpp"
#include "api/RequestValidator.hpp"
#include "api/RouteHelpers.hpp"
#include "common/Errors.hpp"
#include "common/Permissions.hpp"
#include "common/Types.hpp"
#include "core/TemplateEngine.hpp"
#include "dal/AuditRepository.hpp"
#include "dal/RecordRepository.hpp"
#include "dal/SnippetRepository.hpp"
#include "dal/ZoneRepository.hpp"
#include "dal/ZoneTemplateRepository.hpp"

#include <nlohmann/json.hpp>

#include <set>
#include <string>
#include <vector>

namespace dns::api::routes {
using namespace dns::common;

ZoneTemplateRoutes::ZoneTemplateRoutes(dns::dal::ZoneTemplateRepository& ztrRepo,
                                       dns::dal::SnippetRepository&      snrRepo,
                                       dns::dal::ZoneRepository&         zrRepo,
                                       dns::dal::RecordRepository&       rrRepo,
                                       dns::dal::AuditRepository&        arRepo,
                                       const dns::api::AuthMiddleware&   amMiddleware)
    : _ztrRepo(ztrRepo), _snrRepo(snrRepo),
      _zrRepo(zrRepo), _rrRepo(rrRepo), _arRepo(arRepo),
      _amMiddleware(amMiddleware) {}

ZoneTemplateRoutes::~ZoneTemplateRoutes() = default;

namespace {

nlohmann::json templateToJson(const dns::dal::ZoneTemplateRow& t, bool bWithSnippets) {
  nlohmann::json j = {
    {"id",          t.iId},
    {"name",        t.sName},
    {"description", t.sDescription},
    {"soa_preset_id", t.oSoaPresetId.has_value()
                          ? nlohmann::json(*t.oSoaPresetId)
                          : nlohmann::json(nullptr)},
    {"created_at",  std::chrono::duration_cast<std::chrono::seconds>(
                        t.tpCreatedAt.time_since_epoch()).count()},
    {"updated_at",  std::chrono::duration_cast<std::chrono::seconds>(
                        t.tpUpdatedAt.time_since_epoch()).count()},
  };
  if (bWithSnippets) {
    nlohmann::json jIds = nlohmann::json::array();
    for (const auto& iSnippetId : t.vSnippetIds) {
      jIds.push_back(iSnippetId);
    }
    j["snippet_ids"] = jIds;
  }
  return j;
}

nlohmann::json diffEntryToJson(const dns::common::RecordDiff& d) {
  std::string sAction;
  switch (d.action) {
    case DiffAction::Add:    sAction = "add";    break;
    case DiffAction::Update: sAction = "update"; break;
    case DiffAction::Delete: sAction = "delete"; break;
    case DiffAction::Drift:  sAction = "drift";  break;
  }
  return {
    {"action",         sAction},
    {"name",           d.sName},
    {"type",           d.sType},
    {"ttl",            d.uTtl},
    {"priority",       d.iPriority},
    {"source_value",   d.sSourceValue},
    {"provider_value", d.sProviderValue},
  };
}

nlohmann::json previewResultToJson(const dns::common::PreviewResult& pr) {
  nlohmann::json jDiffs = nlohmann::json::array();
  for (const auto& d : pr.vDiffs) {
    jDiffs.push_back(diffEntryToJson(d));
  }
  return {
    {"zone_id",   pr.iZoneId},
    {"zone_name", pr.sZoneName},
    {"has_drift", pr.bHasDrift},
    {"diffs",     jDiffs},
  };
}

/// Build an expected records vector from all snippet IDs in a template.
std::vector<dns::dal::SnippetRecordRow> buildExpectedRecords(
    dns::dal::SnippetRepository& snrRepo,
    const std::vector<int64_t>& vSnippetIds) {
  std::vector<dns::dal::SnippetRecordRow> vExpected;
  for (const auto& iSnippetId : vSnippetIds) {
    auto vRecords = snrRepo.listRecords(iSnippetId);
    for (auto& r : vRecords) {
      vExpected.push_back(std::move(r));
    }
  }
  return vExpected;
}

}  // namespace

dns::common::PreviewResult ZoneTemplateRoutes::runComplianceCheck(int64_t iZoneId) {
  auto oZone = _zrRepo.findById(iZoneId);
  if (!oZone.has_value()) {
    throw common::NotFoundError("ZONE_NOT_FOUND", "Zone not found");
  }

  if (!oZone->oTemplateId.has_value()) {
    throw common::ValidationError("ZONE_NOT_LINKED", "Zone has no template linked");
  }

  auto oTemplate = _ztrRepo.findById(*oZone->oTemplateId);
  if (!oTemplate.has_value()) {
    throw common::NotFoundError("TEMPLATE_NOT_FOUND", "Template not found");
  }

  auto vExpected = buildExpectedRecords(_snrRepo, oTemplate->vSnippetIds);
  auto vCurrent  = _rrRepo.listByZoneId(iZoneId);

  return dns::core::TemplateEngine::computeComplianceDiff(
      iZoneId, oZone->sName, vExpected, vCurrent);
}

void ZoneTemplateRoutes::registerRoutes(crow::SimpleApp& app) {

  // GET /api/v1/templates
  CROW_ROUTE(app, "/api/v1/templates").methods("GET"_method)(
      [this](const crow::request& req) -> crow::response {
        try {
          auto rcCtx = authenticate(_amMiddleware, req);
          requirePermission(rcCtx, Permissions::kTemplatesView);

          auto vRows = _ztrRepo.listAll();
          nlohmann::json jArr = nlohmann::json::array();
          for (const auto& row : vRows) {
            jArr.push_back(templateToJson(row, false));
          }
          return jsonResponse(200, jArr);
        } catch (const common::AppError& e) {
          return errorResponse(e);
        }
      });

  // POST /api/v1/templates
  CROW_ROUTE(app, "/api/v1/templates").methods("POST"_method)(
      [this](const crow::request& req) -> crow::response {
        try {
          auto rcCtx = authenticate(_amMiddleware, req);
          requirePermission(rcCtx, Permissions::kTemplatesCreate);
          enforceBodyLimit(req);

          auto jBody = nlohmann::json::parse(req.body);
          std::string sName = jBody.value("name", "");
          std::string sDescription = jBody.value("description", "");

          if (sName.empty()) {
            throw common::ValidationError("MISSING_FIELDS", "name is required");
          }
          dns::api::RequestValidator::validateStringLength(sName, "name", 255);

          std::optional<int64_t> oSoaPresetId;
          if (jBody.contains("soa_preset_id") && !jBody["soa_preset_id"].is_null()) {
            oSoaPresetId = jBody["soa_preset_id"].get<int64_t>();
          }

          int64_t iId = _ztrRepo.create(sName, sDescription, oSoaPresetId);

          if (jBody.contains("snippet_ids") && jBody["snippet_ids"].is_array()) {
            std::vector<int64_t> vSnippetIds;
            for (const auto& jSnippetId : jBody["snippet_ids"]) {
              vSnippetIds.push_back(jSnippetId.get<int64_t>());
            }
            _ztrRepo.setSnippets(iId, vSnippetIds);
          }

          return jsonResponse(201, {{"id", iId}});
        } catch (const common::AppError& e) {
          return errorResponse(e);
        } catch (const nlohmann::json::exception&) {
          return invalidJsonResponse();
        }
      });

  // GET /api/v1/templates/<int>
  CROW_ROUTE(app, "/api/v1/templates/<int>").methods("GET"_method)(
      [this](const crow::request& req, int iId) -> crow::response {
        try {
          auto rcCtx = authenticate(_amMiddleware, req);
          requirePermission(rcCtx, Permissions::kTemplatesView);

          auto oRow = _ztrRepo.findById(static_cast<int64_t>(iId));
          if (!oRow.has_value()) {
            throw common::NotFoundError("TEMPLATE_NOT_FOUND", "Template not found");
          }
          return jsonResponse(200, templateToJson(*oRow, true));
        } catch (const common::AppError& e) {
          return errorResponse(e);
        }
      });

  // PUT /api/v1/templates/<int>
  CROW_ROUTE(app, "/api/v1/templates/<int>").methods("PUT"_method)(
      [this](const crow::request& req, int iId) -> crow::response {
        try {
          auto rcCtx = authenticate(_amMiddleware, req);
          requirePermission(rcCtx, Permissions::kTemplatesEdit);
          enforceBodyLimit(req);

          auto jBody = nlohmann::json::parse(req.body);
          std::string sName = jBody.value("name", "");
          std::string sDescription = jBody.value("description", "");

          if (sName.empty()) {
            throw common::ValidationError("MISSING_FIELDS", "name is required");
          }
          dns::api::RequestValidator::validateStringLength(sName, "name", 255);

          std::optional<int64_t> oSoaPresetId;
          if (jBody.contains("soa_preset_id") && !jBody["soa_preset_id"].is_null()) {
            oSoaPresetId = jBody["soa_preset_id"].get<int64_t>();
          }

          _ztrRepo.update(static_cast<int64_t>(iId), sName, sDescription, oSoaPresetId);

          if (jBody.contains("snippet_ids") && jBody["snippet_ids"].is_array()) {
            std::vector<int64_t> vSnippetIds;
            for (const auto& jSnippetId : jBody["snippet_ids"]) {
              vSnippetIds.push_back(jSnippetId.get<int64_t>());
            }
            _ztrRepo.setSnippets(static_cast<int64_t>(iId), vSnippetIds);
          }

          return jsonResponse(200, {{"message", "Template updated"}});
        } catch (const common::AppError& e) {
          return errorResponse(e);
        } catch (const nlohmann::json::exception&) {
          return invalidJsonResponse();
        }
      });

  // DELETE /api/v1/templates/<int>
  CROW_ROUTE(app, "/api/v1/templates/<int>").methods("DELETE"_method)(
      [this](const crow::request& req, int iId) -> crow::response {
        try {
          auto rcCtx = authenticate(_amMiddleware, req);
          requirePermission(rcCtx, Permissions::kTemplatesDelete);

          _ztrRepo.deleteById(static_cast<int64_t>(iId));
          return jsonResponse(200, {{"message", "Template deleted"}});
        } catch (const common::AppError& e) {
          return errorResponse(e);
        }
      });

  // POST /api/v1/zones/<int>/template/push
  CROW_ROUTE(app, "/api/v1/zones/<int>/template/push").methods("POST"_method)(
      [this](const crow::request& req, int iZoneId) -> crow::response {
        try {
          auto rcCtx = authenticate(_amMiddleware, req);
          requirePermission(rcCtx, Permissions::kZonesDeploy);
          enforceBodyLimit(req);

          auto jBody = nlohmann::json::parse(req.body);

          if (!jBody.contains("template_id")) {
            throw common::ValidationError("MISSING_FIELDS", "template_id is required");
          }
          int64_t iTemplateId = jBody["template_id"].get<int64_t>();
          bool bLink = jBody.value("link", true);

          auto oTemplate = _ztrRepo.findById(iTemplateId);
          if (!oTemplate.has_value()) {
            throw common::NotFoundError("TEMPLATE_NOT_FOUND", "Template not found");
          }

          auto oZone = _zrRepo.findById(static_cast<int64_t>(iZoneId));
          if (!oZone.has_value()) {
            throw common::NotFoundError("ZONE_NOT_FOUND", "Zone not found");
          }

          auto vExpected = buildExpectedRecords(_snrRepo, oTemplate->vSnippetIds);

          if (bLink) {
            _zrRepo.setTemplateLink(static_cast<int64_t>(iZoneId), iTemplateId);

            auto vCurrent = _rrRepo.listByZoneId(static_cast<int64_t>(iZoneId));

            auto prResult = dns::core::TemplateEngine::computeComplianceDiff(
                static_cast<int64_t>(iZoneId), oZone->sName, vExpected, vCurrent);

            return jsonResponse(200, previewResultToJson(prResult));
          } else {
            // One-shot apply without linking
            int iRecordsApplied = 0;
            for (const auto& record : vExpected) {
              _rrRepo.create(static_cast<int64_t>(iZoneId), record.sName, record.sType,
                             record.iTtl, record.sValueTemplate, record.iPriority);
              ++iRecordsApplied;
            }
            return jsonResponse(200, {
              {"message",          "Template applied"},
              {"records_applied",  iRecordsApplied},
            });
          }
        } catch (const common::AppError& e) {
          return errorResponse(e);
        } catch (const nlohmann::json::exception&) {
          return invalidJsonResponse();
        }
      });

  // GET /api/v1/zones/<int>/template/check
  CROW_ROUTE(app, "/api/v1/zones/<int>/template/check").methods("GET"_method)(
      [this](const crow::request& req, int iZoneId) -> crow::response {
        try {
          auto rcCtx = authenticate(_amMiddleware, req);
          requirePermission(rcCtx, Permissions::kZonesView);

          auto prResult = runComplianceCheck(static_cast<int64_t>(iZoneId));
          return jsonResponse(200, previewResultToJson(prResult));
        } catch (const common::AppError& e) {
          return errorResponse(e);
        }
      });

  // POST /api/v1/zones/<int>/template/apply
  CROW_ROUTE(app, "/api/v1/zones/<int>/template/apply").methods("POST"_method)(
      [this](const crow::request& req, int iZoneId) -> crow::response {
        try {
          auto rcCtx = authenticate(_amMiddleware, req);
          requirePermission(rcCtx, Permissions::kRecordsCreate);
          enforceBodyLimit(req);

          // Parse body before DB calls
          auto jBody = nlohmann::json::parse(req.body);

          // Build set of requested (name, type) pairs
          std::set<std::string> requestedKeys;
          if (jBody.contains("records") && jBody["records"].is_array()) {
            for (const auto& jr : jBody["records"]) {
              std::string sKey = jr.value("name", "") + '\0' + jr.value("type", "");
              requestedKeys.insert(sKey);
            }
          }

          // Re-run compliance check
          auto prResult = runComplianceCheck(static_cast<int64_t>(iZoneId));

          // Need current records for update path
          auto vCurrent = _rrRepo.listByZoneId(static_cast<int64_t>(iZoneId));

          int iApplied = 0;
          for (const auto& diff : prResult.vDiffs) {
            std::string sKey = diff.sName + '\0' + diff.sType;
            if (requestedKeys.find(sKey) == requestedKeys.end()) {
              continue;
            }

            if (diff.action == DiffAction::Add) {
              _rrRepo.create(static_cast<int64_t>(iZoneId), diff.sName, diff.sType,
                             static_cast<int>(diff.uTtl), diff.sSourceValue, diff.iPriority);
              ++iApplied;
            } else if (diff.action == DiffAction::Update) {
              // Find existing record in vCurrent by name+type
              for (const auto& existing : vCurrent) {
                if (existing.sName == diff.sName && existing.sType == diff.sType) {
                  _rrRepo.update(existing.iId, diff.sName, diff.sType,
                                 static_cast<int>(diff.uTtl), diff.sSourceValue, diff.iPriority);
                  ++iApplied;
                  break;
                }
              }
            }
          }

          _zrRepo.clearTemplateCheckPending(static_cast<int64_t>(iZoneId));

          return jsonResponse(200, {{"message", "Compliance records applied"}});
        } catch (const common::AppError& e) {
          return errorResponse(e);
        } catch (const nlohmann::json::exception&) {
          return invalidJsonResponse();
        }
      });

  // DELETE /api/v1/zones/<int>/template  (unlink)
  CROW_ROUTE(app, "/api/v1/zones/<int>/template").methods("DELETE"_method)(
      [this](const crow::request& req, int iZoneId) -> crow::response {
        try {
          auto rcCtx = authenticate(_amMiddleware, req);
          requirePermission(rcCtx, Permissions::kZonesEdit);

          _zrRepo.setTemplateLink(static_cast<int64_t>(iZoneId), std::nullopt);
          _zrRepo.clearTemplateCheckPending(static_cast<int64_t>(iZoneId));

          return jsonResponse(200, {{"message", "Template unlinked"}});
        } catch (const common::AppError& e) {
          return errorResponse(e);
        }
      });
}

}  // namespace dns::api::routes
