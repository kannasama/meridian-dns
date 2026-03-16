// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Meridian DNS Contributors
// This file is part of Meridian DNS. See LICENSE for details.

#include "security/SamlService.hpp"

#include "common/Errors.hpp"
#include "security/SamlReplayCache.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <string>

using dns::security::SamlAuthState;
using dns::security::SamlReplayCache;
using dns::security::SamlService;

class SamlServiceTest : public ::testing::Test {
 protected:
  void SetUp() override {
    _upCache = std::make_unique<SamlReplayCache>();
    _upService = std::make_unique<SamlService>(*_upCache);
  }

  std::unique_ptr<SamlReplayCache> _upCache;
  std::unique_ptr<SamlService> _upService;
};

// ── Auth state management tests (unchanged) ────────────────────────────────

TEST_F(SamlServiceTest, StoreAndRetrieveSamlState) {
  SamlAuthState saState;
  saState.iIdpId = 7;
  saState.sRequestId = "_abc123";
  saState.bIsTestMode = true;
  saState.tpCreatedAt = std::chrono::system_clock::now();

  _upService->storeAuthState("relay-key", saState);

  // First consume should succeed
  auto oResult = _upService->consumeAuthState("relay-key");
  ASSERT_TRUE(oResult.has_value());
  EXPECT_EQ(oResult->iIdpId, 7);
  EXPECT_EQ(oResult->sRequestId, "_abc123");
  EXPECT_TRUE(oResult->bIsTestMode);

  // Second consume should return nullopt
  auto oResult2 = _upService->consumeAuthState("relay-key");
  EXPECT_FALSE(oResult2.has_value());
}

TEST_F(SamlServiceTest, ConsumeNonexistentState) {
  auto oResult = _upService->consumeAuthState("does-not-exist");
  EXPECT_FALSE(oResult.has_value());
}

// ── Static helper tests ────────────────────────────────────────────────────

TEST_F(SamlServiceTest, Base64Encode) {
  std::string sEncoded = SamlService::base64Encode("Hello, World!");
  EXPECT_FALSE(sEncoded.empty());
  // Base64 of "Hello, World!" is "SGVsbG8sIFdvcmxkIQ=="
  EXPECT_EQ(sEncoded, "SGVsbG8sIFdvcmxkIQ==");
}

TEST_F(SamlServiceTest, FormatIso8601) {
  // Use a known timestamp: 2024-01-15T12:30:00Z
  std::tm tmTest{};
  tmTest.tm_year = 124;  // 2024
  tmTest.tm_mon = 0;     // January
  tmTest.tm_mday = 15;
  tmTest.tm_hour = 12;
  tmTest.tm_min = 30;
  tmTest.tm_sec = 0;

  auto tTime = timegm(&tmTest);
  auto tp = std::chrono::system_clock::from_time_t(tTime);

  std::string sFormatted = SamlService::formatIso8601(tp);
  EXPECT_EQ(sFormatted, "2024-01-15T12:30:00Z");
}

// ── Lasso integration tests ────────────────────────────────────────────────

TEST_F(SamlServiceTest, IsIdpRegisteredReturnsFalseForUnknown) {
  EXPECT_FALSE(_upService->isIdpRegistered(999));
}

TEST_F(SamlServiceTest, BuildLoginUrlThrowsForUnregisteredIdp) {
  EXPECT_THROW(_upService->buildLoginUrl(999, "relay"), dns::common::AuthenticationError);
}

TEST_F(SamlServiceTest, ValidateResponseThrowsForUnregisteredIdp) {
  EXPECT_THROW(
      _upService->validateResponse(999, "dGVzdA==", "_req1"),
      dns::common::AuthenticationError);
}

TEST_F(SamlServiceTest, RegisterIdpAndBuildLoginUrl) {
  // Use a self-signed test certificate (base64 DER, no PEM headers)
  // This is a minimal RSA cert for testing lasso metadata parsing
  // Validity: 2026-03-15 to 2036-03-12 (10-year self-signed)
  static const char* kTestCert =
      "MIIDCTCCAfGgAwIBAgIUd+SbZjQ0+3T4amUvPOCN8YvJHYswDQYJKoZIhvcNAQEL"
      "BQAwFDESMBAGA1UEAwwJbG9jYWxob3N0MB4XDTI2MDMxNTIzMTk0MVoXDTM2MDMx"
      "MjIzMTk0MVowFDESMBAGA1UEAwwJbG9jYWxob3N0MIIBIjANBgkqhkiG9w0BAQEF"
      "AAOCAQ8AMIIBCgKCAQEAlzgJUJssOwGhq8mbyW0HgxWfo7At/6llYVKgcllccCJD"
      "bBIhWO/jpehxZ7whgnk9sIa4cZY/rPxKaamzFs8HCfG8vwVQTggNcuv469DtX5c7"
      "UG0+G6Ls57ubC50n1sOp6jVaLtbAuyRhOvxFgFawy79MgEIivmmaNYPKBEghdHCF"
      "klBBA9atj3H2+o8pXGl7yqFLO5FqyVtiC0q4IDXmr7C4pXi/hddsxmj9otlE8Zod"
      "diKVODP0Dh5E4g2Oaa6Lt/folXn/yyFCE/F+y2UDkRUy6njYflTPgW27g/iBQ/Vf"
      "TQwmYT+9m5KGJVVLrEMA8qfCSkpbpKFU3QlucUdqSQIDAQABo1MwUTAdBgNVHQ4E"
      "FgQUhLYBuqpR6sztX4IuXx0z6X06ccAwHwYDVR0jBBgwFoAUhLYBuqpR6sztX4Iu"
      "Xx0z6X06ccAwDwYDVR0TAQH/BAUwAwEB/zANBgkqhkiG9w0BAQsFAAOCAQEABuBS"
      "D0IrutAzX5N4MzpxKcDL8ZUTevI2u6S9eNDtewuiVW62tGX+HWDx14QjjP7uBRYb"
      "CQv7boRMztjN3Hh3QTJEkbAAlgAvMF/hO2s2OAcPiXTOPXwgI9WdsSABKraLMgXy"
      "zeVnK5GmOKxlBzfS38vF3mJrWedSyJTXJG52u7kzGX1C+wVy488nYPltVQjClZfP"
      "ysk2JjposprkAGmwxWRZu5Tqs5nSOkiwlLngGSWJrW2X3RFN/Mi3mN7ZB3BJVkBW"
      "q5MNgfQyW0F59juD88a3SCZ+GbqbwV61S+L80g565CRXKpazHdOaN1kmLODZhpDH"
      "LVo2a3+yOwoSQS4wTA==";

  // Register a test IdP
  _upService->registerIdp(
      42,
      "https://meridian.example.com",                       // SP entity ID
      "https://meridian.example.com/api/v1/auth/saml/42/acs",  // ACS URL
      "https://idp.example.com",                              // IdP entity ID
      "https://idp.example.com/sso",                          // IdP SSO URL
      kTestCert);                                             // IdP certificate

  EXPECT_TRUE(_upService->isIdpRegistered(42));

  // Build login URL
  auto result = _upService->buildLoginUrl(42, "test-relay-state");
  EXPECT_FALSE(result.sRedirectUrl.empty());
  EXPECT_FALSE(result.sRequestId.empty());
  EXPECT_NE(result.sRedirectUrl.find("https://idp.example.com/sso"), std::string::npos);
  EXPECT_NE(result.sRedirectUrl.find("SAMLRequest="), std::string::npos);
  EXPECT_NE(result.sRedirectUrl.find("RelayState="), std::string::npos);
}
