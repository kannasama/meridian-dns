// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Meridian DNS Contributors
// This file is part of Meridian DNS. See LICENSE for details.

#include "common/TxtRdata.hpp"

#include <gtest/gtest.h>
#include <string>

using dns::common::normalizeTxtValue;
using dns::common::parseTxtPresentation;
using dns::common::toTxtPresentation;

// ---------------------------------------------------------------------------
// parseTxtPresentation
// ---------------------------------------------------------------------------

TEST(TxtRdataParseTest, StripsSurroundingQuotes) {
  EXPECT_EQ(parseTxtPresentation("\"v=spf1 -all\""), "v=spf1 -all");
}

// Long TXT values arrive as multiple character-strings. The old naive strip
// turned "a" "b" into a" "b — this is the mangling being fixed.
TEST(TxtRdataParseTest, JoinsMultipleCharacterStrings) {
  EXPECT_EQ(parseTxtPresentation("\"part1\" \"part2\""), "part1part2");
  EXPECT_EQ(parseTxtPresentation("\"a\" \"b\" \"c\""), "abc");
}

TEST(TxtRdataParseTest, UnescapesQuotesAndBackslashes) {
  EXPECT_EQ(parseTxtPresentation("\"say \\\"hi\\\"\""), "say \"hi\"");
  EXPECT_EQ(parseTxtPresentation("\"back\\\\slash\""), "back\\slash");
}

TEST(TxtRdataParseTest, UnescapesDecimalEscapes) {
  // \032 is a space in RFC 1035 presentation format.
  EXPECT_EQ(parseTxtPresentation("\"a\\032b\""), "a b");
}

// Providers that return raw unquoted content must pass through untouched.
TEST(TxtRdataParseTest, UnquotedInputPassesThrough) {
  EXPECT_EQ(parseTxtPresentation("v=spf1 -all"), "v=spf1 -all");
}

TEST(TxtRdataParseTest, UnbalancedQuoteIsLeftAlone) {
  EXPECT_EQ(parseTxtPresentation("\"unterminated"), "\"unterminated");
}

TEST(TxtRdataParseTest, EmptyInputs) {
  EXPECT_EQ(parseTxtPresentation(""), "");
  EXPECT_EQ(parseTxtPresentation("\"\""), "");
}

// ---------------------------------------------------------------------------
// toTxtPresentation
// ---------------------------------------------------------------------------

TEST(TxtRdataRenderTest, WrapsShortValueInQuotes) {
  EXPECT_EQ(toTxtPresentation("v=spf1 -all"), "\"v=spf1 -all\"");
}

TEST(TxtRdataRenderTest, EscapesQuotesAndBackslashes) {
  EXPECT_EQ(toTxtPresentation("say \"hi\""), "\"say \\\"hi\\\"\"");
  EXPECT_EQ(toTxtPresentation("back\\slash"), "\"back\\\\slash\"");
}

TEST(TxtRdataRenderTest, EmptyValueRendersAsEmptyString) {
  EXPECT_EQ(toTxtPresentation(""), "\"\"");
}

// A DNS character-string caps at 255 bytes, so anything longer must be split
// into several quoted strings. DKIM keys routinely exceed this.
TEST(TxtRdataRenderTest, ChunksAt255ByteBoundary) {
  std::string sExactly255(255, 'a');
  EXPECT_EQ(toTxtPresentation(sExactly255), "\"" + sExactly255 + "\"");

  std::string s256(256, 'a');
  auto sRendered = toTxtPresentation(s256);
  EXPECT_EQ(sRendered, "\"" + std::string(255, 'a') + "\" \"a\"");
}

TEST(TxtRdataRenderTest, ChunkLimitAppliesToUnescapedBytes) {
  // 255 quote characters: each escapes to 2 bytes, but the 255-byte limit is a
  // wire-format limit on the unescaped string, so this stays a single chunk.
  std::string sQuotes(255, '"');
  auto sRendered = toTxtPresentation(sQuotes);
  // One opening and one closing quote, plus 255 escaped quotes (2 bytes each).
  EXPECT_EQ(sRendered.size(), 2u + 255u * 2u);
  EXPECT_EQ(parseTxtPresentation(sRendered), sQuotes);
}

// ---------------------------------------------------------------------------
// Round-trip — the invariant that keeps TXT out of the drift report
// ---------------------------------------------------------------------------

TEST(TxtRdataRoundTripTest, SurvivesRoundTrip) {
  const std::string kValues[] = {
      "v=spf1 include:_spf.google.com ~all",
      "simple",
      "",
      "with \"quotes\" inside",
      "with\\backslash",
      "trailing dot.",
      "spaces   preserved",
      std::string(300, 'x'),
      std::string(255, 'y'),
      std::string(256, 'z'),
  };

  for (const auto& sValue : kValues) {
    EXPECT_EQ(parseTxtPresentation(toTxtPresentation(sValue)), sValue)
        << "round-trip failed for a value of length " << sValue.size();
  }
}

TEST(TxtRdataRoundTripTest, LongDkimStyleKeyRoundTrips) {
  std::string sKey = "v=DKIM1; k=rsa; p=";
  sKey += std::string(400, 'A');

  auto sRendered = toTxtPresentation(sKey);
  EXPECT_EQ(parseTxtPresentation(sRendered), sKey);
  // Must have been split into more than one character-string.
  EXPECT_NE(sRendered.find("\" \""), std::string::npos);
}

// ---------------------------------------------------------------------------
// normalizeTxtValue — tolerance for values already stored with quotes
// ---------------------------------------------------------------------------

TEST(TxtRdataNormalizeTest, StripsQuotesFromStoredPresentationValue) {
  EXPECT_EQ(normalizeTxtValue("\"v=spf1 -all\""), "v=spf1 -all");
}

TEST(TxtRdataNormalizeTest, LeavesRawValueUntouched) {
  EXPECT_EQ(normalizeTxtValue("v=spf1 -all"), "v=spf1 -all");
}

TEST(TxtRdataNormalizeTest, IsIdempotent) {
  auto sOnce = normalizeTxtValue("\"v=spf1 -all\"");
  EXPECT_EQ(normalizeTxtValue(sOnce), sOnce);
}

TEST(TxtRdataNormalizeTest, JoinsStoredMultiChunkValue) {
  EXPECT_EQ(normalizeTxtValue("\"part1\" \"part2\""), "part1part2");
}

TEST(TxtRdataNormalizeTest, EmptyValue) {
  EXPECT_EQ(normalizeTxtValue(""), "");
}
