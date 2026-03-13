#include "security/FederatedAuthService.hpp"

#include "common/Errors.hpp"
#include "dal/GroupRepository.hpp"
#include "dal/RoleRepository.hpp"
#include "dal/SessionRepository.hpp"
#include "dal/UserRepository.hpp"
#include "security/CryptoService.hpp"
#include "security/IJwtSigner.hpp"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <chrono>

namespace dns::security {

FederatedAuthService::FederatedAuthService(dal::UserRepository& urRepo,
                                           dal::GroupRepository& grRepo,
                                           dal::RoleRepository& rrRepo,
                                           dal::SessionRepository& srRepo,
                                           const IJwtSigner& jsSigner,
                                           int iJwtTtlSeconds,
                                           int iSessionAbsoluteTtlSeconds)
    : _urRepo(urRepo),
      _grRepo(grRepo),
      _rrRepo(rrRepo),
      _srRepo(srRepo),
      _jsSigner(jsSigner),
      _iJwtTtlSeconds(iJwtTtlSeconds),
      _iSessionAbsoluteTtlSeconds(iSessionAbsoluteTtlSeconds) {}

FederatedAuthService::~FederatedAuthService() = default;

std::vector<int64_t> FederatedAuthService::matchGroups(
    const nlohmann::json& jGroupMappings,
    const std::vector<std::string>& vIdpGroups,
    int64_t iDefaultGroupId) {
  std::vector<int64_t> vMatched;

  if (!jGroupMappings.is_null() && jGroupMappings.contains("rules") &&
      jGroupMappings["rules"].is_array()) {
    for (const auto& jRule : jGroupMappings["rules"]) {
      std::string sPattern = jRule.value("idp_group", "");
      int64_t iGroupId = jRule.value("meridian_group_id", static_cast<int64_t>(0));

      if (sPattern.empty() || iGroupId == 0) continue;

      bool bIsWildcard = (!sPattern.empty() && sPattern.back() == '*');
      std::string sPrefix = bIsWildcard ? sPattern.substr(0, sPattern.size() - 1) : "";

      for (const auto& sIdpGroup : vIdpGroups) {
        bool bMatch = false;
        if (bIsWildcard) {
          bMatch = (sIdpGroup.size() >= sPrefix.size() &&
                    sIdpGroup.compare(0, sPrefix.size(), sPrefix) == 0);
        } else {
          bMatch = (sIdpGroup == sPattern);
        }

        if (bMatch) {
          // Deduplicate
          if (std::find(vMatched.begin(), vMatched.end(), iGroupId) == vMatched.end()) {
            vMatched.push_back(iGroupId);
          }
          break;  // This rule matched, move to next rule
        }
      }
    }
  }

  // Fall back to default group if no matches
  if (vMatched.empty() && iDefaultGroupId > 0) {
    vMatched.push_back(iDefaultGroupId);
  }

  return vMatched;
}

FederatedAuthService::LoginResult FederatedAuthService::processFederatedLogin(
    const std::string& sAuthMethod,
    const std::string& sFederatedId,
    const std::string& sUsername,
    const std::string& sEmail,
    const std::vector<std::string>& vIdpGroups,
    const nlohmann::json& jGroupMappings,
    int64_t iDefaultGroupId) {
  LoginResult lr;

  // 1. Look up user by federated ID
  std::optional<dal::UserRow> oUser;
  if (sAuthMethod == "oidc") {
    oUser = _urRepo.findByOidcSub(sFederatedId);
  } else if (sAuthMethod == "saml") {
    oUser = _urRepo.findBySamlNameId(sFederatedId);
  } else {
    throw common::ValidationError("INVALID_AUTH_METHOD",
                                  "Unknown auth method: " + sAuthMethod);
  }

  // 2. Create or update user
  if (!oUser.has_value()) {
    // Create new federated user
    std::string sOidcSub = (sAuthMethod == "oidc") ? sFederatedId : "";
    std::string sSamlNameId = (sAuthMethod == "saml") ? sFederatedId : "";

    int64_t iUserId = _urRepo.createFederated(sUsername, sEmail, sAuthMethod,
                                               sOidcSub, sSamlNameId);
    oUser = _urRepo.findById(iUserId);
    lr.bNewUser = true;
    spdlog::info("Created federated user: {} ({})", sUsername, sAuthMethod);
  } else {
    // Update email if changed
    if (!sEmail.empty() && oUser->sEmail != sEmail) {
      _urRepo.updateFederatedEmail(oUser->iId, sEmail);
    }
  }

  // 3. Check active status
  if (!oUser->bIsActive) {
    throw common::AuthenticationError("account_disabled", "User account is disabled");
  }

  lr.iUserId = oUser->iId;
  lr.sUsername = oUser->sUsername;

  // 4. Match and assign groups
  auto vGroupIds = matchGroups(jGroupMappings, vIdpGroups, iDefaultGroupId);

  for (int64_t iGroupId : vGroupIds) {
    _urRepo.addToGroup(oUser->iId, iGroupId);
  }

  // 5. Resolve role name for JWT
  std::string sRole = _rrRepo.getHighestRoleName(oUser->iId);
  if (sRole.empty()) {
    sRole = "Viewer";
  }

  // 6. Build JWT payload (same pattern as AuthService::authenticateLocal)
  auto iNow = std::chrono::duration_cast<std::chrono::seconds>(
                   std::chrono::system_clock::now().time_since_epoch())
                   .count();

  nlohmann::json jPayload = {
      {"sub", std::to_string(oUser->iId)},
      {"username", oUser->sUsername},
      {"role", sRole},
      {"auth_method", sAuthMethod},
      {"iat", iNow},
      {"exp", iNow + _iJwtTtlSeconds},
  };

  lr.sToken = _jsSigner.sign(jPayload);

  // 7. Create session
  std::string sTokenHash = CryptoService::sha256Hex(lr.sToken);
  _srRepo.create(oUser->iId, sTokenHash, _iJwtTtlSeconds, _iSessionAbsoluteTtlSeconds);

  spdlog::info("Federated login successful: {} via {}", oUser->sUsername, sAuthMethod);
  return lr;
}

}  // namespace dns::security
