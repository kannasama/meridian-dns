#pragma once
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Meridian DNS Contributors
// This file is part of Meridian DNS. See LICENSE for details.

#include <atomic>
#include <string>

#include <crow.h>

namespace dns::dal {
class ConnectionPool;
class UserRepository;
}

namespace dns::security {
class IJwtSigner;
}

namespace dns::api::routes {

/// Handles initial setup wizard endpoints.
/// Class abbreviation: sr (setup routes)
class SetupRoutes {
 public:
  SetupRoutes(dns::dal::ConnectionPool& cpPool,
              dns::dal::UserRepository& urRepo,
              const dns::security::IJwtSigner& jsSigner);
  ~SetupRoutes();

  /// Register setup routes on the Crow app.
  void registerRoutes(crow::SimpleApp& app);

  /// Set the setup JWT token (called by main on startup when setup is needed).
  void setSetupToken(const std::string& sToken);

  /// Check if setup has been completed.
  bool isSetupCompleted() const;

  /// Check system_config table for setup_completed flag and cache the result.
  void loadSetupState();

 private:
  dns::dal::ConnectionPool& _cpPool;
  dns::dal::UserRepository& _urRepo;
  const dns::security::IJwtSigner& _jsSigner;
  std::atomic<bool> _bSetupCompleted{false};
  std::string _sSetupToken;
};

}  // namespace dns::api::routes
