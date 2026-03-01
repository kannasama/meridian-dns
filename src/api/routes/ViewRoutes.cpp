#include "api/routes/ViewRoutes.hpp"

#include "api/AuthMiddleware.hpp"
#include "common/Errors.hpp"
#include "dal/ViewRepository.hpp"

#include <nlohmann/json.hpp>

namespace dns::api::routes {

ViewRoutes::ViewRoutes(dns::dal::ViewRepository& vrRepo,
                       const dns::api::AuthMiddleware& amMiddleware)
    : _vrRepo(vrRepo), _amMiddleware(amMiddleware) {}
ViewRoutes::~ViewRoutes() = default;

void ViewRoutes::registerRoutes(crow::SimpleApp& app) {
  // GET /api/v1/views
  CROW_ROUTE(app, "/api/v1/views").methods("GET"_method)(
      [this](const crow::request& req) -> crow::response {
        try {
          std::string sAuth = req.get_header_value("Authorization");
          std::string sApiKey = req.get_header_value("X-API-Key");
          _amMiddleware.authenticate(sAuth, sApiKey);

          auto vViews = _vrRepo.list();
          nlohmann::json jArr = nlohmann::json::array();
          for (const auto& v : vViews) {
            jArr.push_back({{"id", v.iId}, {"name", v.sName},
                            {"description", v.sDescription},
                            {"provider_ids", v.vProviderIds},
                            {"created_at", v.sCreatedAt}});
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

  // POST /api/v1/views
  CROW_ROUTE(app, "/api/v1/views").methods("POST"_method)(
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
          std::string sDesc = jBody.value("description", "");

          if (sName.empty()) {
            throw common::ValidationError("missing_fields",
                                          "name is required");
          }

          int64_t iId = _vrRepo.create(sName, sDesc);
          nlohmann::json jResp = {{"id", iId}, {"name", sName},
                                  {"description", sDesc}};
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

  // GET /api/v1/views/<int>
  CROW_ROUTE(app, "/api/v1/views/<int>").methods("GET"_method)(
      [this](const crow::request& req, int iId) -> crow::response {
        try {
          std::string sAuth = req.get_header_value("Authorization");
          std::string sApiKey = req.get_header_value("X-API-Key");
          _amMiddleware.authenticate(sAuth, sApiKey);

          auto oView = _vrRepo.findById(iId);
          if (!oView.has_value()) {
            throw common::NotFoundError("view_not_found", "View not found");
          }

          nlohmann::json jResp = {{"id", oView->iId}, {"name", oView->sName},
                                  {"description", oView->sDescription},
                                  {"provider_ids", oView->vProviderIds},
                                  {"created_at", oView->sCreatedAt}};
          crow::response resp(200, jResp.dump(2));
          resp.set_header("Content-Type", "application/json");
          return resp;
        } catch (const common::AppError& e) {
          nlohmann::json jErr = {{"error", e._sErrorCode},
                                 {"message", e.what()}};
          return crow::response(e._iHttpStatus, jErr.dump(2));
        }
      });

  // PUT /api/v1/views/<int>
  CROW_ROUTE(app, "/api/v1/views/<int>").methods("PUT"_method)(
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
          std::string sDesc = jBody.value("description", "");

          if (sName.empty()) {
            throw common::ValidationError("missing_fields",
                                          "name is required");
          }

          _vrRepo.update(iId, sName, sDesc);
          nlohmann::json jResp = {{"id", iId}, {"name", sName},
                                  {"description", sDesc}};
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

  // DELETE /api/v1/views/<int>
  CROW_ROUTE(app, "/api/v1/views/<int>").methods("DELETE"_method)(
      [this](const crow::request& req, int iId) -> crow::response {
        try {
          std::string sAuth = req.get_header_value("Authorization");
          std::string sApiKey = req.get_header_value("X-API-Key");
          auto rcCtx = _amMiddleware.authenticate(sAuth, sApiKey);
          if (rcCtx.sRole != "admin") {
            throw common::AuthorizationError("insufficient_role",
                                             "Admin role required");
          }

          _vrRepo.deleteById(iId);
          return crow::response(204);
        } catch (const common::AppError& e) {
          nlohmann::json jErr = {{"error", e._sErrorCode},
                                 {"message", e.what()}};
          return crow::response(e._iHttpStatus, jErr.dump(2));
        }
      });

  // POST /api/v1/views/<int>/providers/<int> — attach provider
  CROW_ROUTE(app, "/api/v1/views/<int>/providers/<int>").methods("POST"_method)(
      [this](const crow::request& req, int iViewId,
             int iProviderId) -> crow::response {
        try {
          std::string sAuth = req.get_header_value("Authorization");
          std::string sApiKey = req.get_header_value("X-API-Key");
          auto rcCtx = _amMiddleware.authenticate(sAuth, sApiKey);
          if (rcCtx.sRole != "admin") {
            throw common::AuthorizationError("insufficient_role",
                                             "Admin role required");
          }

          _vrRepo.attachProvider(iViewId, iProviderId);
          return crow::response(204);
        } catch (const common::AppError& e) {
          nlohmann::json jErr = {{"error", e._sErrorCode},
                                 {"message", e.what()}};
          return crow::response(e._iHttpStatus, jErr.dump(2));
        }
      });

  // DELETE /api/v1/views/<int>/providers/<int> — detach provider
  CROW_ROUTE(app, "/api/v1/views/<int>/providers/<int>")
      .methods("DELETE"_method)(
          [this](const crow::request& req, int iViewId,
                 int iProviderId) -> crow::response {
            try {
              std::string sAuth = req.get_header_value("Authorization");
              std::string sApiKey = req.get_header_value("X-API-Key");
              auto rcCtx = _amMiddleware.authenticate(sAuth, sApiKey);
              if (rcCtx.sRole != "admin") {
                throw common::AuthorizationError("insufficient_role",
                                                 "Admin role required");
              }

              _vrRepo.detachProvider(iViewId, iProviderId);
              return crow::response(204);
            } catch (const common::AppError& e) {
              nlohmann::json jErr = {{"error", e._sErrorCode},
                                     {"message", e.what()}};
              return crow::response(e._iHttpStatus, jErr.dump(2));
            }
          });
}

}  // namespace dns::api::routes
