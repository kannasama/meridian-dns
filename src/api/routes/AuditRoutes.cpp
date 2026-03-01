#include "api/routes/AuditRoutes.hpp"

#include "api/AuthMiddleware.hpp"
#include "common/Errors.hpp"
#include "dal/AuditRepository.hpp"

#include <nlohmann/json.hpp>

namespace dns::api::routes {

AuditRoutes::AuditRoutes(dns::dal::AuditRepository& arRepo,
                         const dns::api::AuthMiddleware& amMiddleware,
                         int iAuditRetentionDays)
    : _arRepo(arRepo),
      _amMiddleware(amMiddleware),
      _iAuditRetentionDays(iAuditRetentionDays) {}
AuditRoutes::~AuditRoutes() = default;

void AuditRoutes::registerRoutes(crow::SimpleApp& app) {
  // GET /api/v1/audit — query audit log (viewer)
  CROW_ROUTE(app, "/api/v1/audit").methods("GET"_method)(
      [this](const crow::request& req) -> crow::response {
        try {
          std::string sAuth = req.get_header_value("Authorization");
          std::string sApiKey = req.get_header_value("X-API-Key");
          _amMiddleware.authenticate(sAuth, sApiKey);

          std::optional<std::string> oEntityType;
          std::optional<std::string> oIdentity;
          std::optional<std::string> oFrom;
          std::optional<std::string> oTo;

          auto* pEntityType = req.url_params.get("entity_type");
          if (pEntityType) oEntityType = std::string(pEntityType);
          auto* pIdentity = req.url_params.get("identity");
          if (pIdentity) oIdentity = std::string(pIdentity);
          auto* pFrom = req.url_params.get("from");
          if (pFrom) oFrom = std::string(pFrom);
          auto* pTo = req.url_params.get("to");
          if (pTo) oTo = std::string(pTo);

          int iLimit = 100;
          int iOffset = 0;
          auto* pLimit = req.url_params.get("limit");
          if (pLimit) iLimit = std::stoi(pLimit);
          auto* pOffset = req.url_params.get("offset");
          if (pOffset) iOffset = std::stoi(pOffset);

          auto vRows = _arRepo.query(oEntityType, oIdentity, oFrom, oTo,
                                     iLimit, iOffset);
          nlohmann::json jArr = nlohmann::json::array();
          for (const auto& a : vRows) {
            nlohmann::json jRow = {
                {"id", a.iId},
                {"entity_type", a.sEntityType},
                {"entity_id", a.iEntityId},
                {"operation", a.sOperation},
                {"identity", a.sIdentity},
                {"auth_method", a.sAuthMethod},
                {"ip_address", a.sIpAddress},
                {"timestamp", a.sTimestamp}};
            if (!a.sOldValue.empty()) {
              jRow["old_value"] = nlohmann::json::parse(a.sOldValue);
            } else {
              jRow["old_value"] = nullptr;
            }
            if (!a.sNewValue.empty()) {
              jRow["new_value"] = nlohmann::json::parse(a.sNewValue);
            } else {
              jRow["new_value"] = nullptr;
            }
            jArr.push_back(jRow);
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

  // DELETE /api/v1/audit/purge — purge old entries (admin)
  CROW_ROUTE(app, "/api/v1/audit/purge").methods("DELETE"_method)(
      [this](const crow::request& req) -> crow::response {
        try {
          std::string sAuth = req.get_header_value("Authorization");
          std::string sApiKey = req.get_header_value("X-API-Key");
          auto rcCtx = _amMiddleware.authenticate(sAuth, sApiKey);
          if (rcCtx.sRole != "admin") {
            throw common::AuthorizationError("insufficient_role",
                                             "Admin role required");
          }

          auto prResult = _arRepo.purgeOld(_iAuditRetentionDays);
          nlohmann::json jResp = {{"deleted", prResult.iDeletedCount}};
          if (prResult.oOldestRemaining.has_value()) {
            auto tpEpoch = std::chrono::duration_cast<std::chrono::seconds>(
                               prResult.oOldestRemaining->time_since_epoch())
                               .count();
            jResp["oldest_remaining"] = std::to_string(tpEpoch);
          } else {
            jResp["oldest_remaining"] = nullptr;
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
}

}  // namespace dns::api::routes
