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
  static const char* kTestCert =
      "MIICpDCCAYwCCQDU+pQRHkTCbDANBgkqhkiG9w0BAQsFADAUMRIwEAYDVQQDDAls"
      "b2NhbGhvc3QwHhcNMjQwMTAxMDAwMDAwWhcNMjUwMTAxMDAwMDAwWjAUMRIwEAYD"
      "VQQDDAlsb2NhbGhvc3QwggEiMA0GCSqGSIb3DQEBAQUAA4IBDwAwggEKAoIBAQC7"
      "o4qne60TB3pOYaBy/YCbSiTO6HMmcGq2KkMSx9J0oVEMOC0Ij2je6sEBP4E9JE5"
      "e4HjBKMjHfLUHPBQGVAMqHF1VKFLhIHlSgOFEPH/HjTqdjXKo+nGEcuJOEN8WT+"
      "sJlMSHfhmB06CihDouKy8nB2MnQW0CHMLhDAqlLX8Eh+b6aJJl5u3UKuJlFMpPjj"
      "e+GDjz2JOPlaSmkQwvbJpOAqaZdVUMlvICFwP1kkOmLqsP5Y8PVYb9aYLQVU33/b"
      "vSFGu8rQnJgUPMC9vaMFkQ0F50UAy5DrlCJlVnTwTvEXThwnJMkBF10MS8xrQzgy"
      "QCt+833tGRnUVbZ3e7LxAgMBAAEwDQYJKoZIhvcNAQELBQADggEBABq03tjNPHFh"
      "fVYBjJbnsiNHSt5u6dYLB74S7IfYLlzVR/Yb0ItOEGC/GjvmXaV7PXHV7NwFCXt"
      "zl6HZJALPODCfFnGOSKYEExrs0nZJ+N/gTqGK+y0d0ECwGm4HBCaYr5jJk4pg5V3"
      "rYBH6JWA0NYEdeO9CdJ43wOQqj5sN4NJjQ3z3DOh+5j4WZSHphFBp8a7mgKPJva0"
      "ESbTBBGJmVSP8VIz7FYGNaIx2CMmW/FNfcX3VDSrvF8kHtX7aSBitfFnLShAzEpT"
      "7mdu0Aq6p2sR6NqKlhIVOoDAIuKznbk7BIJbHPqXNslQZj3VNZlwE8Y7p7IbjGaP"
      "aCFPEtIL0i0=";

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
