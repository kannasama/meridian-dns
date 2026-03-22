// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Meridian DNS Contributors
// This file is part of Meridian DNS. See LICENSE for details.

#include "api/routes/SystemLogRoutes.hpp"

#include <chrono>
#include <optional>
#include <string>

#include <nlohmann/json.hpp>

#include "api/RouteHelpers.hpp"
#include "common/Errors.hpp"
#include "common/Permissions.hpp"
#include "dal/SystemLogRepository.hpp"

namespace dns::api::routes {
using namespace dns::common;

SystemLogRoutes::SystemLogRoutes(dns::dal::SystemLogRepository& slrRepo,
                                 const dns::api::AuthMiddleware& amMiddleware)
    : _slrRepo(slrRepo), _amMiddleware(amMiddleware) {}

SystemLogRoutes::~SystemLogRoutes() = default;

namespace {

nlohmann::json systemLogRowToJson(const dns::dal::SystemLogRow& row) {
  auto iEpoch = std::chrono::duration_cast<std::chrono::seconds>(
                    row.tpCreatedAt.time_since_epoch())
                    .count();

  nlohmann::json j = {
      {"id", row.iId},
      {"category", row.sCategory},
      {"severity", row.sSeverity},
      {"zone_id", row.oZoneId ? nlohmann::json(*row.oZoneId) : nlohmann::json(nullptr)},
      {"provider_id",
       row.oProviderId ? nlohmann::json(*row.oProviderId) : nlohmann::json(nullptr)},
      {"operation",
       row.osOperation ? nlohmann::json(*row.osOperation) : nlohmann::json(nullptr)},
      {"record_name",
       row.osRecordName ? nlohmann::json(*row.osRecordName) : nlohmann::json(nullptr)},
      {"record_type",
       row.osRecordType ? nlohmann::json(*row.osRecordType) : nlohmann::json(nullptr)},
      {"success", row.obSuccess ? nlohmann::json(*row.obSuccess) : nlohmann::json(nullptr)},
      {"status_code",
       row.oiStatusCode ? nlohmann::json(*row.oiStatusCode) : nlohmann::json(nullptr)},
      {"message", row.sMessage},
      {"detail", row.osDetail ? nlohmann::json(*row.osDetail) : nlohmann::json(nullptr)},
      {"created_at", iEpoch},
  };
  return j;
}

}  // namespace

void SystemLogRoutes::registerRoutes(crow::SimpleApp& app) {
  // GET /api/v1/system-logs
  CROW_ROUTE(app, "/api/v1/system-logs").methods("GET"_method)(
      [this](const crow::request& req) -> crow::response {
        try {
          auto rcCtx = authenticate(_amMiddleware, req);
          requirePermission(rcCtx, Permissions::kSystemLogsView);

          std::optional<std::string> osCategory;
          std::optional<std::string> osSeverity;
          std::optional<int64_t> oZoneId;
          std::optional<int64_t> oProviderId;
          std::optional<std::chrono::system_clock::time_point> otpFrom;
          std::optional<std::chrono::system_clock::time_point> otpTo;
          int iLimit = 200;

          auto pCategory = req.url_params.get("category");
          if (pCategory) osCategory = std::string(pCategory);

          auto pSeverity = req.url_params.get("severity");
          if (pSeverity) osSeverity = std::string(pSeverity);

          auto pZoneId = req.url_params.get("zone_id");
          if (pZoneId) oZoneId = std::stoll(pZoneId);

          auto pProviderId = req.url_params.get("provider_id");
          if (pProviderId) oProviderId = std::stoll(pProviderId);

          auto pFrom = req.url_params.get("from");
          if (pFrom) {
            auto iEpoch = std::stoll(pFrom);
            otpFrom = std::chrono::system_clock::time_point(std::chrono::seconds(iEpoch));
          }

          auto pTo = req.url_params.get("to");
          if (pTo) {
            auto iEpoch = std::stoll(pTo);
            otpTo = std::chrono::system_clock::time_point(std::chrono::seconds(iEpoch));
          }

          auto pLimit = req.url_params.get("limit");
          if (pLimit) {
            iLimit = std::stoi(pLimit);
            if (iLimit < 1) iLimit = 1;
            if (iLimit > 1000) iLimit = 1000;
          }

          auto vRows = _slrRepo.query(osCategory, osSeverity, oZoneId, oProviderId,
                                       otpFrom, otpTo, iLimit);

          nlohmann::json jArr = nlohmann::json::array();
          for (const auto& row : vRows) {
            jArr.push_back(systemLogRowToJson(row));
          }
          return jsonResponse(200, jArr);
        } catch (const common::AppError& e) {
          return errorResponse(e);
        }
      });
}

}  // namespace dns::api::routes
