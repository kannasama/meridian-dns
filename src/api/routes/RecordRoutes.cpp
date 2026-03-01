#include "api/routes/RecordRoutes.hpp"

#include "api/AuthMiddleware.hpp"
#include "common/Errors.hpp"
#include "dal/RecordRepository.hpp"

#include <nlohmann/json.hpp>

namespace dns::api::routes {

RecordRoutes::RecordRoutes(dns::dal::RecordRepository& rrRepo,
                           const dns::api::AuthMiddleware& amMiddleware)
    : _rrRepo(rrRepo), _amMiddleware(amMiddleware) {}
RecordRoutes::~RecordRoutes() = default;

void RecordRoutes::registerRoutes(crow::SimpleApp& app) {
  // GET /api/v1/zones/<int>/records
  CROW_ROUTE(app, "/api/v1/zones/<int>/records").methods("GET"_method)(
      [this](const crow::request& req, int iZoneId) -> crow::response {
        try {
          std::string sAuth = req.get_header_value("Authorization");
          std::string sApiKey = req.get_header_value("X-API-Key");
          _amMiddleware.authenticate(sAuth, sApiKey);

          auto vRecords = _rrRepo.listByZone(iZoneId);
          nlohmann::json jArr = nlohmann::json::array();
          for (const auto& r : vRecords) {
            nlohmann::json jRec = {
                {"id", r.iId},
                {"zone_id", r.iZoneId},
                {"name", r.sName},
                {"type", r.sType},
                {"ttl", r.iTtl},
                {"value_template", r.sValueTemplate},
                {"priority", r.iPriority},
                {"created_at", r.sCreatedAt},
                {"updated_at", r.sUpdatedAt}};
            if (r.oLastAuditId.has_value()) {
              jRec["last_audit_id"] = *r.oLastAuditId;
            } else {
              jRec["last_audit_id"] = nullptr;
            }
            jArr.push_back(jRec);
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

  // POST /api/v1/zones/<int>/records
  CROW_ROUTE(app, "/api/v1/zones/<int>/records").methods("POST"_method)(
      [this](const crow::request& req, int iZoneId) -> crow::response {
        try {
          std::string sAuth = req.get_header_value("Authorization");
          std::string sApiKey = req.get_header_value("X-API-Key");
          auto rcCtx = _amMiddleware.authenticate(sAuth, sApiKey);
          if (rcCtx.sRole == "viewer") {
            throw common::AuthorizationError("insufficient_role",
                                             "Operator role required");
          }

          auto jBody = nlohmann::json::parse(req.body);
          std::string sName = jBody.value("name", "");
          std::string sType = jBody.value("type", "");
          std::string sValueTemplate = jBody.value("value_template", "");
          int iTtl = jBody.value("ttl", 300);
          int iPriority = jBody.value("priority", 0);

          if (sName.empty() || sType.empty() || sValueTemplate.empty()) {
            throw common::ValidationError(
                "missing_fields",
                "name, type, and value_template are required");
          }

          int64_t iId = _rrRepo.create(iZoneId, sName, sType, iTtl,
                                       sValueTemplate, iPriority);
          nlohmann::json jResp = {
              {"id", iId},         {"zone_id", iZoneId},
              {"name", sName},     {"type", sType},
              {"ttl", iTtl},       {"value_template", sValueTemplate},
              {"priority", iPriority}};
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

  // GET /api/v1/zones/<int>/records/<int>
  CROW_ROUTE(app, "/api/v1/zones/<int>/records/<int>").methods("GET"_method)(
      [this](const crow::request& req, int /*iZoneId*/,
             int iRecordId) -> crow::response {
        try {
          std::string sAuth = req.get_header_value("Authorization");
          std::string sApiKey = req.get_header_value("X-API-Key");
          _amMiddleware.authenticate(sAuth, sApiKey);

          auto oRecord = _rrRepo.findById(iRecordId);
          if (!oRecord.has_value()) {
            throw common::NotFoundError("record_not_found",
                                        "Record not found");
          }

          nlohmann::json jResp = {
              {"id", oRecord->iId},
              {"zone_id", oRecord->iZoneId},
              {"name", oRecord->sName},
              {"type", oRecord->sType},
              {"ttl", oRecord->iTtl},
              {"value_template", oRecord->sValueTemplate},
              {"priority", oRecord->iPriority},
              {"created_at", oRecord->sCreatedAt},
              {"updated_at", oRecord->sUpdatedAt}};
          if (oRecord->oLastAuditId.has_value()) {
            jResp["last_audit_id"] = *oRecord->oLastAuditId;
          } else {
            jResp["last_audit_id"] = nullptr;
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

  // PUT /api/v1/zones/<int>/records/<int>
  CROW_ROUTE(app, "/api/v1/zones/<int>/records/<int>").methods("PUT"_method)(
      [this](const crow::request& req, int /*iZoneId*/,
             int iRecordId) -> crow::response {
        try {
          std::string sAuth = req.get_header_value("Authorization");
          std::string sApiKey = req.get_header_value("X-API-Key");
          auto rcCtx = _amMiddleware.authenticate(sAuth, sApiKey);
          if (rcCtx.sRole == "viewer") {
            throw common::AuthorizationError("insufficient_role",
                                             "Operator role required");
          }

          auto jBody = nlohmann::json::parse(req.body);
          std::string sName = jBody.value("name", "");
          std::string sType = jBody.value("type", "");
          std::string sValueTemplate = jBody.value("value_template", "");
          int iTtl = jBody.value("ttl", 300);
          int iPriority = jBody.value("priority", 0);

          if (sName.empty() || sType.empty() || sValueTemplate.empty()) {
            throw common::ValidationError(
                "missing_fields",
                "name, type, and value_template are required");
          }

          _rrRepo.update(iRecordId, sName, sType, iTtl, sValueTemplate,
                         iPriority, std::nullopt);
          nlohmann::json jResp = {
              {"id", iRecordId},   {"name", sName},
              {"type", sType},     {"ttl", iTtl},
              {"value_template", sValueTemplate},
              {"priority", iPriority}};
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

  // DELETE /api/v1/zones/<int>/records/<int>
  CROW_ROUTE(app, "/api/v1/zones/<int>/records/<int>")
      .methods("DELETE"_method)(
          [this](const crow::request& req, int /*iZoneId*/,
                 int iRecordId) -> crow::response {
            try {
              std::string sAuth = req.get_header_value("Authorization");
              std::string sApiKey = req.get_header_value("X-API-Key");
              auto rcCtx = _amMiddleware.authenticate(sAuth, sApiKey);
              if (rcCtx.sRole == "viewer") {
                throw common::AuthorizationError("insufficient_role",
                                                 "Operator role required");
              }

              _rrRepo.deleteById(iRecordId);
              return crow::response(204);
            } catch (const common::AppError& e) {
              nlohmann::json jErr = {{"error", e._sErrorCode},
                                     {"message", e.what()}};
              return crow::response(e._iHttpStatus, jErr.dump(2));
            }
          });
}

}  // namespace dns::api::routes
