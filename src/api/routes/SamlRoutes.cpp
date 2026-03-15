#include "api/routes/SamlRoutes.hpp"

#include "api/RouteHelpers.hpp"
#include "common/Errors.hpp"
#include "dal/IdpRepository.hpp"
#include "dal/SessionRepository.hpp"
#include "security/CryptoService.hpp"
#include "security/FederatedAuthService.hpp"
#include "security/OidcService.hpp"
#include "security/SamlService.hpp"

#include <spdlog/spdlog.h>

#include <nlohmann/json.hpp>

namespace dns::api::routes {

SamlRoutes::SamlRoutes(dns::dal::IdpRepository& irRepo,
                       dns::dal::SessionRepository& srRepo,
                       dns::security::SamlService& ssService,
                       dns::security::FederatedAuthService& fasService)
    : _irRepo(irRepo), _srRepo(srRepo), _ssService(ssService), _fasService(fasService) {}

SamlRoutes::~SamlRoutes() = default;

void SamlRoutes::ensureIdpRegistered(int iIdpId) {
  if (_ssService.isIdpRegistered(iIdpId)) return;

  auto oIdp = _irRepo.findById(iIdpId);
  if (!oIdp.has_value()) {
    throw common::NotFoundError("IDP_NOT_FOUND", "Identity provider not found");
  }

  std::string sSpEntityId = oIdp->jConfig.value("entity_id", "");
  std::string sSsoUrl = oIdp->jConfig.value("sso_url", "");
  std::string sAcsUrl = oIdp->jConfig.value("assertion_consumer_service_url", "");
  std::string sCert = oIdp->jConfig.value("certificate", "");
  std::string sIdpEntityId = oIdp->jConfig.value("idp_entity_id", sSsoUrl);
  std::string sSpPrivateKey = oIdp->jConfig.value("sp_private_key", "");
  std::string sIdpSloUrl = oIdp->jConfig.value("slo_url", "");
  std::string sSpSloUrl = oIdp->jConfig.value("sp_slo_url", "");

  _ssService.registerIdp(iIdpId, sSpEntityId, sAcsUrl, sIdpEntityId, sSsoUrl,
                         sCert, sSpPrivateKey, sIdpSloUrl, sSpSloUrl);
}

void SamlRoutes::registerRoutes(crow::SimpleApp& app) {
  // ── GET /api/v1/auth/saml/<int>/login — initiate SAML flow ───────────────
  CROW_ROUTE(app, "/api/v1/auth/saml/<int>/login").methods("GET"_method)(
      [this](const crow::request&, int iIdpId) -> crow::response {
        try {
          auto oIdp = _irRepo.findById(iIdpId);
          if (!oIdp.has_value() || !oIdp->bIsEnabled || oIdp->sType != "saml") {
            throw common::NotFoundError("IDP_NOT_FOUND", "SAML identity provider not found");
          }

          // Lazy registration: ensure IdP is registered with lasso
          ensureIdpRegistered(iIdpId);

          // Generate relay state and build login URL
          auto sRelayState = dns::security::OidcService::generateState();
          auto [sUrl, sRequestId] = _ssService.buildLoginUrl(iIdpId, sRelayState);

          // Store SAML auth state for ACS callback
          dns::security::SamlAuthState saState;
          saState.iIdpId = iIdpId;
          saState.sRequestId = sRequestId;
          saState.bIsTestMode = false;
          saState.tpCreatedAt = std::chrono::system_clock::now();
          _ssService.storeAuthState(sRelayState, saState);

          // Return 302 redirect
          crow::response resp(302);
          resp.set_header("Location", sUrl);
          return resp;
        } catch (const common::AppError& e) {
          return errorResponse(e);
        }
      });

  // ── POST /api/v1/auth/saml/<int>/acs — Assertion Consumer Service ────────
  CROW_ROUTE(app, "/api/v1/auth/saml/<int>/acs").methods("POST"_method)(
      [this](const crow::request& req, int iIdpId) -> crow::response {
        try {
          // Parse URL-encoded form body
          std::string sSamlResponse;
          std::string sRelayState;

          // Crow provides url_params for query strings; for POST body we parse manually
          std::string sBody = req.body;
          auto parseFormParam = [&sBody](const std::string& sKey) -> std::string {
            std::string sSearch = sKey + "=";
            auto iStart = sBody.find(sSearch);
            if (iStart == std::string::npos) return "";
            iStart += sSearch.size();
            auto iEnd = sBody.find('&', iStart);
            std::string sValue = (iEnd != std::string::npos)
                                     ? sBody.substr(iStart, iEnd - iStart)
                                     : sBody.substr(iStart);
            // URL-decode
            std::string sDecoded;
            for (size_t i = 0; i < sValue.size(); ++i) {
              if (sValue[i] == '%' && i + 2 < sValue.size()) {
                int iHigh = 0, iLow = 0;
                auto hexVal = [](char c) -> int {
                  if (c >= '0' && c <= '9') return c - '0';
                  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                  return 0;
                };
                iHigh = hexVal(sValue[i + 1]);
                iLow = hexVal(sValue[i + 2]);
                sDecoded += static_cast<char>((iHigh << 4) | iLow);
                i += 2;
              } else if (sValue[i] == '+') {
                sDecoded += ' ';
              } else {
                sDecoded += sValue[i];
              }
            }
            return sDecoded;
          };

          sSamlResponse = parseFormParam("SAMLResponse");
          sRelayState = parseFormParam("RelayState");

          if (sSamlResponse.empty()) {
            throw common::ValidationError("MISSING_SAML_RESPONSE",
                                          "Missing SAMLResponse parameter");
          }

          // Consume auth state via relay state (SP-initiated flow)
          std::optional<dns::security::SamlAuthState> oState;
          if (!sRelayState.empty()) {
            oState = _ssService.consumeAuthState(sRelayState);
          }

          // Load IdP config
          auto oIdp = _irRepo.findById(iIdpId);
          if (!oIdp.has_value() || !oIdp->bIsEnabled || oIdp->sType != "saml") {
            throw common::NotFoundError("IDP_NOT_FOUND",
                                        "SAML identity provider not found");
          }

          // Ensure IdP is registered with lasso
          ensureIdpRegistered(iIdpId);

          nlohmann::json jResult;

          if (oState.has_value()) {
            // ── SP-initiated: validate with InResponseTo check ──────────────
            if (oState->iIdpId != iIdpId) {
              throw common::AuthenticationError("STATE_MISMATCH",
                                                "Relay state IdP ID does not match URL");
            }
            jResult = _ssService.validateResponse(
                iIdpId, sSamlResponse, oState->sRequestId);
          } else {
            // ── IdP-initiated: check if allowed for this IdP ────────────────
            if (!oIdp->jConfig.value("allow_idp_initiated", false)) {
              throw common::AuthenticationError(
                  "IDP_INITIATED_DISABLED",
                  "IdP-initiated login not enabled for this identity provider");
            }
            jResult = _ssService.validateIdpInitiatedResponse(iIdpId, sSamlResponse);
          }

          std::string sGroupAttribute = oIdp->jConfig.value("group_attribute", "groups");
          std::string sNameId = jResult.value("name_id", "");
          std::string sSessionIndex = jResult.value("session_index", "");
          auto& jAttributes = jResult["attributes"];

          // Extract groups
          std::vector<std::string> vGroups;
          if (jAttributes.contains(sGroupAttribute) &&
              jAttributes[sGroupAttribute].is_array()) {
            for (const auto& g : jAttributes[sGroupAttribute]) {
              vGroups.push_back(g.get<std::string>());
            }
          }

          // Read attribute mapping from IdP config
          auto jMapping = oIdp->jConfig.value("attribute_mapping", nlohmann::json::object());
          std::string sEmailAttr = jMapping.value("email", "email");
          std::string sUsernameAttr = jMapping.value("username", "");  // empty = use NameID
          std::string sDisplayNameAttr = jMapping.value("display_name", "displayName");

          // Extract email using configurable mapping
          std::string sEmail;
          if (jAttributes.contains(sEmailAttr) && jAttributes[sEmailAttr].is_array() &&
              !jAttributes[sEmailAttr].empty()) {
            sEmail = jAttributes[sEmailAttr][0].get<std::string>();
          }

          // Extract display name
          std::string sDisplayName;
          if (jAttributes.contains(sDisplayNameAttr) && jAttributes[sDisplayNameAttr].is_array() &&
              !jAttributes[sDisplayNameAttr].empty()) {
            sDisplayName = jAttributes[sDisplayNameAttr][0].get<std::string>();
          }

          // Username from mapping, then NameID, then email
          std::string sUsername;
          if (!sUsernameAttr.empty() && jAttributes.contains(sUsernameAttr) &&
              jAttributes[sUsernameAttr].is_array() && !jAttributes[sUsernameAttr].empty()) {
            sUsername = jAttributes[sUsernameAttr][0].get<std::string>();
          }
          if (sUsername.empty()) sUsername = sNameId;
          if (sUsername.empty()) sUsername = sEmail;

          // Test mode: return HTML page displaying raw attributes
          if (oState.has_value() && oState->bIsTestMode) {
            nlohmann::json jTestResult = {
                {"subject", sNameId},
                {"email", sEmail},
                {"username", sUsername},
                {"display_name", sDisplayName},
                {"groups", vGroups},
                {"all_claims", jAttributes},
            };
            std::string sHtml =
                "<!DOCTYPE html><html><head><title>SAML Test Result</title></head>"
                "<body><h1>SAML Test Result</h1>"
                "<pre>" + jTestResult.dump(2) + "</pre>"
                "<script>if(window.opener){window.opener.postMessage(" +
                jTestResult.dump() + ",'*');}</script>"
                "</body></html>";
            crow::response resp(200);
            resp.set_header("Content-Type", "text/html");
            resp.body = sHtml;
            return resp;
          }

          // Process federated login
          auto lr = _fasService.processFederatedLogin(
              "saml", sNameId, sUsername, sEmail, sDisplayName, vGroups,
              oIdp->jGroupMappings, oIdp->iDefaultGroupId);

          // Store SAML session index on the session for SLO support
          if (!sSessionIndex.empty()) {
            std::string sTokenHash = dns::security::CryptoService::sha256Hex(lr.sToken);
            _srRepo.setSamlSessionIndex(sTokenHash, sSessionIndex);
          }

          // Return HTML page with JavaScript redirect (SAML POST binding can't redirect directly)
          std::string sHtml =
              "<!DOCTYPE html><html><head><title>Signing in...</title></head>"
              "<body><p>Signing in...</p>"
              "<script>window.location.replace('/auth/callback?token=" +
              lr.sToken + "');</script>"
              "</body></html>";
          crow::response resp(200);
          resp.set_header("Content-Type", "text/html");
          resp.body = sHtml;
          return resp;
        } catch (const common::AppError& e) {
          spdlog::error("SAML ACS error: {}", e.what());
          // Return HTML error page
          std::string sHtml =
              "<!DOCTYPE html><html><head><title>Authentication Error</title></head>"
              "<body><p>Authentication failed.</p>"
              "<script>window.location.replace('/login?error=auth_failed');</script>"
              "</body></html>";
          crow::response resp(200);
          resp.set_header("Content-Type", "text/html");
          resp.body = sHtml;
          return resp;
        }
      });

  // ── GET /api/v1/auth/saml/<int>/metadata — SP metadata endpoint ──────────
  CROW_ROUTE(app, "/api/v1/auth/saml/<int>/metadata").methods("GET"_method)(
      [this](const crow::request&, int iIdpId) -> crow::response {
        try {
          auto oIdp = _irRepo.findById(iIdpId);
          if (!oIdp.has_value() || !oIdp->bIsEnabled || oIdp->sType != "saml") {
            throw common::NotFoundError("IDP_NOT_FOUND", "SAML identity provider not found");
          }

          // Ensure IdP is registered so metadata can be generated
          ensureIdpRegistered(iIdpId);

          std::string sMetadata = _ssService.generateMetadata(iIdpId);

          crow::response resp(200);
          resp.set_header("Content-Type", "application/samlmetadata+xml");
          resp.set_header("Cache-Control", "public, max-age=86400");
          resp.body = sMetadata;
          return resp;
        } catch (const common::AppError& e) {
          return errorResponse(e);
        }
      });

  // ── GET /api/v1/auth/saml/<int>/slo — initiate SP-initiated SLO ─────────
  CROW_ROUTE(app, "/api/v1/auth/saml/<int>/slo").methods("GET"_method)(
      [this](const crow::request& req, int iIdpId) -> crow::response {
        try {
          auto oIdp = _irRepo.findById(iIdpId);
          if (!oIdp.has_value() || !oIdp->bIsEnabled || oIdp->sType != "saml") {
            throw common::NotFoundError("IDP_NOT_FOUND", "SAML identity provider not found");
          }

          // Check SLO is configured for this IdP
          std::string sIdpSloUrl = oIdp->jConfig.value("slo_url", "");
          if (sIdpSloUrl.empty()) {
            throw common::ValidationError("SLO_NOT_CONFIGURED",
                                          "Single Logout not configured for this IdP");
          }

          // Get NameID and SessionIndex from query parameters
          // These should be provided by the frontend from the current session context
          std::string sNameId;
          std::string sSessionIndex;
          if (req.url_params.get("name_id") != nullptr) {
            sNameId = req.url_params.get("name_id");
          }
          if (req.url_params.get("session_index") != nullptr) {
            sSessionIndex = req.url_params.get("session_index");
          }

          if (sNameId.empty()) {
            throw common::ValidationError("MISSING_NAME_ID",
                                          "NameID is required for SLO");
          }

          // Ensure IdP is registered
          ensureIdpRegistered(iIdpId);

          std::string sSloUrl = _ssService.initiateSlo(iIdpId, sNameId, sSessionIndex);
          if (sSloUrl.empty()) {
            throw common::ValidationError("SLO_NOT_CONFIGURED",
                                          "Single Logout not configured for this IdP");
          }

          // Redirect to IdP's SLO endpoint
          crow::response resp(302);
          resp.set_header("Location", sSloUrl);
          return resp;
        } catch (const common::AppError& e) {
          return errorResponse(e);
        }
      });

  // ── GET /api/v1/auth/saml/<int>/slo/callback — SLO response from IdP ────
  CROW_ROUTE(app, "/api/v1/auth/saml/<int>/slo/callback").methods("GET"_method)(
      [this](const crow::request&, int /*iIdpId*/) -> crow::response {
        // IdP redirects here after processing our SP-initiated LogoutRequest.
        // The user's local session should already have been destroyed before
        // initiating SLO. Redirect to login page.
        crow::response resp(302);
        resp.set_header("Location", "/login?slo=success");
        return resp;
      });

  // ── POST /api/v1/auth/saml/<int>/slo/callback — IdP-initiated SLO ───────
  CROW_ROUTE(app, "/api/v1/auth/saml/<int>/slo/callback").methods("POST"_method)(
      [this](const crow::request& req, int iIdpId) -> crow::response {
        try {
          auto oIdp = _irRepo.findById(iIdpId);
          if (!oIdp.has_value() || !oIdp->bIsEnabled || oIdp->sType != "saml") {
            throw common::NotFoundError("IDP_NOT_FOUND", "SAML identity provider not found");
          }

          // Parse the SLO request from the POST body
          // IdP-initiated SLO comes as a SAMLRequest via HTTP-POST or query param
          std::string sSloRequest;

          // Check query params first (HTTP-Redirect binding)
          if (req.url_params.get("SAMLRequest") != nullptr) {
            sSloRequest = req.url_params.get("SAMLRequest");
          }

          // Fall back to POST body parsing
          if (sSloRequest.empty()) {
            std::string sBody = req.body;
            auto iStart = sBody.find("SAMLRequest=");
            if (iStart != std::string::npos) {
              iStart += 12;  // length of "SAMLRequest="
              auto iEnd = sBody.find('&', iStart);
              sSloRequest = (iEnd != std::string::npos)
                                ? sBody.substr(iStart, iEnd - iStart)
                                : sBody.substr(iStart);
            }
          }

          if (sSloRequest.empty()) {
            throw common::ValidationError("MISSING_SLO_REQUEST",
                                          "Missing SAMLRequest parameter");
          }

          // Ensure IdP is registered
          ensureIdpRegistered(iIdpId);

          // Process the SLO request — this validates and builds a response
          std::string sResponseUrl = _ssService.processSloRequest(iIdpId, sSloRequest);

          // Destroy local sessions matching the SAML session index
          // The SLO request should contain a SessionIndex; for now, we rely on
          // the response URL to redirect back. Session cleanup happens based on
          // any SessionIndex we can extract.
          // Note: Full SessionIndex extraction from the LogoutRequest would require
          // additional lasso API calls — for now the IdP-initiated SLO destroys
          // sessions via the processSloRequest flow.

          spdlog::info("IdP-initiated SLO processed for IdP {}", iIdpId);

          if (!sResponseUrl.empty()) {
            crow::response resp(302);
            resp.set_header("Location", sResponseUrl);
            return resp;
          }

          // If no redirect URL, return a simple success response
          crow::response resp(200);
          resp.set_header("Content-Type", "text/plain");
          resp.body = "Logout successful";
          return resp;
        } catch (const common::AppError& e) {
          spdlog::error("SAML SLO callback error: {}", e.what());
          return errorResponse(e);
        }
      });
}

}  // namespace dns::api::routes
