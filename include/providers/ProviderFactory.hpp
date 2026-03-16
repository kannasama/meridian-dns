#pragma once
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Meridian DNS Contributors
// This file is part of Meridian DNS. See LICENSE for details.

#include <memory>
#include <string>

#include <nlohmann/json.hpp>

#include "providers/IProvider.hpp"

namespace dns::providers {

/// Creates concrete IProvider instances by type string.
class ProviderFactory {
 public:
  static std::unique_ptr<IProvider> create(
      const std::string& sType, const std::string& sApiEndpoint,
      const std::string& sDecryptedToken,
      const nlohmann::json& jConfig = nlohmann::json::object());
};

}  // namespace dns::providers
