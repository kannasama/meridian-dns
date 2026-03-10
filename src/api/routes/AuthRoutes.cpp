#include "api/routes/AuthRoutes.hpp"

#include "api/AuthMiddleware.hpp"
#include "api/RateLimiter.hpp"
#include "api/RequestValidator.hpp"
#include "api/RouteHelpers.hpp"
#include "common/Errors.hpp"
#include "dal/UserRepository.hpp"
#include "security/AuthService.hpp"
#include "security/CryptoService.hpp"

#include <nlohmann/json.hpp>

namespace dns::api::routes {

static dns::api::RateLimiter g_rlLogin(5, std::chrono::seconds(60));

AuthRoutes::AuthRoutes(dns::security::AuthService& asService,
                       const dns::api::AuthMiddleware& amMiddleware,
                       dns::dal::UserRepository& urRepo)
    : _asService(asService), _amMiddleware(amMiddleware), _urRepo(urRepo) {}

AuthRoutes::~AuthRoutes() = default;

void AuthRoutes::registerRoutes(crow::SimpleApp& app) {
  // POST /api/v1/auth/local/login
  CROW_ROUTE(app, "/api/v1/auth/local/login").methods("POST"_method)(
      [this](const crow::request& req) -> crow::response {
        try {
         std::string sClientIp = req.get_header_value("X-Forwarded-For");
         if (sClientIp.empty()) sClientIp = req.remote_ip_address;
         if (!g_rlLogin.allow(sClientIp))
           throw common::RateLimitedError("RATE_LIMITED",
                                          "Too many login attempts. Try again later.");

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

          bool bForcePasswordChange = false;
          auto oUser = _urRepo.findById(rcCtx.iUserId);
          if (oUser) {
            bForcePasswordChange = oUser->bForcePasswordChange;
          }

          std::string sEmail;
          if (oUser) {
            sEmail = oUser->sEmail;
          }

          nlohmann::json jPerms = nlohmann::json::array();
          for (const auto& sPerm : rcCtx.vPermissions) {
            jPerms.push_back(sPerm);
          }

          return jsonResponse(200, {
              {"user_id", rcCtx.iUserId},
              {"username", rcCtx.sUsername},
              {"email", sEmail},
              {"role", rcCtx.sRole},
              {"permissions", jPerms},
              {"auth_method", rcCtx.sAuthMethod},
              {"force_password_change", bForcePasswordChange},
          });
        } catch (const common::AppError& e) {
          return errorResponse(e);
        }
      });

  // PUT /api/v1/auth/profile
  CROW_ROUTE(app, "/api/v1/auth/profile").methods("PUT"_method)(
      [this](const crow::request& req) -> crow::response {
        try {
          auto rcCtx = authenticate(_amMiddleware, req);

          auto jBody = nlohmann::json::parse(req.body);
          std::string sEmail = jBody.value("email", "");
          RequestValidator::validateRequired(sEmail, "email");

          auto oUser = _urRepo.findById(rcCtx.iUserId);
          if (!oUser) throw common::NotFoundError("USER_NOT_FOUND", "User not found");

          _urRepo.update(rcCtx.iUserId, sEmail, oUser->bIsActive);
          return jsonResponse(200, {{"message", "Profile updated"}});
        } catch (const common::AppError& e) {
          return errorResponse(e);
        } catch (const nlohmann::json::exception&) {
          return invalidJsonResponse();
        }
      });

  // POST /api/v1/auth/change-password
  CROW_ROUTE(app, "/api/v1/auth/change-password").methods("POST"_method)(
      [this](const crow::request& req) -> crow::response {
        try {
          auto rcCtx = authenticate(_amMiddleware, req);

          auto jBody = nlohmann::json::parse(req.body);
          std::string sCurrentPassword = jBody.value("current_password", "");
          std::string sNewPassword = jBody.value("new_password", "");

          RequestValidator::validateRequired(sCurrentPassword, "current_password");
          RequestValidator::validatePassword(sNewPassword);

          // Verify current password
          auto oUser = _urRepo.findById(rcCtx.iUserId);
          if (!oUser) throw common::NotFoundError("USER_NOT_FOUND", "User not found");

          if (!dns::security::CryptoService::verifyPassword(sCurrentPassword,
                                                            oUser->sPasswordHash)) {
            throw common::AuthenticationError("INVALID_PASSWORD", "Current password is incorrect");
          }

          std::string sHash = dns::security::CryptoService::hashPassword(sNewPassword);
          _urRepo.updatePassword(rcCtx.iUserId, sHash);
          _urRepo.setForcePasswordChange(rcCtx.iUserId, false);

          return jsonResponse(200, {{"message", "Password changed successfully"}});
        } catch (const common::AppError& e) {
          return errorResponse(e);
        } catch (const nlohmann::json::exception&) {
          return invalidJsonResponse();
        }
      });
}

}  // namespace dns::api::routes
