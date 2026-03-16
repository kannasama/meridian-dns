#pragma once
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Meridian DNS Contributors
// This file is part of Meridian DNS. See LICENSE for details.

#include <string>

#include "security/IJwtSigner.hpp"

namespace dns::security {

/// HS256 JWT signing implementation using OpenSSL HMAC.
class HmacJwtSigner : public IJwtSigner {
 public:
  explicit HmacJwtSigner(const std::string& sSecret);
  ~HmacJwtSigner() override;

  std::string sign(const nlohmann::json& jPayload) const override;
  nlohmann::json verify(const std::string& sToken) const override;

 private:
  std::string _sSecret;
};

}  // namespace dns::security
