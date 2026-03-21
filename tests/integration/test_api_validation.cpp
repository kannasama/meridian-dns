// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Meridian DNS Contributors
// This file is part of Meridian DNS. See LICENSE for details.

#include "api/RequestValidator.hpp"
#include "api/RateLimiter.hpp"
#include "api/RouteHelpers.hpp"
#include "common/Errors.hpp"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

using namespace dns::api;
using namespace dns::common;

// ── Validation rejection tests ──────────────────────────────────────────────

TEST(ApiValidationTest, RecordNameTooLongReturns400) {
  std::string sLong(254, 'a');
  EXPECT_THROW(RequestValidator::validateRecordName(sLong), ValidationError);
}

TEST(ApiValidationTest, InvalidRecordTypeReturns400) {
  EXPECT_THROW(RequestValidator::validateRecordType("BOGUS"), ValidationError);
}

TEST(ApiValidationTest, NegativeTtlReturns400) {
  EXPECT_THROW(RequestValidator::validateTtl(-5), ValidationError);
}

TEST(ApiValidationTest, VariableNameWithSpacesReturns400) {
  EXPECT_THROW(RequestValidator::validateVariableName("has spaces"), ValidationError);
}

TEST(ApiValidationTest, ProviderNameTooLongReturns400) {
  std::string sLong(129, 'x');
  EXPECT_THROW(RequestValidator::validateProviderName(sLong), ValidationError);
}

TEST(ApiValidationTest, InvalidProviderTypeReturns400) {
  EXPECT_THROW(RequestValidator::validateProviderType("route53"), ValidationError);
}

TEST(ApiValidationTest, EmptyUsernameReturns400) {
  EXPECT_THROW(RequestValidator::validateUsername(""), ValidationError);
}

TEST(ApiValidationTest, PasswordTooLongReturns400) {
  std::string sLong(1025, 'p');
  EXPECT_THROW(RequestValidator::validatePassword(sLong), ValidationError);
}

TEST(ApiValidationTest, PasswordTooShortReturns400) {
  EXPECT_THROW(RequestValidator::validatePassword("short"), ValidationError);
  EXPECT_THROW(RequestValidator::validatePassword("1234567"), ValidationError);
  EXPECT_NO_THROW(RequestValidator::validatePassword("12345678"));
}

// ── Response format tests ───────────────────────────────────────────────────
// Baseline security headers are set in-application as defense-in-depth.
// The reverse proxy provides additional headers (CSP, HSTS, Permissions-Policy).
// See docs/DEPLOYMENT.md §Reverse Proxy for the full configuration.

TEST(ApiValidationTest, AllResponsesHaveContentType) {
  auto resp = jsonResponse(200, {{"ok", true}});
  EXPECT_EQ(resp.get_header_value("Content-Type"), "application/json");
}

TEST(ApiValidationTest, AllResponsesHaveSecurityHeaders) {
  auto resp = jsonResponse(200, {{"ok", true}});
  EXPECT_EQ(resp.get_header_value("X-Content-Type-Options"), "nosniff");
  EXPECT_EQ(resp.get_header_value("X-Frame-Options"), "DENY");
  EXPECT_EQ(resp.get_header_value("Referrer-Policy"), "strict-origin-when-cross-origin");
}

TEST(ApiValidationTest, ErrorResponsesHaveSecurityHeaders) {
  ValidationError err("TEST", "test");
  auto resp = errorResponse(err);
  EXPECT_EQ(resp.get_header_value("X-Content-Type-Options"), "nosniff");
}

TEST(ApiValidationTest, ErrorResponsesHaveContentType) {
  ValidationError err("TEST", "test");
  auto resp = errorResponse(err);
  EXPECT_EQ(resp.get_header_value("Content-Type"), "application/json");
}

// ── Rate limiter integration ────────────────────────────────────────────────

TEST(ApiValidationTest, RateLimiterBlocksAfterThreshold) {
  RateLimiter rl(3, std::chrono::seconds(60));
  EXPECT_TRUE(rl.allow("test_ip"));
  EXPECT_TRUE(rl.allow("test_ip"));
  EXPECT_TRUE(rl.allow("test_ip"));
  EXPECT_FALSE(rl.allow("test_ip"));
}

// ── Error response format ───────────────────────────────────────────────────

TEST(ApiValidationTest, ErrorResponseHasCorrectJsonShape) {
  ValidationError err("FIELD_REQUIRED", "name is required");
  auto resp = errorResponse(err);
  auto j = nlohmann::json::parse(resp.body);
  EXPECT_EQ(j["error"], "FIELD_REQUIRED");
  EXPECT_EQ(j["message"], "name is required");
  EXPECT_EQ(resp.code, 400);
}

TEST(ApiValidationTest, InvalidJsonResponseHasCorrectShape) {
  auto resp = invalidJsonResponse();
  auto j = nlohmann::json::parse(resp.body);
  EXPECT_EQ(j["error"], "invalid_json");
  EXPECT_EQ(resp.code, 400);
}

// ── Body limit enforcement ──────────────────────────────────────────────────

TEST(ApiValidationTest, EnforceBodyLimitAllowsWithinDefault) {
  crow::request req;
  req.body = std::string(65536, 'x');  // exactly 64 KB
  EXPECT_NO_THROW(enforceBodyLimit(req));
}

TEST(ApiValidationTest, EnforceBodyLimitRejectsOverDefault) {
  crow::request req;
  req.body = std::string(65537, 'x');  // 64 KB + 1
  EXPECT_THROW(enforceBodyLimit(req), PayloadTooLargeError);
}

TEST(ApiValidationTest, EnforceBodyLimitRejectsOverCustom) {
  // 10 MB backup limit used by backup/restore routes
  static constexpr size_t kBackupBodyLimit = 10UL * 1024UL * 1024UL;
  crow::request req;
  req.body = std::string(kBackupBodyLimit + 1, 'x');
  EXPECT_THROW(enforceBodyLimit(req, kBackupBodyLimit), PayloadTooLargeError);
}

TEST(ApiValidationTest, EnforceBodyLimitAllowsWithinCustom) {
  static constexpr size_t kBackupBodyLimit = 10UL * 1024UL * 1024UL;
  crow::request req;
  req.body = std::string(kBackupBodyLimit, 'x');  // exactly 10 MB
  EXPECT_NO_THROW(enforceBodyLimit(req, kBackupBodyLimit));
}

TEST(ApiValidationTest, EnforceBodyLimitThrowsPayloadTooLargeError413) {
  crow::request req;
  req.body = std::string(100000, 'x');
  try {
    enforceBodyLimit(req);
    FAIL() << "Expected PayloadTooLargeError";
  } catch (const PayloadTooLargeError& e) {
    EXPECT_EQ(e._iHttpStatus, 413);
    EXPECT_EQ(e._sErrorCode, "PAYLOAD_TOO_LARGE");
  }
}
