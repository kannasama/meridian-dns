// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Meridian DNS Contributors
// This file is part of Meridian DNS. See LICENSE for details.

#include "api/RouteHelpers.hpp"

namespace dns::api {

namespace {
// Baseline security headers set in-application as defense-in-depth.
// The reverse proxy provides additional headers (CSP, HSTS, Permissions-Policy).
// See docs/DEPLOYMENT.md §Reverse Proxy for the full recommended configuration.
void applySecurityHeaders(crow::response& resp) {
  resp.set_header("X-Content-Type-Options", "nosniff");
  resp.set_header("X-Frame-Options", "DENY");
  resp.set_header("Referrer-Policy", "strict-origin-when-cross-origin");
}
}  // namespace

void enforceBodyLimit(const crow::request& req, size_t nMaxBytes) {
  if (req.body.size() > nMaxBytes)
    throw common::PayloadTooLargeError("PAYLOAD_TOO_LARGE",
        "Request body exceeds maximum size of " + std::to_string(nMaxBytes) + " bytes");
}

common::RequestContext authenticate(const AuthMiddleware& amMiddleware,
                                    const crow::request& req) {
  auto rcCtx = amMiddleware.authenticate(req.get_header_value("Authorization"),
                                          req.get_header_value("X-API-Key"));

  // Populate client IP — X-Forwarded-For is trusted unconditionally.
  // Production deployments MUST use a trusted reverse proxy. See docs/DEPLOYMENT.md.
  // Future enhancement: DNS_TRUSTED_PROXIES configuration variable.
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
