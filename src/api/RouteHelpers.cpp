#include "api/RouteHelpers.hpp"

namespace dns::api {

common::RequestContext authenticate(const AuthMiddleware& amMiddleware,
                                    const crow::request& req) {
  return amMiddleware.authenticate(req.get_header_value("Authorization"),
                                   req.get_header_value("X-API-Key"));
}

void requireRole(const common::RequestContext& rcCtx, const std::string& sMinRole) {
  if (sMinRole == "admin" && rcCtx.sRole != "admin") {
    throw common::AuthorizationError("INSUFFICIENT_ROLE", "Admin role required");
  }
  if (sMinRole == "operator" && rcCtx.sRole == "viewer") {
    throw common::AuthorizationError("INSUFFICIENT_ROLE",
                                     "Operator or admin role required");
  }
}

crow::response jsonResponse(int iStatus, const nlohmann::json& j) {
  crow::response resp(iStatus, j.dump(2));
  resp.set_header("Content-Type", "application/json");
  return resp;
}

crow::response errorResponse(const common::AppError& e) {
  nlohmann::json jErr = {{"error", e._sErrorCode}, {"message", e.what()}};
  return crow::response(e._iHttpStatus, jErr.dump(2));
}

crow::response invalidJsonResponse() {
  nlohmann::json jErr = {{"error", "invalid_json"},
                         {"message", "Invalid JSON body"}};
  return crow::response(400, jErr.dump(2));
}

}  // namespace dns::api
