#include "api/routes/ProviderRoutes.hpp"

#include "api/AuthMiddleware.hpp"
#include "common/Errors.hpp"
#include "dal/ProviderRepository.hpp"

#include <nlohmann/json.hpp>

namespace dns::api::routes {

ProviderRoutes::ProviderRoutes(dns::dal::ProviderRepository& prRepo,
                               const dns::api::AuthMiddleware& amMiddleware)
    : _prRepo(prRepo), _amMiddleware(amMiddleware) {}
ProviderRoutes::~ProviderRoutes() = default;

void ProviderRoutes::registerRoutes(crow::SimpleApp& app) {
  // GET /api/v1/providers — list all providers (viewer)
  CROW_ROUTE(app, "/api/v1/providers").methods("GET"_method)(
      [this](const crow::request& req) -> crow::response {
        try {
          std::string sAuth = req.get_header_value("Authorization");
          std::string sApiKey = req.get_header_value("X-API-Key");
          _amMiddleware.authenticate(sAuth, sApiKey);

          auto vProviders = _prRepo.list();
          nlohmann::json jArr = nlohmann::json::array();
          for (const auto& p : vProviders) {
            jArr.push_back({{"id", p.iId}, {"name", p.sName},
                            {"type", p.sType},
                            {"api_endpoint", p.sApiEndpoint},
                            {"created_at", p.sCreatedAt},
                            {"updated_at", p.sUpdatedAt}});
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

  // POST /api/v1/providers — create a provider (admin)
  CROW_ROUTE(app, "/api/v1/providers").methods("POST"_method)(
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
          std::string sType = jBody.value("type", "");
          std::string sEndpoint = jBody.value("api_endpoint", "");
          std::string sToken = jBody.value("api_token", "");

          if (sName.empty() || sType.empty() || sEndpoint.empty() ||
              sToken.empty()) {
            throw common::ValidationError(
                "missing_fields",
                "name, type, api_endpoint, and api_token are required");
          }

          int64_t iId = _prRepo.create(sName, sType, sEndpoint, sToken);
          nlohmann::json jResp = {{"id", iId}, {"name", sName},
                                  {"type", sType},
                                  {"api_endpoint", sEndpoint}};
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

  // GET /api/v1/providers/<int> — get provider by ID (viewer)
  CROW_ROUTE(app, "/api/v1/providers/<int>").methods("GET"_method)(
      [this](const crow::request& req, int iId) -> crow::response {
        try {
          std::string sAuth = req.get_header_value("Authorization");
          std::string sApiKey = req.get_header_value("X-API-Key");
          _amMiddleware.authenticate(sAuth, sApiKey);

          auto oProvider = _prRepo.findById(iId);
          if (!oProvider.has_value()) {
            throw common::NotFoundError("provider_not_found",
                                        "Provider not found");
          }

          nlohmann::json jResp = {
              {"id", oProvider->iId},
              {"name", oProvider->sName},
              {"type", oProvider->sType},
              {"api_endpoint", oProvider->sApiEndpoint},
              {"created_at", oProvider->sCreatedAt},
              {"updated_at", oProvider->sUpdatedAt}};
          // Note: decrypted token is NOT included in GET response for security
          crow::response resp(200, jResp.dump(2));
          resp.set_header("Content-Type", "application/json");
          return resp;
        } catch (const common::AppError& e) {
          nlohmann::json jErr = {{"error", e._sErrorCode},
                                 {"message", e.what()}};
          return crow::response(e._iHttpStatus, jErr.dump(2));
        }
      });

  // PUT /api/v1/providers/<int> — update provider (admin)
  CROW_ROUTE(app, "/api/v1/providers/<int>").methods("PUT"_method)(
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
          std::string sType = jBody.value("type", "");
          std::string sEndpoint = jBody.value("api_endpoint", "");
          std::string sToken = jBody.value("api_token", "");

          if (sName.empty() || sType.empty() || sEndpoint.empty() ||
              sToken.empty()) {
            throw common::ValidationError(
                "missing_fields",
                "name, type, api_endpoint, and api_token are required");
          }

          _prRepo.update(iId, sName, sType, sEndpoint, sToken);
          nlohmann::json jResp = {{"id", iId}, {"name", sName},
                                  {"type", sType},
                                  {"api_endpoint", sEndpoint}};
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

  // DELETE /api/v1/providers/<int> — delete provider (admin)
  CROW_ROUTE(app, "/api/v1/providers/<int>").methods("DELETE"_method)(
      [this](const crow::request& req, int iId) -> crow::response {
        try {
          std::string sAuth = req.get_header_value("Authorization");
          std::string sApiKey = req.get_header_value("X-API-Key");
          auto rcCtx = _amMiddleware.authenticate(sAuth, sApiKey);
          if (rcCtx.sRole != "admin") {
            throw common::AuthorizationError("insufficient_role",
                                             "Admin role required");
          }

          _prRepo.deleteById(iId);
          return crow::response(204);
        } catch (const common::AppError& e) {
          nlohmann::json jErr = {{"error", e._sErrorCode},
                                 {"message", e.what()}};
          return crow::response(e._iHttpStatus, jErr.dump(2));
        }
      });
}

}  // namespace dns::api::routes
