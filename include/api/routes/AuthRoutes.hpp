#pragma once
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Meridian DNS Contributors
// This file is part of Meridian DNS. See LICENSE for details.

#include <crow.h>

namespace dns::security {
class AuthService;
}

namespace dns::dal {
class UserRepository;
}

namespace dns::api {
class AuthMiddleware;
}

namespace dns::api::routes {

/// Handlers for /api/v1/auth
/// Class abbreviation: ar
class AuthRoutes {
 public:
  AuthRoutes(dns::security::AuthService& asService,
             const dns::api::AuthMiddleware& amMiddleware,
             dns::dal::UserRepository& urRepo);
  ~AuthRoutes();

  /// Register auth routes on the Crow app.
  void registerRoutes(crow::SimpleApp& app);

 private:
  dns::security::AuthService& _asService;
  const dns::api::AuthMiddleware& _amMiddleware;
  dns::dal::UserRepository& _urRepo;
};

}  // namespace dns::api::routes
