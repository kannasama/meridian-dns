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

/// True when sName is the apex of sZoneName or a subdomain of it.
///
/// Needed because some providers expose a single flat namespace with no zone
/// concept (AdGuard Home's rewrite list), so the provider must decide zone
/// membership itself rather than relying on the API to scope the response.
///
/// Unlike canonicalName, this DOES fold case: zone membership is a containment
/// test over case-insensitive DNS labels, not a value comparison.
///
/// A leading "*." wildcard label is ignored for the purpose of the test, so
/// "*.example.com" belongs to zone "example.com". A bare "*" catch-all belongs
/// to no zone and returns false.
inline bool isWithinZone(std::string_view sName, std::string_view sZoneName) {
  auto toLowerNoDot = [](std::string_view s) {
    std::string sOut = stripTrailingDot(s);
    for (char& c : sOut) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return sOut;
  };

  std::string sZone = toLowerNoDot(sZoneName);
  std::string sCandidate = toLowerNoDot(sName);
  if (sZone.empty() || sCandidate.empty()) return false;

  // Strip a leading wildcard label; "*.example.com" is scoped to example.com.
  if (sCandidate.rfind("*.", 0) == 0) {
    sCandidate = sCandidate.substr(2);
    if (sCandidate.empty()) return false;
  } else if (sCandidate == "*") {
    return false;  // bare catch-all — belongs to no zone
  }

  if (sCandidate == sZone) return true;

  // Suffix match must land on a label boundary, so "notexample.com" is not
  // treated as inside "example.com".
  if (sCandidate.size() > sZone.size() + 1) {
    auto nOffset = sCandidate.size() - sZone.size();
    if (sCandidate[nOffset - 1] == '.' &&
        sCandidate.compare(nOffset, sZone.size(), sZone) == 0) {
      return true;
    }
  }

  return false;
}

/// Comparison form of a record value: trailing-dot insensitive for
/// hostname-valued types only. A TXT or similar value keeps its trailing dot,
/// which is content rather than presentation syntax.
inline std::string canonicalValue(std::string_view sType, std::string_view sValue) {
  if (isHostnameValuedType(sType)) return stripTrailingDot(sValue);
  return std::string(sValue);
}

}  // namespace dns::common
