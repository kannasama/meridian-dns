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

// ── Response format tests ───────────────────────────────────────────────────
// Security headers (CSP, X-Frame-Options, etc.) are delegated to the reverse
// proxy for v1.0 — see docs/DEPLOYMENT.md §Reverse Proxy.

TEST(ApiValidationTest, AllResponsesHaveContentType) {
  auto resp = jsonResponse(200, {{"ok", true}});
  EXPECT_EQ(resp.get_header_value("Content-Type"), "application/json");
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
