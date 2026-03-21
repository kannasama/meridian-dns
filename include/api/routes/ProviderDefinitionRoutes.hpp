#pragma once
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Meridian DNS Contributors
// This file is part of Meridian DNS. See LICENSE for details.

#include <crow.h>

namespace dns::dal {
class ProviderDefinitionRepository;
}
namespace dns::api {
class AuthMiddleware;
}

namespace dns::api::routes {

/// Handlers for /api/v1/provider-definitions
/// Class abbreviation: pdr
class ProviderDefinitionRoutes {
 public:
  ProviderDefinitionRoutes(dns::dal::ProviderDefinitionRepository& pdrRepo,
                            const dns::api::AuthMiddleware& amMiddleware);
  ~ProviderDefinitionRoutes();

  void registerRoutes(crow::SimpleApp& app);

 private:
  dns::dal::ProviderDefinitionRepository& _pdrRepo;
  const dns::api::AuthMiddleware& _amMiddleware;
};

}  // namespace dns::api::routes
