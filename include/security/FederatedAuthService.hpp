#pragma once
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Meridian DNS Contributors
// This file is part of Meridian DNS. See LICENSE for details.

#include <cstdint>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace dns::dal {
class UserRepository;
class GroupRepository;
class RoleRepository;
class SessionRepository;
}  // namespace dns::dal

namespace dns::security {

class IJwtSigner;

/// Handles user provisioning and group mapping for federated auth (OIDC/SAML).
/// Class abbreviation: fas
class FederatedAuthService {
 public:
  FederatedAuthService(dal::UserRepository& urRepo,
                       dal::GroupRepository& grRepo,
                       dal::RoleRepository& rrRepo,
                       dal::SessionRepository& srRepo,
                       const IJwtSigner& jsSigner,
                       int iJwtTtlSeconds,
                       int iSessionAbsoluteTtlSeconds);
  ~FederatedAuthService();

  struct LoginResult {
    std::string sToken;
    int64_t iUserId = 0;
    std::string sUsername;
    bool bNewUser = false;
  };

  /// Process a federated login: find or create user, map groups, issue JWT.
  LoginResult processFederatedLogin(
      const std::string& sAuthMethod,
      const std::string& sFederatedId,
      const std::string& sUsername,
      const std::string& sEmail,
      const std::string& sDisplayName,
      const std::vector<std::string>& vIdpGroups,
      const nlohmann::json& jGroupMappings,
      int64_t iDefaultGroupId);

  /// Match IdP groups against mapping rules. Returns matched Meridian group IDs.
  /// Falls back to iDefaultGroupId if no rules match and default > 0.
  static std::vector<int64_t> matchGroups(
      const nlohmann::json& jGroupMappings,
      const std::vector<std::string>& vIdpGroups,
      int64_t iDefaultGroupId);

 private:
  dal::UserRepository& _urRepo;
  dal::GroupRepository& _grRepo;
  dal::RoleRepository& _rrRepo;
  dal::SessionRepository& _srRepo;
  const IJwtSigner& _jsSigner;
  int _iJwtTtlSeconds;
  int _iSessionAbsoluteTtlSeconds;
};

}  // namespace dns::security
