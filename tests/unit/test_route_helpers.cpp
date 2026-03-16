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
