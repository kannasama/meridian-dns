#pragma once
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Meridian DNS Contributors
// This file is part of Meridian DNS. See LICENSE for details.

#include <algorithm>
#include <array>
#include <cctype>
#include <string>
#include <string_view>

namespace dns::common {

/// True for record types whose value is a hostname, where a trailing dot is
/// DNS presentation syntax rather than content and must not affect equality.
/// Comparison is case-insensitive on the type name.
inline bool isHostnameValuedType(std::string_view sType) {
  static constexpr std::array<std::string_view, 6> kTypes = {
      "CNAME", "NS", "MX", "SRV", "PTR", "DNAME"};

  std::string sUpper;
  sUpper.reserve(sType.size());
  for (char c : sType)
    sUpper.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));

  return std::find(kTypes.begin(), kTypes.end(), sUpper) != kTypes.end();
}

/// Removes a single trailing dot. The DNS root (".") is preserved, since
/// stripping it would erase the label rather than normalise it.
inline std::string stripTrailingDot(std::string_view s) {
  if (s.size() > 1 && s.back() == '.') return std::string(s.substr(0, s.size() - 1));
  return std::string(s);
}

/// Appends a trailing dot when missing. Empty input is left alone.
inline std::string ensureTrailingDot(std::string_view s) {
  if (s.empty()) return std::string(s);
  if (s.back() == '.') return std::string(s);
  return std::string(s) + ".";
}

/// Comparison form of a record name: trailing-dot insensitive.
/// Deliberately does NOT case-fold — that is a separate concern.
inline std::string canonicalName(std::string_view sName) { return stripTrailingDot(sName); }

/// Comparison form of a record value: trailing-dot insensitive for
/// hostname-valued types only. A TXT or similar value keeps its trailing dot,
/// which is content rather than presentation syntax.
inline std::string canonicalValue(std::string_view sType, std::string_view sValue) {
  if (isHostnameValuedType(sType)) return stripTrailingDot(sValue);
  return std::string(sValue);
}

}  // namespace dns::common
