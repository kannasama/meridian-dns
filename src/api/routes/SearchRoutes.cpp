// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Meridian DNS Contributors
// This file is part of Meridian DNS. See LICENSE for details.

#include "api/routes/SearchRoutes.hpp"

#include "api/AuthMiddleware.hpp"
#include "api/RouteHelpers.hpp"
#include "common/Errors.hpp"
#include "common/Permissions.hpp"
#include "dal/RecordRepository.hpp"

#include <nlohmann/json.hpp>
#include <optional>
#include <string>

namespace dns::api::routes {
using namespace dns::common;

SearchRoutes::SearchRoutes(dns::dal::RecordRepository& rrRepo,
                           const dns::api::AuthMiddleware& amMiddleware)
    : _rrRepo(rrRepo), _amMiddleware(amMiddleware) {}

SearchRoutes::~SearchRoutes() = default;

void SearchRoutes::registerRoutes(crow::SimpleApp& app) {
  // GET /api/v1/search/records
  CROW_ROUTE(app, "/api/v1/search/records").methods("GET"_method)(
      [this](const crow::request& req) -> crow::response {
        try {
          auto rcCtx = authenticate(_amMiddleware, req);
          requirePermission(rcCtx, Permissions::kRecordsView);

          const char* pQ = req.url_params.get("q");
          if (!pQ || std::string(pQ).empty()) {
            throw common::ValidationError("MISSING_QUERY",
                "Query parameter 'q' is required");
          }
          std::string sQuery = pQ;
          if (sQuery.size() > 200) {
            throw common::ValidationError("QUERY_TOO_LONG",
                "Query must be 200 characters or fewer");
          }

          std::optional<std::string> osType;
          if (auto pType = req.url_params.get("type")) {
            osType = std::string(pType);
          }

          std::optional<int64_t> oiZoneId;
          if (auto pZoneId = req.url_params.get("zone_id")) {
            oiZoneId = std::stoll(pZoneId);
          }

          std::optional<int64_t> oiViewId;
          if (auto pViewId = req.url_params.get("view_id")) {
            oiViewId = std::stoll(pViewId);
          }

          auto vResults = _rrRepo.search(sQuery, osType, oiZoneId, oiViewId);

          nlohmann::json jArr = nlohmann::json::array();
          for (const auto& sr : vResults) {
            jArr.push_back({
                {"id", sr.iId},
                {"zone_id", sr.iZoneId},
                {"zone_name", sr.sZoneName},
                {"view_name", sr.sViewName},
                {"name", sr.sName},
                {"type", sr.sType},
                {"ttl", sr.iTtl},
                {"value_template", sr.sValueTemplate},
                {"priority", sr.iPriority},
            });
          }
          return jsonResponse(200, jArr);
        } catch (const common::AppError& e) {
          return errorResponse(e);
        }
      });
}

}  // namespace dns::api::routes
