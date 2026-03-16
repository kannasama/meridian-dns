#pragma once
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Meridian DNS Contributors
// This file is part of Meridian DNS. See LICENSE for details.

#include <crow.h>

namespace dns::dal {
class IdpRepository;
class SessionRepository;
}  // namespace dns::dal
namespace dns::security {
class SamlService;
class FederatedAuthService;
}  // namespace dns::security

namespace dns::api::routes {

class SamlRoutes {
 public:
  SamlRoutes(dns::dal::IdpRepository& irRepo,
             dns::dal::SessionRepository& srRepo,
             dns::security::SamlService& ssService,
             dns::security::FederatedAuthService& fasService);
  ~SamlRoutes();
  void registerRoutes(crow::SimpleApp& app);

 private:
  /// Helper: ensure an IdP is registered with the SamlService (lazy registration).
  void ensureIdpRegistered(int iIdpId);

  dns::dal::IdpRepository& _irRepo;
  dns::dal::SessionRepository& _srRepo;
  dns::security::SamlService& _ssService;
  dns::security::FederatedAuthService& _fasService;
};

}  // namespace dns::api::routes
