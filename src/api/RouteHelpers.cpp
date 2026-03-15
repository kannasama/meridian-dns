#include "api/RouteHelpers.hpp"

namespace dns::api {

namespace {
void applySecurityHeaders(crow::response& resp) {
  resp.set_header("X-Content-Type-Options", "nosniff");
  resp.set_header("X-Frame-Options", "DENY");
  resp.set_header("Referrer-Policy", "strict-origin-when-cross-origin");
  resp.set_header("Content-Security-Policy", "default-src 'self'");
}
}  // namespace

common::RequestContext authenticate(const AuthMiddleware& amMiddleware,
                                    const crow::request& req) {
  auto rcCtx = amMiddleware.authenticate(req.get_header_value("Authorization"),
                                          req.get_header_value("X-API-Key"));

  // Populate client IP with X-Forwarded-For awareness
  std::string sIp = req.get_header_value("X-Forwarded-For");
  if (!sIp.empty()) {
    auto pos = sIp.find(',');
    if (pos != std::string::npos) sIp = sIp.substr(0, pos);
    auto start = sIp.find_first_not_of(' ');
    auto end = sIp.find_last_not_of(' ');
    if (start != std::string::npos) sIp = sIp.substr(start, end - start + 1);
  } else {
    sIp = req.remote_ip_address;
  }
  rcCtx.sIpAddress = sIp;

  return rcCtx;
}

void requirePermission(const common::RequestContext& rcCtx, std::string_view svPermission) {
  if (rcCtx.vPermissions.count(std::string(svPermission)) == 0) {
    throw common::AuthorizationError(
        "INSUFFICIENT_PERMISSION",
        "Required permission: " + std::string(svPermission));
  }
}

crow::response jsonResponse(int iStatus, const nlohmann::json& j) {
  crow::response resp(iStatus, j.dump(2));
  resp.set_header("Content-Type", "application/json");
  applySecurityHeaders(resp);
  return resp;
}

crow::response errorResponse(const common::AppError& e) {
  nlohmann::json jErr = {{"error", e._sErrorCode}, {"message", e.what()}};
  crow::response resp(e._iHttpStatus, jErr.dump(2));
  resp.set_header("Content-Type", "application/json");
  applySecurityHeaders(resp);
  return resp;
}

crow::response invalidJsonResponse() {
  nlohmann::json jErr = {{"error", "invalid_json"},
                         {"message", "Invalid JSON body"}};
  crow::response resp(400, jErr.dump(2));
  resp.set_header("Content-Type", "application/json");
  applySecurityHeaders(resp);
  return resp;
}

std::string formatAuditIdentity(const common::RequestContext& rcCtx) {
  if (rcCtx.sDisplayName.empty()) return rcCtx.sUsername;
  return rcCtx.sDisplayName + " (" + rcCtx.sUsername + ")";
}

common::AuditContext buildAuditContext(const common::RequestContext& rcCtx) {
  common::AuditContext ac;
  ac.sIdentity = formatAuditIdentity(rcCtx);
  ac.sAuthMethod = rcCtx.sAuthMethod;
  ac.sIpAddress = rcCtx.sIpAddress;
  return ac;
}

}  // namespace dns::api
