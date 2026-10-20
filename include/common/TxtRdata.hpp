#pragma once
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Meridian DNS Contributors
// This file is part of Meridian DNS. See LICENSE for details.

#include <cctype>
#include <string>
#include <string_view>

namespace dns::common {

/// TXT record presentation-format helpers (RFC 1035 §5.1, §3.3.14).
///
/// Meridian's canonical internal form for a TXT value is the raw, unquoted
/// text the operator actually means (e.g. `v=spf1 include:... ~all`). That form
/// is what every provider API except PowerDNS accepts directly, and it is what
/// the database stores.
///
/// PowerDNS speaks zone-file presentation format instead: one or more quoted
/// character-strings, each at most 255 bytes, space separated. These helpers
/// convert between the two so the wire format stays confined to the provider.

/// Maximum length of a single DNS character-string, in unescaped bytes.
inline constexpr std::size_t kTxtChunkSize = 255;

/// Parse presentation format into the raw concatenated value.
///
/// Joins multiple character-strings (`"a" "b"` -> `ab`), since a long TXT value
/// is split across them on the wire and resolvers concatenate on receipt.
/// Unescapes `\\`, `\"` and `\DDD` decimal escapes.
///
/// Tolerant by design: input with no complete quoted string is returned
/// unchanged, so a provider that hands back raw unquoted content still works.
inline std::string parseTxtPresentation(std::string_view sPresentation) {
  std::string sOut;
  bool bFoundString = false;

  for (std::size_t i = 0; i < sPresentation.size(); ++i) {
    if (sPresentation[i] != '"') continue;

    // Walk one quoted character-string.
    std::string sChunk;
    std::size_t j = i + 1;
    bool bClosed = false;
    for (; j < sPresentation.size(); ++j) {
      char c = sPresentation[j];
      if (c == '"') {
        bClosed = true;
        break;
      }
      if (c == '\\' && j + 1 < sPresentation.size()) {
        // \DDD decimal escape, else a literal escape of the next character.
        if (j + 3 < sPresentation.size() &&
            std::isdigit(static_cast<unsigned char>(sPresentation[j + 1])) &&
            std::isdigit(static_cast<unsigned char>(sPresentation[j + 2])) &&
            std::isdigit(static_cast<unsigned char>(sPresentation[j + 3]))) {
          int iVal = (sPresentation[j + 1] - '0') * 100 +
                     (sPresentation[j + 2] - '0') * 10 +
                     (sPresentation[j + 3] - '0');
          sChunk.push_back(static_cast<char>(iVal));
          j += 3;
          continue;
        }
        sChunk.push_back(sPresentation[j + 1]);
        ++j;
        continue;
      }
      sChunk.push_back(c);
    }

    if (!bClosed) break;  // unterminated — not valid presentation format
    sOut += sChunk;
    bFoundString = true;
    i = j;
  }

  if (!bFoundString) return std::string(sPresentation);
  return sOut;
}

/// Render a raw value as presentation format: escaped, quoted, and split into
/// character-strings of at most kTxtChunkSize unescaped bytes.
inline std::string toTxtPresentation(std::string_view sRaw) {
  auto escapeChunk = [](std::string_view sChunk) {
    std::string sOut;
    sOut.reserve(sChunk.size() + 2);
    sOut.push_back('"');
    for (char c : sChunk) {
      if (c == '"' || c == '\\') sOut.push_back('\\');
      sOut.push_back(c);
    }
    sOut.push_back('"');
    return sOut;
  };

  if (sRaw.empty()) return "\"\"";

  // Chunk the raw bytes first: the 255-byte cap is a wire-format limit on the
  // unescaped character-string, not on its escaped presentation.
  std::string sOut;
  for (std::size_t nOffset = 0; nOffset < sRaw.size(); nOffset += kTxtChunkSize) {
    if (!sOut.empty()) sOut.push_back(' ');
    sOut += escapeChunk(sRaw.substr(nOffset, kTxtChunkSize));
  }
  return sOut;
}

/// Coerce a stored TXT value to the canonical raw form.
///
/// Operators often paste values already in presentation format, copied from a
/// DNS provider's documentation. Storing those verbatim used to produce
/// permanent drift: the write path sent them through unchanged while the read
/// path stripped the quotes off whatever came back. Normalising here makes the
/// pipeline idempotent and accepts either form.
inline std::string normalizeTxtValue(std::string_view sStored) {
  if (sStored.size() >= 2 && sStored.front() == '"') {
    return parseTxtPresentation(sStored);
  }
  return std::string(sStored);
}

}  // namespace dns::common
