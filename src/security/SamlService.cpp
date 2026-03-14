#include "security/SamlService.hpp"

#include "common/Errors.hpp"
#include "security/SamlReplayCache.hpp"

#include <spdlog/spdlog.h>

// lasso C headers
extern "C" {
#include <lasso/lasso.h>
#include <lasso/xml/saml-2.0/saml2_assertion.h>
#include <lasso/xml/saml-2.0/saml2_attribute.h>
#include <lasso/xml/saml-2.0/saml2_attribute_statement.h>
#include <lasso/xml/saml-2.0/saml2_attribute_value.h>
#include <lasso/xml/saml-2.0/saml2_authn_statement.h>
#include <lasso/xml/saml-2.0/saml2_name_id.h>
#include <lasso/xml/saml-2.0/saml2_subject.h>
#include <lasso/xml/saml-2.0/samlp2_authn_request.h>
#include <lasso/xml/saml-2.0/samlp2_name_id_policy.h>
#include <lasso/xml/saml-2.0/samlp2_response.h>
#include <lasso/xml/saml-2.0/samlp2_status_response.h>
#include <lasso/xml/misc_text_node.h>
}

#include <openssl/evp.h>

#include <algorithm>
#include <ctime>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <vector>

namespace dns::security {

namespace {

constexpr auto kStateTtl = std::chrono::minutes(10);
std::once_flag s_lassoInitFlag;

/// Base64 encode raw bytes using OpenSSL.
std::string base64EncodeBytes(const unsigned char* pData, size_t uLen) {
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

  std::string sResult(reinterpret_cast<char*>(vOut.data()), static_cast<size_t>(iTotalLen));
  std::erase(sResult, '\n');
  return sResult;
}

/// Helper: safe null-to-empty-string conversion for C strings.
std::string safeStr(const char* p) {
  return p != nullptr ? std::string(p) : std::string();
}

/// Helper: extract text content from a LassoNode that may be a MiscTextNode.
std::string extractNodeText(LassoNode* pNode) {
  if (pNode == nullptr) return "";

  if (LASSO_IS_MISC_TEXT_NODE(pNode)) {
    auto* pText = LASSO_MISC_TEXT_NODE(pNode);
    return safeStr(pText->content);
  }

  // Try to export node to XML and extract text content
  gchar* pDump = lasso_node_dump(pNode);
  if (pDump != nullptr) {
    std::string sDump(pDump);
    g_free(pDump);
    // Strip XML tags, return inner text
    auto iStart = sDump.find('>');
    if (iStart != std::string::npos) {
      auto iEnd = sDump.rfind('<');
      if (iEnd != std::string::npos && iEnd > iStart) {
        return sDump.substr(iStart + 1, iEnd - iStart - 1);
      }
    }
    return sDump;
  }
  return "";
}

}  // anonymous namespace

// ── SamlService ────────────────────────────────────────────────────────────

SamlService::SamlService(SamlReplayCache& srcCache) : _srcCache(srcCache) {
  initLibrary();
}

SamlService::~SamlService() {
  std::lock_guard<std::mutex> lock(_mtxIdps);
  for (auto& [iId, pServer] : _mServers) {
    if (pServer != nullptr) {
      lasso_server_destroy(pServer);
    }
  }
  _mServers.clear();
}

void SamlService::initLibrary() {
  std::call_once(s_lassoInitFlag, []() {
    int iRc = lasso_init();
    if (iRc != 0) {
      spdlog::error("lasso_init() failed with code {}", iRc);
      throw std::runtime_error("Failed to initialize lasso library");
    }
    spdlog::info("lasso library initialized");
  });
}

// ── Static helpers ─────────────────────────────────────────────────────────

std::string SamlService::base64Encode(const std::string& sInput) {
  return base64EncodeBytes(reinterpret_cast<const unsigned char*>(sInput.data()),
                           sInput.size());
}

std::string SamlService::formatIso8601(std::chrono::system_clock::time_point tp) {
  auto tTime = std::chrono::system_clock::to_time_t(tp);
  std::tm tmUtc{};
  gmtime_r(&tTime, &tmUtc);
  std::ostringstream oss;
  oss << std::put_time(&tmUtc, "%Y-%m-%dT%H:%M:%SZ");
  return oss.str();
}

std::string SamlService::stripPemHeaders(const std::string& sPem) {
  std::string sResult;
  std::istringstream iss(sPem);
  std::string sLine;
  while (std::getline(iss, sLine)) {
    // Strip CR if present
    if (!sLine.empty() && sLine.back() == '\r') {
      sLine.pop_back();
    }
    // Skip PEM header/footer lines
    if (sLine.find("-----BEGIN") != std::string::npos) continue;
    if (sLine.find("-----END") != std::string::npos) continue;
    if (sLine.empty()) continue;
    sResult += sLine;
  }
  // If input had no PEM headers, it's already bare base64
  if (sResult.empty()) {
    // Remove all whitespace from original
    sResult.reserve(sPem.size());
    for (char c : sPem) {
      if (!std::isspace(static_cast<unsigned char>(c))) {
        sResult += c;
      }
    }
  }
  return sResult;
}

// ── Metadata generation ────────────────────────────────────────────────────

std::string SamlService::buildSpMetadata(const std::string& sSpEntityId,
                                         const std::string& sAcsUrl) {
  std::ostringstream oss;
  oss << R"(<?xml version="1.0" encoding="UTF-8"?>)"
      << R"(<EntityDescriptor entityID=")" << sSpEntityId
      << R"(" xmlns="urn:oasis:names:tc:SAML:2.0:metadata">)"
      << R"(<SPSSODescriptor AuthnRequestsSigned="false" )"
      << R"(protocolSupportEnumeration="urn:oasis:names:tc:SAML:2.0:protocol">)"
      << R"(<AssertionConsumerService )"
      << R"(Binding="urn:oasis:names:tc:SAML:2.0:bindings:HTTP-POST" )"
      << R"(Location=")" << sAcsUrl << R"(" )"
      << R"(index="0" isDefault="true"/>)"
      << R"(</SPSSODescriptor>)"
      << R"(</EntityDescriptor>)";
  return oss.str();
}

std::string SamlService::buildIdpMetadata(const std::string& sIdpEntityId,
                                          const std::string& sIdpSsoUrl,
                                          const std::string& sIdpCertPem) {
  std::string sCertBase64 = stripPemHeaders(sIdpCertPem);

  std::ostringstream oss;
  oss << R"(<?xml version="1.0" encoding="UTF-8"?>)"
      << R"(<EntityDescriptor entityID=")" << sIdpEntityId
      << R"(" xmlns="urn:oasis:names:tc:SAML:2.0:metadata">)"
      << R"(<IDPSSODescriptor )"
      << R"(protocolSupportEnumeration="urn:oasis:names:tc:SAML:2.0:protocol">)"
      << R"(<KeyDescriptor use="signing">)"
      << R"(<ds:KeyInfo xmlns:ds="http://www.w3.org/2000/09/xmldsig#">)"
      << R"(<ds:X509Data>)"
      << R"(<ds:X509Certificate>)" << sCertBase64 << R"(</ds:X509Certificate>)"
      << R"(</ds:X509Data>)"
      << R"(</ds:KeyInfo>)"
      << R"(</KeyDescriptor>)"
      << R"(<SingleSignOnService )"
      << R"(Binding="urn:oasis:names:tc:SAML:2.0:bindings:HTTP-Redirect" )"
      << R"(Location=")" << sIdpSsoUrl << R"("/>)"
      << R"(</IDPSSODescriptor>)"
      << R"(</EntityDescriptor>)";
  return oss.str();
}

// ── IdP registration ───────────────────────────────────────────────────────

void SamlService::registerIdp(int64_t iIdpId,
                              const std::string& sSpEntityId,
                              const std::string& sAcsUrl,
                              const std::string& sIdpEntityId,
                              const std::string& sIdpSsoUrl,
                              const std::string& sIdpCertPem,
                              const std::string& sSpPrivateKeyPem) {
  std::lock_guard<std::mutex> lock(_mtxIdps);

  // Destroy existing registration if any
  auto it = _mServers.find(iIdpId);
  if (it != _mServers.end()) {
    if (it->second != nullptr) {
      lasso_server_destroy(it->second);
    }
    _mServers.erase(it);
  }

  // Generate metadata XML
  std::string sSpMeta = buildSpMetadata(sSpEntityId, sAcsUrl);
  std::string sIdpMeta = buildIdpMetadata(sIdpEntityId, sIdpSsoUrl, sIdpCertPem);

  spdlog::debug("Registering SAML IdP {} (entity={})", iIdpId, sIdpEntityId);

  // Create lasso server from SP metadata buffer
  const gchar* pSpPrivKey = sSpPrivateKeyPem.empty() ? nullptr : sSpPrivateKeyPem.c_str();
  LassoServer* pServer = lasso_server_new_from_buffers(
      sSpMeta.c_str(),
      pSpPrivKey,   // private_key_content (optional)
      nullptr,      // private_key_password
      nullptr);     // certificate_content

  if (pServer == nullptr) {
    throw common::AuthenticationError(
        "saml_server_init",
        "Failed to create lasso server for IdP " + std::to_string(iIdpId));
  }

  // Add the IdP provider from metadata buffer
  int iRc = lasso_server_add_provider_from_buffer(
      pServer,
      LASSO_PROVIDER_ROLE_IDP,
      sIdpMeta.c_str(),
      nullptr,   // public_key
      nullptr);  // ca_cert_chain

  if (iRc != 0) {
    spdlog::error("lasso_server_add_provider_from_buffer failed: {} (code {})",
                  lasso_strerror(iRc) ? lasso_strerror(iRc) : "unknown", iRc);
    lasso_server_destroy(pServer);
    throw common::AuthenticationError(
        "saml_provider_add",
        "Failed to add IdP provider for " + std::to_string(iIdpId) +
            ": code " + std::to_string(iRc));
  }

  _mServers[iIdpId] = pServer;
  spdlog::info("SAML IdP {} registered (SP={}, IdP={})", iIdpId, sSpEntityId, sIdpEntityId);
}

bool SamlService::isIdpRegistered(int64_t iIdpId) const {
  std::lock_guard<std::mutex> lock(_mtxIdps);
  return _mServers.contains(iIdpId);
}

// ── SP-initiated login ─────────────────────────────────────────────────────

SamlLoginResult SamlService::buildLoginUrl(int64_t iIdpId,
                                           const std::string& sRelayState) {
  LassoServer* pServer = nullptr;
  {
    std::lock_guard<std::mutex> lock(_mtxIdps);
    auto it = _mServers.find(iIdpId);
    if (it == _mServers.end() || it->second == nullptr) {
      throw common::AuthenticationError(
          "saml_idp_not_registered",
          "SAML IdP " + std::to_string(iIdpId) + " is not registered");
    }
    pServer = it->second;
  }

  // Create login profile
  LassoLogin* pLogin = lasso_login_new(pServer);
  if (pLogin == nullptr) {
    throw common::AuthenticationError("saml_login_new", "Failed to create lasso login");
  }

  // Find the IdP entity ID from the server's providers
  // Use the first (and only) IdP we registered
  const gchar* pIdpEntityId = nullptr;
  {
    auto* pProviders = g_hash_table_get_keys(pServer->providers);
    if (pProviders != nullptr && pProviders->data != nullptr) {
      pIdpEntityId = static_cast<const gchar*>(pProviders->data);
    }
    g_list_free(pProviders);
  }

  if (pIdpEntityId == nullptr) {
    g_object_unref(pLogin);
    throw common::AuthenticationError("saml_no_idp", "No IdP registered in lasso server");
  }

  // Initialize the AuthnRequest
  int iRc = lasso_login_init_authn_request(pLogin, pIdpEntityId,
                                           LASSO_HTTP_METHOD_REDIRECT);
  if (iRc != 0) {
    spdlog::error("lasso_login_init_authn_request: {} (code {})",
                  lasso_strerror(iRc) ? lasso_strerror(iRc) : "unknown", iRc);
    g_object_unref(pLogin);
    throw common::AuthenticationError("saml_init_authn",
                                      "Failed to init AuthnRequest: code " +
                                          std::to_string(iRc));
  }

  // Configure NameIDPolicy
  auto* pRequest = LASSO_SAMLP2_AUTHN_REQUEST(LASSO_PROFILE(pLogin)->request);
  if (pRequest != nullptr && pRequest->NameIDPolicy != nullptr) {
    g_free(pRequest->NameIDPolicy->Format);
    pRequest->NameIDPolicy->Format =
        g_strdup(LASSO_SAML2_NAME_IDENTIFIER_FORMAT_UNSPECIFIED);
    pRequest->NameIDPolicy->AllowCreate = TRUE;
  }

  // Set relay state on the profile
  if (!sRelayState.empty()) {
    g_free(LASSO_PROFILE(pLogin)->msg_relayState);
    LASSO_PROFILE(pLogin)->msg_relayState = g_strdup(sRelayState.c_str());
  }

  // Build the AuthnRequest message (redirect URL)
  iRc = lasso_login_build_authn_request_msg(pLogin);
  if (iRc != 0) {
    spdlog::error("lasso_login_build_authn_request_msg: {} (code {})",
                  lasso_strerror(iRc) ? lasso_strerror(iRc) : "unknown", iRc);
    g_object_unref(pLogin);
    throw common::AuthenticationError("saml_build_authn",
                                      "Failed to build AuthnRequest: code " +
                                          std::to_string(iRc));
  }

  // Extract redirect URL and request ID
  SamlLoginResult result;
  result.sRedirectUrl = safeStr(LASSO_PROFILE(pLogin)->msg_url);
  result.sRequestId = safeStr(
      LASSO_SAMLP2_REQUEST_ABSTRACT(LASSO_PROFILE(pLogin)->request)->ID);

  spdlog::debug("SAML AuthnRequest built: id={}, url_prefix={}...",
                result.sRequestId,
                result.sRedirectUrl.substr(0, std::min(result.sRedirectUrl.size(),
                                                       static_cast<size_t>(80))));

  g_object_unref(pLogin);
  return result;
}

// ── Response validation ────────────────────────────────────────────────────

nlohmann::json SamlService::validateResponse(int64_t iIdpId,
                                             const std::string& sSamlResponse,
                                             const std::string& sExpectedRequestId) {
  LassoServer* pServer = nullptr;
  {
    std::lock_guard<std::mutex> lock(_mtxIdps);
    auto it = _mServers.find(iIdpId);
    if (it == _mServers.end() || it->second == nullptr) {
      throw common::AuthenticationError(
          "saml_idp_not_registered",
          "SAML IdP " + std::to_string(iIdpId) + " is not registered");
    }
    pServer = it->second;
  }

  // Create login profile for processing the response
  LassoLogin* pLogin = lasso_login_new(pServer);
  if (pLogin == nullptr) {
    throw common::AuthenticationError("saml_login_new",
                                      "Failed to create lasso login for response");
  }

  // Process the SAML response (base64-encoded)
  // lasso handles base64 decoding, XML parsing, signature verification via xmlsec1
  int iRc = lasso_login_process_authn_response_msg(pLogin,
                                                   const_cast<gchar*>(sSamlResponse.c_str()));
  if (iRc != 0) {
    std::string sErr = safeStr(lasso_strerror(iRc));
    spdlog::error("lasso_login_process_authn_response_msg: {} (code {})", sErr, iRc);
    g_object_unref(pLogin);
    throw common::AuthenticationError(
        "saml_response_invalid",
        "Failed to process SAML response: " + sErr + " (code " + std::to_string(iRc) + ")");
  }

  // Validate InResponseTo if we have an expected request ID
  if (!sExpectedRequestId.empty()) {
    auto* pResponse = LASSO_SAMLP2_STATUS_RESPONSE(LASSO_PROFILE(pLogin)->response);
    if (pResponse != nullptr && pResponse->InResponseTo != nullptr) {
      std::string sInResponseTo(pResponse->InResponseTo);
      if (sInResponseTo != sExpectedRequestId) {
        spdlog::error("SAML InResponseTo mismatch: expected={}, got={}",
                      sExpectedRequestId, sInResponseTo);
        g_object_unref(pLogin);
        throw common::AuthenticationError(
            "saml_request_id_mismatch",
            "SAML InResponseTo mismatch: expected " + sExpectedRequestId);
      }
    }
  }

  // Accept SSO — validates conditions (audience, time), creates identity/session
  iRc = lasso_login_accept_sso(pLogin);
  if (iRc != 0) {
    std::string sErr = safeStr(lasso_strerror(iRc));
    spdlog::error("lasso_login_accept_sso: {} (code {})", sErr, iRc);
    g_object_unref(pLogin);
    throw common::AuthenticationError(
        "saml_accept_sso_failed",
        "SAML SSO validation failed: " + sErr + " (code " + std::to_string(iRc) + ")");
  }

  // ── Extract NameID ──────────────────────────────────────────────────────
  std::string sNameId;
  auto* pNameIdNode = LASSO_PROFILE(pLogin)->nameIdentifier;
  if (pNameIdNode != nullptr && LASSO_IS_SAML2_NAME_ID(pNameIdNode)) {
    auto* pNameId = LASSO_SAML2_NAME_ID(pNameIdNode);
    sNameId = safeStr(pNameId->content);
  }

  // ── Extract attributes and session index from assertions ────────────────
  nlohmann::json jAttributes = nlohmann::json::object();
  std::string sSessionIndex;

  auto* pResponseNode = LASSO_PROFILE(pLogin)->response;
  if (pResponseNode != nullptr && LASSO_IS_SAMLP2_RESPONSE(pResponseNode)) {
    auto* pResponse = LASSO_SAMLP2_RESPONSE(pResponseNode);

    for (GList* pAssertionItem = pResponse->Assertion;
         pAssertionItem != nullptr;
         pAssertionItem = g_list_next(pAssertionItem)) {
      if (!LASSO_IS_SAML2_ASSERTION(pAssertionItem->data)) continue;
      auto* pAssertion = LASSO_SAML2_ASSERTION(pAssertionItem->data);

      // ── Replay detection ──────────────────────────────────────────────
      std::string sAssertionId = safeStr(pAssertion->ID);
      if (!sAssertionId.empty()) {
        auto tpExpiry = std::chrono::system_clock::now() + std::chrono::hours(1);
        if (!_srcCache.checkAndInsert(sAssertionId, tpExpiry)) {
          g_object_unref(pLogin);
          throw common::AuthenticationError("saml_replay",
                                            "SAML assertion replay detected");
        }
      }

      // ── Session index from AuthnStatement ─────────────────────────────
      for (GList* pStmtItem = pAssertion->AuthnStatement;
           pStmtItem != nullptr;
           pStmtItem = g_list_next(pStmtItem)) {
        if (!LASSO_IS_SAML2_AUTHN_STATEMENT(pStmtItem->data)) continue;
        auto* pAuthnStmt = LASSO_SAML2_AUTHN_STATEMENT(pStmtItem->data);
        if (pAuthnStmt->SessionIndex != nullptr && sSessionIndex.empty()) {
          sSessionIndex = pAuthnStmt->SessionIndex;
        }
      }

      // ── If NameID not found on profile, try the assertion subject ─────
      if (sNameId.empty() && pAssertion->Subject != nullptr &&
          pAssertion->Subject->NameID != nullptr) {
        sNameId = safeStr(pAssertion->Subject->NameID->content);
      }

      // ── AttributeStatements ───────────────────────────────────────────
      for (GList* pAttrStmtItem = pAssertion->AttributeStatement;
           pAttrStmtItem != nullptr;
           pAttrStmtItem = g_list_next(pAttrStmtItem)) {
        if (!LASSO_IS_SAML2_ATTRIBUTE_STATEMENT(pAttrStmtItem->data)) continue;
        auto* pAttrStmt = LASSO_SAML2_ATTRIBUTE_STATEMENT(pAttrStmtItem->data);

        for (GList* pAttrItem = pAttrStmt->Attribute;
             pAttrItem != nullptr;
             pAttrItem = g_list_next(pAttrItem)) {
          if (!LASSO_IS_SAML2_ATTRIBUTE(pAttrItem->data)) continue;
          auto* pAttr = LASSO_SAML2_ATTRIBUTE(pAttrItem->data);

          std::string sAttrName = safeStr(pAttr->Name);
          if (sAttrName.empty()) continue;

          nlohmann::json jValues = nlohmann::json::array();

          for (GList* pValItem = pAttr->AttributeValue;
               pValItem != nullptr;
               pValItem = g_list_next(pValItem)) {
            if (pValItem->data == nullptr) continue;

            if (LASSO_IS_SAML2_ATTRIBUTE_VALUE(pValItem->data)) {
              auto* pAttrVal = LASSO_SAML2_ATTRIBUTE_VALUE(pValItem->data);
              // Each AttributeValue can contain multiple child nodes
              for (GList* pAnyItem = pAttrVal->any;
                   pAnyItem != nullptr;
                   pAnyItem = g_list_next(pAnyItem)) {
                if (pAnyItem->data == nullptr) continue;
                std::string sText = extractNodeText(LASSO_NODE(pAnyItem->data));
                if (!sText.empty()) {
                  jValues.push_back(sText);
                }
              }
              // If AttributeValue had no children but may have text content
              if (pAttrVal->any == nullptr) {
                std::string sText = extractNodeText(LASSO_NODE(pAttrVal));
                if (!sText.empty()) {
                  jValues.push_back(sText);
                }
              }
            } else {
              // Might be a text node directly
              std::string sText = extractNodeText(LASSO_NODE(pValItem->data));
              if (!sText.empty()) {
                jValues.push_back(sText);
              }
            }
          }

          jAttributes[sAttrName] = jValues;
        }
      }
    }
  }

  g_object_unref(pLogin);

  spdlog::debug("SAML response validated: name_id={}, session_index={}, attrs={}",
                sNameId, sSessionIndex, jAttributes.size());

  return {
      {"name_id", sNameId},
      {"attributes", jAttributes},
      {"session_index", sSessionIndex},
  };
}

// ── Auth state management ──────────────────────────────────────────────────

void SamlService::storeAuthState(const std::string& sRelayState, SamlAuthState saState) {
  std::lock_guard<std::mutex> lock(_mtxStates);
  evictExpiredStates();
  _mAuthStates.emplace(sRelayState, std::move(saState));
}

std::optional<SamlAuthState> SamlService::consumeAuthState(const std::string& sRelayState) {
  std::lock_guard<std::mutex> lock(_mtxStates);
  evictExpiredStates();

  auto it = _mAuthStates.find(sRelayState);
  if (it == _mAuthStates.end()) {
    return std::nullopt;
  }

  SamlAuthState saState = std::move(it->second);
  _mAuthStates.erase(it);
  return saState;
}

void SamlService::evictExpiredStates() {
  auto tpNow = std::chrono::system_clock::now();
  for (auto it = _mAuthStates.begin(); it != _mAuthStates.end();) {
    if (tpNow - it->second.tpCreatedAt > kStateTtl) {
      it = _mAuthStates.erase(it);
    } else {
      ++it;
    }
  }
}

}  // namespace dns::security
