// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Meridian DNS Contributors
// This file is part of Meridian DNS. See LICENSE for details.

#include "security/OidcService.hpp"

#include "common/Errors.hpp"

#include <openssl/evp.h>
#include <openssl/rand.h>

#include <httplib.h>
#include <spdlog/spdlog.h>

// liboauth2 C headers
extern "C" {
#include <oauth2/jose.h>
#include <oauth2/mem.h>
#include <oauth2/oauth2.h>
}

#include <algorithm>
#include <sstream>
#include <vector>

namespace dns::security {

namespace {

// ── Base64url / SHA-256 helpers (used by PKCE and state generation) ─────────

std::string base64UrlEncode(const unsigned char* pData, size_t uLen) {
  EVP_ENCODE_CTX* pCtx = EVP_ENCODE_CTX_new();
  EVP_EncodeInit(pCtx);

  const int iMaxOut = static_cast<int>(uLen) * 2 + 64;
  std::vector<unsigned char> vOut(static_cast<size_t>(iMaxOut));
  int iOutLen = 0;
  int iTotalLen = 0;

  EVP_EncodeUpdate(pCtx, vOut.data(), &iOutLen, pData, static_cast<int>(uLen));
  iTotalLen += iOutLen;
  EVP_EncodeFinal(pCtx, vOut.data() + iTotalLen, &iOutLen);
  iTotalLen += iOutLen;
  EVP_ENCODE_CTX_free(pCtx);

  std::string sB64(reinterpret_cast<char*>(vOut.data()), static_cast<size_t>(iTotalLen));
  std::erase(sB64, '\n');
  for (auto& c : sB64) {
    if (c == '+') c = '-';
    else if (c == '/') c = '_';
  }
  while (!sB64.empty() && sB64.back() == '=') {
    sB64.pop_back();
  }
  return sB64;
}

std::string sha256Raw(const std::string& sInput) {
  unsigned char vHash[EVP_MAX_MD_SIZE];
  unsigned int uHashLen = 0;

  EVP_MD_CTX* pCtx = EVP_MD_CTX_new();
  EVP_DigestInit_ex(pCtx, EVP_sha256(), nullptr);
  EVP_DigestUpdate(pCtx, sInput.data(), sInput.size());
  EVP_DigestFinal_ex(pCtx, vHash, &uHashLen);
  EVP_MD_CTX_free(pCtx);

  return std::string(reinterpret_cast<char*>(vHash), uHashLen);
}

// ── URL helpers (used by buildAuthorizationUrl, discover, exchangeCode) ─────

std::string urlEncode(const std::string& sValue) {
  std::ostringstream oss;
  for (unsigned char c : sValue) {
    if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
      oss << c;
    } else {
      oss << '%' << std::uppercase << std::hex
          << static_cast<int>(c >> 4) << static_cast<int>(c & 0x0F);
    }
  }
  return oss.str();
}

/// Parse a URL into scheme+host and path components.
std::pair<std::string, std::string> parseUrl(const std::string& sUrl) {
  auto iSchemeEnd = sUrl.find("://");
  if (iSchemeEnd == std::string::npos) {
    throw common::ValidationError("INVALID_URL", "Invalid URL: " + sUrl);
  }
  auto iPathStart = sUrl.find('/', iSchemeEnd + 3);
  if (iPathStart == std::string::npos) {
    return {sUrl, "/"};
  }
  return {sUrl.substr(0, iPathStart), sUrl.substr(iPathStart)};
}

// ── liboauth2 log sink → spdlog bridge ─────────────────────────────────────

void oauth2SpdlogSink(oauth2_log_sink_t* /*pSink*/, const char* /*sFilename*/,
                      unsigned long /*uLine*/, const char* sFunction,
                      oauth2_log_level_t eLevel, const char* sMsg) {
  switch (eLevel) {
    case OAUTH2_LOG_ERROR:
      spdlog::error("[liboauth2] {}: {}", sFunction, sMsg);
      break;
    case OAUTH2_LOG_WARN:
      spdlog::warn("[liboauth2] {}: {}", sFunction, sMsg);
      break;
    case OAUTH2_LOG_NOTICE:
    case OAUTH2_LOG_INFO:
      spdlog::info("[liboauth2] {}: {}", sFunction, sMsg);
      break;
    case OAUTH2_LOG_DEBUG:
      spdlog::debug("[liboauth2] {}: {}", sFunction, sMsg);
      break;
    default:  // TRACE1, TRACE2
      spdlog::trace("[liboauth2] {}: {}", sFunction, sMsg);
      break;
  }
}

// ── Helper: convert jansson json_t* to nlohmann::json ───────────────────────

nlohmann::json janssonToNlohmann(json_t* pJson) {
  char* sStr = json_dumps(pJson, JSON_COMPACT | JSON_ENCODE_ANY);
  if (!sStr) {
    throw common::AuthenticationError("invalid_id_token",
                                      "Failed to serialize JWT payload from liboauth2");
  }
  nlohmann::json jResult;
  try {
    jResult = nlohmann::json::parse(sStr);
  } catch (...) {
    free(sStr);  // NOLINT — jansson uses malloc
    throw common::AuthenticationError("invalid_id_token",
                                      "Failed to parse JWT payload JSON");
  }
  free(sStr);  // NOLINT — jansson uses malloc
  return jResult;
}

constexpr auto kStateTtl = std::chrono::minutes(10);
constexpr auto kDiscoveryCacheTtl = std::chrono::hours(1);

}  // anonymous namespace

// ── OidcService ────────────────────────────────────────────────────────────

OidcService::OidcService() {
  // Create a custom log sink that routes liboauth2 messages through spdlog
  oauth2_log_sink_t* pSink =
      oauth2_log_sink_create(OAUTH2_LOG_DEBUG, oauth2SpdlogSink, nullptr);
  _pLog = oauth2_log_init(OAUTH2_LOG_DEBUG, pSink);
}

OidcService::~OidcService() {
  if (_pLog) {
    oauth2_log_free(_pLog);
    _pLog = nullptr;
  }
}

std::pair<std::string, std::string> OidcService::generatePkce() {
  // Generate 32 random bytes for verifier
  unsigned char vRandom[32];
  if (RAND_bytes(vRandom, sizeof(vRandom)) != 1) {
    throw std::runtime_error("RAND_bytes failed for PKCE verifier");
  }
  std::string sVerifier = base64UrlEncode(vRandom, sizeof(vRandom));

  // Challenge = base64url(SHA-256(verifier))
  std::string sHash = sha256Raw(sVerifier);
  std::string sChallenge = base64UrlEncode(
      reinterpret_cast<const unsigned char*>(sHash.data()), sHash.size());

  return {sVerifier, sChallenge};
}

std::string OidcService::generateState() {
  unsigned char vRandom[24];
  if (RAND_bytes(vRandom, sizeof(vRandom)) != 1) {
    throw std::runtime_error("RAND_bytes failed for OIDC state");
  }
  return base64UrlEncode(vRandom, sizeof(vRandom));
}

std::string OidcService::buildAuthorizationUrl(
    const std::string& sAuthEndpoint, const std::string& sClientId,
    const std::string& sRedirectUri, const std::string& sScope,
    const std::string& sState, const std::string& sCodeChallenge) {
  std::ostringstream oss;
  oss << sAuthEndpoint;
  oss << (sAuthEndpoint.find('?') != std::string::npos ? "&" : "?");
  oss << "response_type=code";
  oss << "&client_id=" << urlEncode(sClientId);
  oss << "&redirect_uri=" << urlEncode(sRedirectUri);
  oss << "&scope=" << urlEncode(sScope);
  oss << "&state=" << urlEncode(sState);
  oss << "&code_challenge=" << urlEncode(sCodeChallenge);
  oss << "&code_challenge_method=S256";
  return oss.str();
}

void OidcService::storeAuthState(const std::string& sState, OidcAuthState oaState) {
  std::lock_guard<std::mutex> lock(_mtxStates);
  evictExpiredStates();
  _mAuthStates.emplace(sState, std::move(oaState));
}

std::optional<OidcAuthState> OidcService::consumeAuthState(const std::string& sState) {
  std::lock_guard<std::mutex> lock(_mtxStates);
  evictExpiredStates();

  auto it = _mAuthStates.find(sState);
  if (it == _mAuthStates.end()) {
    return std::nullopt;
  }

  OidcAuthState oaState = std::move(it->second);
  _mAuthStates.erase(it);
  return oaState;
}

void OidcService::evictExpiredStates() {
  auto tpNow = std::chrono::system_clock::now();
  for (auto it = _mAuthStates.begin(); it != _mAuthStates.end();) {
    if (tpNow - it->second.tpCreatedAt > kStateTtl) {
      it = _mAuthStates.erase(it);
    } else {
      ++it;
    }
  }
}

OidcDiscovery OidcService::discover(const std::string& sIssuerUrl) {
  {
    std::lock_guard<std::mutex> lock(_mtxDiscovery);
    auto it = _mDiscoveryCache.find(sIssuerUrl);
    if (it != _mDiscoveryCache.end()) {
      auto tpAge = std::chrono::system_clock::now() - it->second.tpFetchedAt;
      if (tpAge < kDiscoveryCacheTtl) {
        return it->second;
      }
    }
  }

  // Fetch discovery document
  std::string sDiscoveryUrl = sIssuerUrl;
  if (!sDiscoveryUrl.empty() && sDiscoveryUrl.back() == '/') {
    sDiscoveryUrl.pop_back();
  }
  sDiscoveryUrl += "/.well-known/openid-configuration";

  auto [sHost, sPath] = parseUrl(sDiscoveryUrl);
  httplib::Client client(sHost);
  client.set_connection_timeout(10);
  client.set_read_timeout(10);

  auto res = client.Get(sPath);
  if (!res || res->status != 200) {
    throw common::AuthenticationError(
        "oidc_discovery_failed",
        "Failed to fetch OIDC discovery document from " + sDiscoveryUrl);
  }

  auto jDoc = nlohmann::json::parse(res->body);

  OidcDiscovery odDiscovery;
  odDiscovery.sAuthorizationEndpoint = jDoc.value("authorization_endpoint", "");
  odDiscovery.sTokenEndpoint = jDoc.value("token_endpoint", "");
  odDiscovery.sJwksUri = jDoc.value("jwks_uri", "");
  odDiscovery.sIssuer = jDoc.value("issuer", "");
  odDiscovery.tpFetchedAt = std::chrono::system_clock::now();

  if (odDiscovery.sAuthorizationEndpoint.empty() || odDiscovery.sTokenEndpoint.empty()) {
    throw common::AuthenticationError(
        "oidc_discovery_invalid",
        "OIDC discovery document missing required endpoints");
  }

  {
    std::lock_guard<std::mutex> lock(_mtxDiscovery);
    _mDiscoveryCache[sIssuerUrl] = odDiscovery;
  }

  spdlog::info("OIDC discovery fetched for issuer: {}", sIssuerUrl);
  return odDiscovery;
}

// ── Token Exchange & JWT Validation (liboauth2) ────────────────────────────

nlohmann::json OidcService::exchangeCode(const std::string& sTokenEndpoint,
                                         const std::string& sCode,
                                         const std::string& sClientId,
                                         const std::string& sClientSecret,
                                         const std::string& sRedirectUri,
                                         const std::string& sCodeVerifier) {
  auto [sHost, sPath] = parseUrl(sTokenEndpoint);
  httplib::Client client(sHost);
  client.set_connection_timeout(10);
  client.set_read_timeout(10);

  httplib::Params params;
  params.emplace("grant_type", "authorization_code");
  params.emplace("code", sCode);
  params.emplace("client_id", sClientId);
  params.emplace("redirect_uri", sRedirectUri);
  params.emplace("code_verifier", sCodeVerifier);
  if (!sClientSecret.empty()) {
    params.emplace("client_secret", sClientSecret);
  }

  auto res = client.Post(sPath, params);
  if (!res) {
    throw common::AuthenticationError(
        "oidc_token_exchange_failed",
        "Failed to connect to token endpoint: " + sTokenEndpoint);
  }
  if (res->status != 200) {
    spdlog::error("OIDC token exchange failed: status={}, body={}", res->status, res->body);
    throw common::AuthenticationError(
        "oidc_token_exchange_failed",
        "Token exchange failed with status " + std::to_string(res->status));
  }

  return nlohmann::json::parse(res->body);
}

nlohmann::json OidcService::validateIdToken(const std::string& sIdToken,
                                            const std::string& sJwksUri,
                                            const std::string& sExpectedIssuer,
                                            const std::string& sExpectedAudience) {
  // Configure a liboauth2 token verifier for the given JWKS URI.
  // Skip exp/iat/iss validation in liboauth2 — our validateIdTokenClaims()
  // handles those with application-specific error messages.
  oauth2_cfg_token_verify_t* pVerify = nullptr;
  char* sErr = oauth2_cfg_token_verify_add_options(
      _pLog, &pVerify, "jwks_uri", sJwksUri.c_str(),
      "verify.exp=skip&verify.iat=skip&verify.iss=skip");

  if (sErr != nullptr) {
    std::string sErrMsg(sErr);
    oauth2_mem_free(sErr);
    if (pVerify) {
      oauth2_cfg_token_verify_free(_pLog, pVerify);
    }
    throw common::AuthenticationError(
        "jwt_verify_config_failed",
        "Failed to configure JWT verifier: " + sErrMsg);
  }

  // Verify JWT signature via liboauth2/cjose — this handles:
  //   - JWKS fetching (with internal caching)
  //   - Key matching by kid
  //   - RS256, RS384, RS512, ES256, ES384, ES512, PS256, etc.
  //   - Signature verification
  json_t* pJsonPayload = nullptr;
  bool bOk = oauth2_token_verify(
      _pLog, nullptr, pVerify, sIdToken.c_str(), &pJsonPayload);

  oauth2_cfg_token_verify_free(_pLog, pVerify);

  if (!bOk || pJsonPayload == nullptr) {
    if (pJsonPayload) {
      json_decref(pJsonPayload);
    }
    throw common::AuthenticationError(
        "invalid_signature", "ID token signature verification failed");
  }

  // Convert jansson json_t* payload to nlohmann::json
  nlohmann::json jPayload = janssonToNlohmann(pJsonPayload);
  json_decref(pJsonPayload);

  // Validate claims (issuer, audience, expiry) with our error messages
  validateIdTokenClaims(jPayload, sExpectedIssuer, sExpectedAudience);

  return jPayload;
}

void OidcService::validateIdTokenClaims(const nlohmann::json& jPayload,
                                        const std::string& sExpectedIssuer,
                                        const std::string& sExpectedAudience) {
  // Validate issuer
  std::string sIss = jPayload.value("iss", "");
  if (sIss != sExpectedIssuer) {
    throw common::AuthenticationError(
        "invalid_issuer",
        "ID token issuer mismatch: expected " + sExpectedIssuer + ", got " + sIss);
  }

  // Validate audience — can be string or array
  bool bAudMatch = false;
  if (jPayload.contains("aud")) {
    if (jPayload["aud"].is_string()) {
      bAudMatch = (jPayload["aud"].get<std::string>() == sExpectedAudience);
    } else if (jPayload["aud"].is_array()) {
      for (const auto& aud : jPayload["aud"]) {
        if (aud.get<std::string>() == sExpectedAudience) {
          bAudMatch = true;
          break;
        }
      }
    }
  }
  if (!bAudMatch) {
    throw common::AuthenticationError(
        "invalid_audience", "ID token audience does not contain expected client_id");
  }

  // Validate expiry
  auto iNow = std::chrono::duration_cast<std::chrono::seconds>(
                   std::chrono::system_clock::now().time_since_epoch())
                   .count();
  int64_t iExp = jPayload.value("exp", static_cast<int64_t>(0));
  if (iExp <= iNow) {
    throw common::AuthenticationError("token_expired", "ID token has expired");
  }
}

}  // namespace dns::security
