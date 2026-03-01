#include "api/routes/ZoneRoutes.hpp"

#include "api/AuthMiddleware.hpp"
#include "common/Errors.hpp"
#include "dal/ZoneRepository.hpp"

#include <nlohmann/json.hpp>

namespace dns::api::routes {

ZoneRoutes::ZoneRoutes(dns::dal::ZoneRepository& zrRepo,
                       const dns::api::AuthMiddleware& amMiddleware)
    : _zrRepo(zrRepo), _amMiddleware(amMiddleware) {}
ZoneRoutes::~ZoneRoutes() = default;

void ZoneRoutes::registerRoutes(crow::SimpleApp& app) {
  // GET /api/v1/zones
  CROW_ROUTE(app, "/api/v1/zones").methods("GET"_method)(
      [this](const crow::request& req) -> crow::response {
        try {
          std::string sAuth = req.get_header_value("Authorization");
          std::string sApiKey = req.get_header_value("X-API-Key");
          _amMiddleware.authenticate(sAuth, sApiKey);

          std::optional<int64_t> oViewId;
          auto* pViewId = req.url_params.get("view_id");
          if (pViewId) {
            oViewId = std::stoll(pViewId);
          }

          auto vZones = _zrRepo.list(oViewId);
          nlohmann::json jArr = nlohmann::json::array();
          for (const auto& z : vZones) {
            nlohmann::json jZone = {{"id", z.iId}, {"name", z.sName},
                                    {"view_id", z.iViewId},
                                    {"created_at", z.sCreatedAt}};
            if (z.oDeploymentRetention.has_value()) {
              jZone["deployment_retention"] = *z.oDeploymentRetention;
            } else {
              jZone["deployment_retention"] = nullptr;
            }
            jArr.push_back(jZone);
          }
          crow::response resp(200, jArr.dump(2));
          resp.set_header("Content-Type", "application/json");
          return resp;
        } catch (const common::AppError& e) {
          nlohmann::json jErr = {{"error", e._sErrorCode},
                                 {"message", e.what()}};
          return crow::response(e._iHttpStatus, jErr.dump(2));
        }
      });

  // POST /api/v1/zones
  CROW_ROUTE(app, "/api/v1/zones").methods("POST"_method)(
      [this](const crow::request& req) -> crow::response {
        try {
          std::string sAuth = req.get_header_value("Authorization");
          std::string sApiKey = req.get_header_value("X-API-Key");
          auto rcCtx = _amMiddleware.authenticate(sAuth, sApiKey);
          if (rcCtx.sRole != "admin") {
            throw common::AuthorizationError("insufficient_role",
                                             "Admin role required");
          }

          auto jBody = nlohmann::json::parse(req.body);
          std::string sName = jBody.value("name", "");
          int64_t iViewId = jBody.value("view_id", static_cast<int64_t>(0));

          if (sName.empty() || iViewId == 0) {
            throw common::ValidationError("missing_fields",
                                          "name and view_id are required");
          }

          std::optional<int> oRetention;
          if (jBody.contains("deployment_retention") &&
              !jBody["deployment_retention"].is_null()) {
            oRetention = jBody["deployment_retention"].get<int>();
          }

          int64_t iId = _zrRepo.create(sName, iViewId, oRetention);
          nlohmann::json jResp = {{"id", iId}, {"name", sName},
                                  {"view_id", iViewId}};
          if (oRetention.has_value()) {
            jResp["deployment_retention"] = *oRetention;
          }
          crow::response resp(201, jResp.dump(2));
          resp.set_header("Content-Type", "application/json");
          return resp;
        } catch (const common::AppError& e) {
          nlohmann::json jErr = {{"error", e._sErrorCode},
                                 {"message", e.what()}};
          return crow::response(e._iHttpStatus, jErr.dump(2));
        } catch (const nlohmann::json::exception&) {
          nlohmann::json jErr = {{"error", "invalid_json"},
                                 {"message", "Invalid JSON body"}};
          return crow::response(400, jErr.dump(2));
        }
      });

  // GET /api/v1/zones/<int>
  CROW_ROUTE(app, "/api/v1/zones/<int>").methods("GET"_method)(
      [this](const crow::request& req, int iId) -> crow::response {
        try {
          std::string sAuth = req.get_header_value("Authorization");
          std::string sApiKey = req.get_header_value("X-API-Key");
          _amMiddleware.authenticate(sAuth, sApiKey);

          auto oZone = _zrRepo.findById(iId);
          if (!oZone.has_value()) {
            throw common::NotFoundError("zone_not_found", "Zone not found");
          }

          nlohmann::json jResp = {{"id", oZone->iId}, {"name", oZone->sName},
                                  {"view_id", oZone->iViewId},
                                  {"created_at", oZone->sCreatedAt}};
          if (oZone->oDeploymentRetention.has_value()) {
            jResp["deployment_retention"] = *oZone->oDeploymentRetention;
          } else {
            jResp["deployment_retention"] = nullptr;
          }
          crow::response resp(200, jResp.dump(2));
          resp.set_header("Content-Type", "application/json");
          return resp;
        } catch (const common::AppError& e) {
          nlohmann::json jErr = {{"error", e._sErrorCode},
                                 {"message", e.what()}};
          return crow::response(e._iHttpStatus, jErr.dump(2));
        }
      });

  // PUT /api/v1/zones/<int>
  CROW_ROUTE(app, "/api/v1/zones/<int>").methods("PUT"_method)(
      [this](const crow::request& req, int iId) -> crow::response {
        try {
          std::string sAuth = req.get_header_value("Authorization");
          std::string sApiKey = req.get_header_value("X-API-Key");
          auto rcCtx = _amMiddleware.authenticate(sAuth, sApiKey);
          if (rcCtx.sRole != "admin") {
            throw common::AuthorizationError("insufficient_role",
                                             "Admin role required");
          }

          auto jBody = nlohmann::json::parse(req.body);
          std::string sName = jBody.value("name", "");
          int64_t iViewId = jBody.value("view_id", static_cast<int64_t>(0));

          if (sName.empty() || iViewId == 0) {
            throw common::ValidationError("missing_fields",
                                          "name and view_id are required");
          }

          std::optional<int> oRetention;
          if (jBody.contains("deployment_retention") &&
              !jBody["deployment_retention"].is_null()) {
            oRetention = jBody["deployment_retention"].get<int>();
          }

          _zrRepo.update(iId, sName, iViewId, oRetention);
          nlohmann::json jResp = {{"id", iId}, {"name", sName},
                                  {"view_id", iViewId}};
          if (oRetention.has_value()) {
            jResp["deployment_retention"] = *oRetention;
          }
          crow::response resp(200, jResp.dump(2));
          resp.set_header("Content-Type", "application/json");
          return resp;
        } catch (const common::AppError& e) {
          nlohmann::json jErr = {{"error", e._sErrorCode},
                                 {"message", e.what()}};
          return crow::response(e._iHttpStatus, jErr.dump(2));
        } catch (const nlohmann::json::exception&) {
          nlohmann::json jErr = {{"error", "invalid_json"},
                                 {"message", "Invalid JSON body"}};
          return crow::response(400, jErr.dump(2));
        }
      });

  // DELETE /api/v1/zones/<int>
  CROW_ROUTE(app, "/api/v1/zones/<int>").methods("DELETE"_method)(
      [this](const crow::request& req, int iId) -> crow::response {
        try {
          std::string sAuth = req.get_header_value("Authorization");
          std::string sApiKey = req.get_header_value("X-API-Key");
          auto rcCtx = _amMiddleware.authenticate(sAuth, sApiKey);
          if (rcCtx.sRole != "admin") {
            throw common::AuthorizationError("insufficient_role",
                                             "Admin role required");
          }

          _zrRepo.deleteById(iId);
          return crow::response(204);
        } catch (const common::AppError& e) {
          nlohmann::json jErr = {{"error", e._sErrorCode},
                                 {"message", e.what()}};
          return crow::response(e._iHttpStatus, jErr.dump(2));
        }
      });
}

}  // namespace dns::api::routes
