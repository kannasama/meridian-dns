#pragma once
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Meridian DNS Contributors
// This file is part of Meridian DNS. See LICENSE for details.

#include <string>
#include <string_view>

#include <crow.h>
#include <nlohmann/json.hpp>

#include "api/AuthMiddleware.hpp"
#include "common/Errors.hpp"
#include "common/Types.hpp"

namespace dns::api {

/// Authenticate a Crow request via AuthMiddleware.
common::RequestContext authenticate(const AuthMiddleware& amMiddleware,
                                    const crow::request& req);

/// Enforce a specific permission. Throws AuthorizationError if the user
/// does not have the required permission in their RequestContext.
void requirePermission(const common::RequestContext& rcCtx, std::string_view svPermission);

/// Enforce maximum request body size (64 KB). Throws PayloadTooLargeError if exceeded.
/// Call at the start of any route handler that processes a request body.
void enforceBodyLimit(const crow::request& req, size_t nMaxBytes = 65536);

/// Build a JSON response with Content-Type and security headers.
crow::response jsonResponse(int iStatus, const nlohmann::json& j);

/// Build an error response from an AppError with security headers.
crow::response errorResponse(const common::AppError& e);

/// Build an error response for invalid JSON parse failures.
crow::response invalidJsonResponse();

/// Format a display identity for audit logging.
/// Returns "Display Name (username)" when display_name is set, otherwise just "username".
std::string formatAuditIdentity(const common::RequestContext& rcCtx);

/// Build an AuditContext from a RequestContext.
common::AuditContext buildAuditContext(const common::RequestContext& rcCtx);

/// Sanitize a string for safe use in Content-Disposition filenames.
/// Keeps only alphanumeric chars, hyphens, underscores, and dots; replaces
/// everything else with '_'. Returns @p sFallback if the result is empty.
std::string sanitizeFilename(const std::string& sInput,
                             const std::string& sFallback = "export");

}  // namespace dns::api
