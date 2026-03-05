#include "api/routes/AuthRoutes.hpp"

#include "api/AuthMiddleware.hpp"
#include "api/RequestValidator.hpp"
#include "api/RouteHelpers.hpp"
#include "common/Errors.hpp"
#include "security/AuthService.hpp"

#include <nlohmann/json.hpp>

namespace dns::api::routes {

AuthRoutes::AuthRoutes(dns::security::AuthService& asService,
                       const dns::api::AuthMiddleware& amMiddleware)
    : _asService(asService), _amMiddleware(amMiddleware) {}

AuthRoutes::~AuthRoutes() = default;

void AuthRoutes::registerRoutes(crow::SimpleApp& app) {
  // POST /api/v1/auth/local/login
  CROW_ROUTE(app, "/api/v1/auth/local/login").methods("POST"_method)(
      [this](const crow::request& req) -> crow::response {
        try {
          auto jBody = nlohmann::json::parse(req.body);
          std::string sUsername = jBody.value("username", "");
          std::string sPassword = jBody.value("password", "");

          RequestValidator::validateUsername(sUsername);
          RequestValidator::validatePassword(sPassword);

          std::string sToken = _asService.authenticateLocal(sUsername, sPassword);

          return jsonResponse(200, {{"token", sToken}});
        } catch (const common::AppError& e) {
          return errorResponse(e);
        } catch (const nlohmann::json::exception&) {
          return invalidJsonResponse();
        }
      });

  // POST /api/v1/auth/local/logout
  CROW_ROUTE(app, "/api/v1/auth/local/logout").methods("POST"_method)(
      [this](const crow::request& req) -> crow::response {
        try {
          auto rcCtx = authenticate(_amMiddleware, req);

          return jsonResponse(200, {{"message", "Logged out successfully"}});
        } catch (const common::AppError& e) {
          return errorResponse(e);
        }
      });

  // GET /api/v1/auth/me
  CROW_ROUTE(app, "/api/v1/auth/me").methods("GET"_method)(
      [this](const crow::request& req) -> crow::response {
        try {
          auto rcCtx = authenticate(_amMiddleware, req);

          return jsonResponse(200, {
              {"user_id", rcCtx.iUserId},
              {"username", rcCtx.sUsername},
              {"role", rcCtx.sRole},
              {"auth_method", rcCtx.sAuthMethod},
          });
        } catch (const common::AppError& e) {
          return errorResponse(e);
        }
      });
}

}  // namespace dns::api::routes
