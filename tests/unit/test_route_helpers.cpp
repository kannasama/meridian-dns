#include "api/RouteHelpers.hpp"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

using namespace dns::api;

TEST(RouteHelpersTest, JsonResponseHasSecurityHeaders) {
  nlohmann::json j = {{"status", "ok"}};
  auto resp = jsonResponse(200, j);
  EXPECT_EQ(resp.code, 200);
  EXPECT_EQ(resp.get_header_value("Content-Type"), "application/json");
  EXPECT_EQ(resp.get_header_value("X-Content-Type-Options"), "nosniff");
  EXPECT_EQ(resp.get_header_value("X-Frame-Options"), "DENY");
  EXPECT_EQ(resp.get_header_value("Referrer-Policy"), "strict-origin-when-cross-origin");
  EXPECT_EQ(resp.get_header_value("Content-Security-Policy"), "default-src 'self'");
}

TEST(RouteHelpersTest, ErrorResponseHasSecurityHeaders) {
  dns::common::ValidationError err("TEST_ERROR", "test message");
  auto resp = errorResponse(err);
  EXPECT_EQ(resp.code, 400);
  EXPECT_EQ(resp.get_header_value("X-Content-Type-Options"), "nosniff");
  EXPECT_EQ(resp.get_header_value("X-Frame-Options"), "DENY");
}

TEST(RouteHelpersTest, InvalidJsonResponseHasSecurityHeaders) {
  auto resp = invalidJsonResponse();
  EXPECT_EQ(resp.code, 400);
  EXPECT_EQ(resp.get_header_value("X-Content-Type-Options"), "nosniff");
}
