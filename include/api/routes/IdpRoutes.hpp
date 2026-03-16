#pragma once
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Meridian DNS Contributors
// This file is part of Meridian DNS. See LICENSE for details.

#include <crow.h>

namespace dns::api {
class AuthMiddleware;
}
namespace dns::dal {
class IdpRepository;
}
namespace dns::security {
class OidcService;
class SamlService;
}  // namespace dns::security

namespace dns::api::routes {

class IdpRoutes {
 public:
  IdpRoutes(dns::dal::IdpRepository& irRepo, const dns::api::AuthMiddleware& amMiddleware,
            dns::security::OidcService& osService, dns::security::SamlService& ssService);
  ~IdpRoutes();
  void registerRoutes(crow::SimpleApp& app);

 private:
  dns::dal::IdpRepository& _irRepo;
  const dns::api::AuthMiddleware& _amMiddleware;
  dns::security::OidcService& _osService;
  dns::security::SamlService& _ssService;
};

}  // namespace dns::api::routes
