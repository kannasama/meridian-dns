#pragma once

#include <string>

#include <crow.h>
#include <nlohmann/json.hpp>

#include "api/AuthMiddleware.hpp"
#include "common/Errors.hpp"
#include "common/Types.hpp"

namespace dns::api {

/// Authenticate a Crow request via AuthMiddleware.
common::RequestContext authenticate(const AuthMiddleware& amMiddleware,
                                    const crow::request& req);

/// Enforce minimum role. Throws AuthorizationError if insufficient.
void requireRole(const common::RequestContext& rcCtx, const std::string& sMinRole);

/// Build a JSON response with Content-Type and security headers.
crow::response jsonResponse(int iStatus, const nlohmann::json& j);

/// Build an error response from an AppError with security headers.
crow::response errorResponse(const common::AppError& e);

/// Build an error response for invalid JSON parse failures.
crow::response invalidJsonResponse();

}  // namespace dns::api
