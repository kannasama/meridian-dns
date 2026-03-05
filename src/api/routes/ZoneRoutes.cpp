#include "api/routes/ZoneRoutes.hpp"

#include "api/AuthMiddleware.hpp"
#include "api/RouteHelpers.hpp"
#include "common/Errors.hpp"
#include "dal/ZoneRepository.hpp"

#include <nlohmann/json.hpp>

namespace dns::api::routes {

ZoneRoutes::ZoneRoutes(dns::dal::ZoneRepository& zrRepo,
                       const dns::api::AuthMiddleware& amMiddleware)
    : _zrRepo(zrRepo), _amMiddleware(amMiddleware) {}

ZoneRoutes::~ZoneRoutes() = default;

namespace {

nlohmann::json zoneRowToJson(const dns::dal::ZoneRow& row) {
  nlohmann::json j = {
      {"id", row.iId},
      {"name", row.sName},
      {"view_id", row.iViewId},
      {"created_at", std::chrono::duration_cast<std::chrono::seconds>(
                         row.tpCreatedAt.time_since_epoch())
                         .count()},
  };
  if (row.oDeploymentRetention.has_value()) {
    j["deployment_retention"] = *row.oDeploymentRetention;
  } else {
    j["deployment_retention"] = nullptr;
  }
  return j;
}

}  // namespace

void ZoneRoutes::registerRoutes(crow::SimpleApp& app) {
  // GET /api/v1/zones
  CROW_ROUTE(app, "/api/v1/zones").methods("GET"_method)(
      [this](const crow::request& req) -> crow::response {
        try {
          auto rcCtx = authenticate(_amMiddleware, req);
          requireRole(rcCtx, "viewer");

          // Check for ?view_id= query parameter
          auto sViewId = req.url_params.get("view_id");
          std::vector<dns::dal::ZoneRow> vRows;
          if (sViewId) {
            vRows = _zrRepo.listByViewId(std::stoll(sViewId));
          } else {
            vRows = _zrRepo.listAll();
          }

          nlohmann::json jArr = nlohmann::json::array();
          for (const auto& row : vRows) {
            jArr.push_back(zoneRowToJson(row));
          }
          return jsonResponse(200, jArr);
        } catch (const common::AppError& e) {
          return errorResponse(e);
        }
      });

  // POST /api/v1/zones
  CROW_ROUTE(app, "/api/v1/zones").methods("POST"_method)(
      [this](const crow::request& req) -> crow::response {
        try {
          auto rcCtx = authenticate(_amMiddleware, req);
          requireRole(rcCtx, "admin");

          auto jBody = nlohmann::json::parse(req.body);
          std::string sName = jBody.value("name", "");
          int64_t iViewId = jBody.value("view_id", int64_t{0});

          if (sName.empty() || iViewId == 0) {
            throw common::ValidationError("MISSING_FIELDS",
                                          "name and view_id are required");
          }

          std::optional<int> oRetention;
          if (jBody.contains("deployment_retention") &&
              !jBody["deployment_retention"].is_null()) {
            oRetention = jBody["deployment_retention"].get<int>();
          }

          int64_t iId = _zrRepo.create(sName, iViewId, oRetention);
          return jsonResponse(201, {{"id", iId}});
        } catch (const common::AppError& e) {
          return errorResponse(e);
        } catch (const nlohmann::json::exception&) {
          return invalidJsonResponse();
        }
      });

  // GET /api/v1/zones/<int>
  CROW_ROUTE(app, "/api/v1/zones/<int>").methods("GET"_method)(
      [this](const crow::request& req, int iId) -> crow::response {
        try {
          auto rcCtx = authenticate(_amMiddleware, req);
          requireRole(rcCtx, "viewer");

          auto oRow = _zrRepo.findById(iId);
          if (!oRow.has_value()) {
            throw common::NotFoundError("ZONE_NOT_FOUND", "Zone not found");
          }
          return jsonResponse(200, zoneRowToJson(*oRow));
        } catch (const common::AppError& e) {
          return errorResponse(e);
        }
      });

  // PUT /api/v1/zones/<int>
  CROW_ROUTE(app, "/api/v1/zones/<int>").methods("PUT"_method)(
      [this](const crow::request& req, int iId) -> crow::response {
        try {
          auto rcCtx = authenticate(_amMiddleware, req);
          requireRole(rcCtx, "admin");

          auto jBody = nlohmann::json::parse(req.body);
          std::string sName = jBody.value("name", "");

          if (sName.empty()) {
            throw common::ValidationError("MISSING_FIELDS", "name is required");
          }

          std::optional<int> oRetention;
          if (jBody.contains("deployment_retention") &&
              !jBody["deployment_retention"].is_null()) {
            oRetention = jBody["deployment_retention"].get<int>();
          }

          _zrRepo.update(iId, sName, oRetention);
          return jsonResponse(200, {{"message", "Zone updated"}});
        } catch (const common::AppError& e) {
          return errorResponse(e);
        } catch (const nlohmann::json::exception&) {
          return invalidJsonResponse();
        }
      });

  // DELETE /api/v1/zones/<int>
  CROW_ROUTE(app, "/api/v1/zones/<int>").methods("DELETE"_method)(
      [this](const crow::request& req, int iId) -> crow::response {
        try {
          auto rcCtx = authenticate(_amMiddleware, req);
          requireRole(rcCtx, "admin");

          _zrRepo.deleteById(iId);
          return jsonResponse(200, {{"message", "Zone deleted"}});
        } catch (const common::AppError& e) {
          return errorResponse(e);
        }
      });
}

}  // namespace dns::api::routes
