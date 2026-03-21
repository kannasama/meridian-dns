// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Meridian DNS Contributors
// This file is part of Meridian DNS. See LICENSE for details.

#include "api/RouteHelpers.hpp"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

using namespace dns::api;

TEST(RouteHelpersTest, JsonResponseSetsContentType) {
  nlohmann::json j = {{"status", "ok"}};
  auto resp = jsonResponse(200, j);
  EXPECT_EQ(resp.code, 200);
  EXPECT_EQ(resp.get_header_value("Content-Type"), "application/json");
  // Security headers (CSP, X-Frame-Options, etc.) are delegated to the
  // reverse proxy for v1.0 — see docs/DEPLOYMENT.md §Reverse Proxy.
}

TEST(RouteHelpersTest, ErrorResponseSetsStatusAndBody) {
  dns::common::ValidationError err("TEST_ERROR", "test message");
  auto resp = errorResponse(err);
  EXPECT_EQ(resp.code, 400);
  EXPECT_EQ(resp.get_header_value("Content-Type"), "application/json");
}

TEST(RouteHelpersTest, InvalidJsonResponseReturns400) {
  auto resp = invalidJsonResponse();
  EXPECT_EQ(resp.code, 400);
  EXPECT_EQ(resp.get_header_value("Content-Type"), "application/json");
}

// --- sanitizeFilename tests ---

TEST(RouteHelpersTest, SanitizeFilenameKeepsSafeChars) {
  EXPECT_EQ(sanitizeFilename("example.com"), "example.com");
  EXPECT_EQ(sanitizeFilename("my-zone_1.test"), "my-zone_1.test");
}

TEST(RouteHelpersTest, SanitizeFilenameReplacesUnsafeChars) {
  EXPECT_EQ(sanitizeFilename("evil\"zone"), "evil_zone");
  EXPECT_EQ(sanitizeFilename("a b/c\\d"), "a_b_c_d");
  EXPECT_EQ(sanitizeFilename("cr\r\nlf"), "cr__lf");
}

TEST(RouteHelpersTest, SanitizeFilenameReturnsFallbackWhenEmpty) {
  EXPECT_EQ(sanitizeFilename(""), "export");
  EXPECT_EQ(sanitizeFilename("", "zone"), "zone");
}

TEST(RouteHelpersTest, SanitizeFilenameAllUnsafeReturnsFallback) {
  EXPECT_EQ(sanitizeFilename("!!!"), "___");
}
