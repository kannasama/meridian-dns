// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Meridian DNS Contributors
// This file is part of Meridian DNS. See LICENSE for details.

#include "security/HmacJwtSigner.hpp"

#include "common/Errors.hpp"
#include "security/CryptoService.hpp"

#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>

#include <chrono>
#include <cstring>
#include <sstream>
#include <vector>

namespace dns::security {

namespace {

std::string hmacSha256(const std::string& sKey, const std::string& sData) {
  unsigned char vHash[EVP_MAX_MD_SIZE];
  unsigned int uHashLen = 0;

  unsigned char* pResult = HMAC(
      EVP_sha256(),
      sKey.data(), static_cast<int>(sKey.size()),
      reinterpret_cast<const unsigned char*>(sData.data()),
      sData.size(),
      vHash, &uHashLen);

  if (!pResult) {
    throw std::runtime_error("HMAC-SHA256 computation failed");
  }

  return std::string(reinterpret_cast<char*>(vHash), uHashLen);
}

}  // anonymous namespace

// ── HmacJwtSigner ──────────────────────────────────────────────────────────

HmacJwtSigner::HmacJwtSigner(const std::string& sSecret) : _sSecret(sSecret) {
  if (_sSecret.empty()) {
    throw std::runtime_error("JWT secret cannot be empty");
  }
}

HmacJwtSigner::~HmacJwtSigner() {
  // Zero the secret from memory
  if (!_sSecret.empty()) {
    OPENSSL_cleanse(_sSecret.data(), _sSecret.size());
  }
}

std::string HmacJwtSigner::sign(const nlohmann::json& jPayload) const {
  // Header: {"alg":"HS256","typ":"JWT"}
  const nlohmann::json jHeader = {{"alg", "HS256"}, {"typ", "JWT"}};

  const std::string sHeader = CryptoService::base64UrlEncode(jHeader.dump());
  const std::string sPayload = CryptoService::base64UrlEncode(jPayload.dump());
  const std::string sSigningInput = sHeader + "." + sPayload;

  const std::string sRawSig = hmacSha256(_sSecret, sSigningInput);
  const std::string sSignature = CryptoService::base64UrlEncode(sRawSig);

  return sSigningInput + "." + sSignature;
}

nlohmann::json HmacJwtSigner::verify(const std::string& sToken) const {
  // Split into 3 parts
  const auto nDot1 = sToken.find('.');
  if (nDot1 == std::string::npos) {
    throw common::AuthenticationError("invalid_token", "Malformed JWT: missing first dot");
  }
  const auto nDot2 = sToken.find('.', nDot1 + 1);
  if (nDot2 == std::string::npos) {
    throw common::AuthenticationError("invalid_token", "Malformed JWT: missing second dot");
  }
  if (sToken.find('.', nDot2 + 1) != std::string::npos) {
    throw common::AuthenticationError("invalid_token", "Malformed JWT: too many dots");
  }

  const std::string sSigningInput = sToken.substr(0, nDot2);
  const std::string sProvidedSig = sToken.substr(nDot2 + 1);

  // Recompute signature
  const std::string sRawExpectedSig = hmacSha256(_sSecret, sSigningInput);
  const std::string sExpectedSig = CryptoService::base64UrlEncode(sRawExpectedSig);

  // Constant-time comparison to prevent timing attacks
  if (sExpectedSig.size() != sProvidedSig.size() ||
      CRYPTO_memcmp(sExpectedSig.data(), sProvidedSig.data(), sExpectedSig.size()) != 0) {
    throw common::AuthenticationError("invalid_signature", "JWT signature verification failed");
  }

  // Decode payload
  const std::string sPayloadB64 = sToken.substr(nDot1 + 1, nDot2 - nDot1 - 1);
  const std::string sPayloadJson = CryptoService::base64UrlDecode(sPayloadB64);

  nlohmann::json jPayload;
  try {
    jPayload = nlohmann::json::parse(sPayloadJson);
  } catch (const nlohmann::json::exception&) {
    throw common::AuthenticationError("invalid_token", "JWT payload is not valid JSON");
  }

  // Check expiry
  if (jPayload.contains("exp")) {
    const auto iExp = jPayload["exp"].get<int64_t>();
    const auto iNow = std::chrono::duration_cast<std::chrono::seconds>(
                          std::chrono::system_clock::now().time_since_epoch())
                          .count();
    if (iNow > iExp) {
      throw common::AuthenticationError("token_expired", "JWT has expired");
    }
  }

  return jPayload;
}

}  // namespace dns::security
