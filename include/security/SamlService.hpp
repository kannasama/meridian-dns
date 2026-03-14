#pragma once

#include <chrono>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

#include <nlohmann/json.hpp>

// Forward-declare lasso types to avoid pulling glib into every TU.
typedef struct _LassoServer LassoServer;

namespace dns::security {

class SamlReplayCache;

/// State stored during SAML authorization flow (between redirect and ACS callback).
struct SamlAuthState {
  int64_t iIdpId = 0;
  std::string sRequestId;  // ID from AuthnRequest for InResponseTo validation
  bool bIsTestMode = false;
  std::chrono::system_clock::time_point tpCreatedAt;
};

/// Result of building a SAML login URL.
struct SamlLoginResult {
  std::string sRedirectUrl;  // Full redirect URL with SAMLRequest + RelayState
  std::string sRequestId;    // AuthnRequest ID for InResponseTo validation
};

/// Handles SAML 2.0 protocol operations using the lasso library.
///
/// Provides proper XMLDSig/C14N signature verification via lasso (xmlsec1).
/// Each registered IdP gets a dedicated LassoServer instance with SP+IdP metadata.
///
/// Class abbreviation: ss
class SamlService {
 public:
  explicit SamlService(SamlReplayCache& srcCache);
  ~SamlService();

  // Non-copyable (owns lasso resources)
  SamlService(const SamlService&) = delete;
  SamlService& operator=(const SamlService&) = delete;

  /// Initialize lasso library (call once at startup).
  /// Thread-safe: uses std::call_once internally.
  static void initLibrary();

  /// Register an SP+IdP pair. Must be called before protocol operations.
  /// @param iIdpId        database IdP identifier
  /// @param sSpEntityId   our SP entity ID
  /// @param sAcsUrl       our ACS endpoint URL
  /// @param sIdpEntityId  IdP entity ID (use SSO URL as fallback)
  /// @param sIdpSsoUrl    IdP SSO service URL (HTTP-Redirect)
  /// @param sIdpCertPem   IdP signing certificate (PEM or bare base64)
  /// @param sSpPrivateKeyPem  optional SP private key for signing AuthnRequests
  /// @param sIdpSloUrl    optional IdP SLO service URL (HTTP-Redirect)
  /// @param sSpSloUrl     optional SP SLO callback URL
  void registerIdp(int64_t iIdpId,
                   const std::string& sSpEntityId,
                   const std::string& sAcsUrl,
                   const std::string& sIdpEntityId,
                   const std::string& sIdpSsoUrl,
                   const std::string& sIdpCertPem,
                   const std::string& sSpPrivateKeyPem = "",
                   const std::string& sIdpSloUrl = "",
                   const std::string& sSpSloUrl = "");

  /// Check whether an IdP is already registered.
  bool isIdpRegistered(int64_t iIdpId) const;

  /// Generate AuthnRequest via lasso and return the redirect URL + request ID.
  SamlLoginResult buildLoginUrl(int64_t iIdpId, const std::string& sRelayState);

  /// Validate a SAML Response (signature, conditions, audience, replay).
  /// @return JSON with keys: name_id, attributes, session_index
  nlohmann::json validateResponse(int64_t iIdpId,
                                  const std::string& sSamlResponse,
                                  const std::string& sExpectedRequestId);

  /// Validate an IdP-initiated SAML response (no prior AuthnRequest).
  /// Skips InResponseTo validation since there was no originating request.
  /// @return JSON with keys: name_id, attributes, session_index
  nlohmann::json validateIdpInitiatedResponse(int64_t iIdpId,
                                              const std::string& sSamlResponse);

  /// Generate SP SAML metadata XML for a given IdP configuration.
  /// Includes EntityDescriptor, SPSSODescriptor, AssertionConsumerService,
  /// NameIDFormat, and optionally SingleLogoutService + SP signing certificate.
  std::string generateMetadata(int64_t iIdpId);

  /// Initiate SP-initiated SLO. Returns redirect URL to IdP's SLO endpoint.
  /// Returns empty string if SLO not configured for this IdP.
  /// @param sNameId       the user's NameID from the SAML assertion
  /// @param sSessionIndex the SAML session index from the AuthnStatement
  std::string initiateSlo(int64_t iIdpId, const std::string& sNameId,
                          const std::string& sSessionIndex);

  /// Process an incoming SLO request from the IdP.
  /// Returns a LogoutResponse URL to redirect back to the IdP.
  std::string processSloRequest(int64_t iIdpId, const std::string& sSloRequest);

  /// Auth state management (unchanged from previous implementation).
  void storeAuthState(const std::string& sRelayState, SamlAuthState saState);
  std::optional<SamlAuthState> consumeAuthState(const std::string& sRelayState);

  /// Base64 encode a string (used by tests to construct SAML responses).
  static std::string base64Encode(const std::string& sInput);

  /// Format a time_point as ISO 8601 UTC string (used by tests).
  static std::string formatIso8601(std::chrono::system_clock::time_point tp);

 private:
  void evictExpiredStates();

  /// Internal response validation shared by validateResponse and validateIdpInitiatedResponse.
  nlohmann::json validateResponseInternal(int64_t iIdpId,
                                          const std::string& sSamlResponse,
                                          const std::string& sExpectedRequestId,
                                          bool bCheckInResponseTo);

  /// Build minimal SAML 2.0 SP metadata XML for lasso.
  static std::string buildSpMetadata(const std::string& sSpEntityId,
                                     const std::string& sAcsUrl,
                                     const std::string& sSpSloUrl = "");

  /// Build minimal SAML 2.0 IdP metadata XML for lasso.
  static std::string buildIdpMetadata(const std::string& sIdpEntityId,
                                      const std::string& sIdpSsoUrl,
                                      const std::string& sIdpCertPem,
                                      const std::string& sIdpSloUrl = "");

  /// Strip PEM headers/footers and whitespace, returning bare base64 cert data.
  static std::string stripPemHeaders(const std::string& sPem);

  /// Per-IdP SP configuration stored for metadata generation.
  struct SpConfig {
    std::string sSpEntityId;
    std::string sAcsUrl;
    std::string sSpSloUrl;
    std::string sSpPrivateKeyPem;
    std::string sIdpSloUrl;
  };

  SamlReplayCache& _srcCache;

  std::mutex _mtxStates;
  std::unordered_map<std::string, SamlAuthState> _mAuthStates;

  mutable std::mutex _mtxIdps;
  std::unordered_map<int64_t, LassoServer*> _mServers;
  std::unordered_map<int64_t, SpConfig> _mSpConfigs;
};

}  // namespace dns::security
