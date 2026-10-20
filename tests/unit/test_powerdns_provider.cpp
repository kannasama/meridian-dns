// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Meridian DNS Contributors
// This file is part of Meridian DNS. See LICENSE for details.

#include "providers/PowerDnsProvider.hpp"

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "common/TxtRdata.hpp"
#include "common/Types.hpp"

using dns::common::DnsRecord;
using dns::providers::PowerDnsProvider;

// --- parseZoneResponse tests ---

TEST(PowerDnsParseTest, EmptyRrsets) {
  std::string sJson = R"({"name":"example.com.","rrsets":[]})";
  auto vRecords = PowerDnsProvider::parseZoneResponse(sJson);
  EXPECT_TRUE(vRecords.empty());
}

TEST(PowerDnsParseTest, SingleARecord) {
  std::string sJson = R"({
    "name": "example.com.",
    "rrsets": [{
      "name": "www.example.com.",
      "type": "A",
      "ttl": 300,
      "records": [{"content": "192.168.1.1", "disabled": false}]
    }]
  })";
  auto vRecords = PowerDnsProvider::parseZoneResponse(sJson);
  ASSERT_EQ(vRecords.size(), 1u);
  EXPECT_EQ(vRecords[0].sName, "www.example.com.");
  EXPECT_EQ(vRecords[0].sType, "A");
  EXPECT_EQ(vRecords[0].uTtl, 300u);
  EXPECT_EQ(vRecords[0].sValue, "192.168.1.1");
  EXPECT_EQ(vRecords[0].sProviderRecordId, "www.example.com./A/192.168.1.1");
}

TEST(PowerDnsParseTest, MultipleRecordsInRrset) {
  std::string sJson = R"({
    "name": "example.com.",
    "rrsets": [{
      "name": "example.com.",
      "type": "NS",
      "ttl": 3600,
      "records": [
        {"content": "ns1.example.com.", "disabled": false},
        {"content": "ns2.example.com.", "disabled": false}
      ]
    }]
  })";
  auto vRecords = PowerDnsProvider::parseZoneResponse(sJson);
  ASSERT_EQ(vRecords.size(), 2u);
  EXPECT_EQ(vRecords[0].sValue, "ns1.example.com.");
  EXPECT_EQ(vRecords[1].sValue, "ns2.example.com.");
}

TEST(PowerDnsParseTest, DisabledRecordsSkipped) {
  std::string sJson = R"({
    "name": "example.com.",
    "rrsets": [{
      "name": "www.example.com.",
      "type": "A",
      "ttl": 300,
      "records": [
        {"content": "192.168.1.1", "disabled": false},
        {"content": "192.168.1.2", "disabled": true}
      ]
    }]
  })";
  auto vRecords = PowerDnsProvider::parseZoneResponse(sJson);
  ASSERT_EQ(vRecords.size(), 1u);
  EXPECT_EQ(vRecords[0].sValue, "192.168.1.1");
}

TEST(PowerDnsParseTest, MxRecordWithPriority) {
  std::string sJson = R"({
    "name": "example.com.",
    "rrsets": [{
      "name": "example.com.",
      "type": "MX",
      "ttl": 3600,
      "records": [{"content": "10 mail.example.com.", "disabled": false}]
    }]
  })";
  auto vRecords = PowerDnsProvider::parseZoneResponse(sJson);
  ASSERT_EQ(vRecords.size(), 1u);
  EXPECT_EQ(vRecords[0].sType, "MX");
  EXPECT_EQ(vRecords[0].iPriority, 10);
  EXPECT_EQ(vRecords[0].sValue, "mail.example.com.");
}

TEST(PowerDnsParseTest, SoaRecordIncluded) {
  std::string sJson = R"({
    "name": "example.com.",
    "rrsets": [{
      "name": "example.com.",
      "type": "SOA",
      "ttl": 3600,
      "records": [{"content": "ns1.example.com. admin.example.com. 2024010101 3600 900 604800 86400", "disabled": false}]
    }]
  })";
  auto vRecords = PowerDnsProvider::parseZoneResponse(sJson);
  ASSERT_EQ(vRecords.size(), 1u);
  EXPECT_EQ(vRecords[0].sType, "SOA");
}

TEST(PowerDnsParseTest, TxtRecordQuotesStripped) {
  std::string sJson = R"({
    "name": "example.com.",
    "rrsets": [{
      "name": "example.com.",
      "type": "TXT",
      "ttl": 3600,
      "records": [{"content": "\"v=spf1 include:_spf.google.com ~all\"", "disabled": false}]
    }]
  })";
  auto vRecords = PowerDnsProvider::parseZoneResponse(sJson);
  ASSERT_EQ(vRecords.size(), 1u);
  EXPECT_EQ(vRecords[0].sType, "TXT");
  EXPECT_EQ(vRecords[0].sValue, "v=spf1 include:_spf.google.com ~all");
}

TEST(PowerDnsParseTest, TxtRecordWithoutQuotesUnchanged) {
  std::string sJson = R"({
    "name": "example.com.",
    "rrsets": [{
      "name": "example.com.",
      "type": "TXT",
      "ttl": 3600,
      "records": [{"content": "v=spf1 ~all", "disabled": false}]
    }]
  })";
  auto vRecords = PowerDnsProvider::parseZoneResponse(sJson);
  ASSERT_EQ(vRecords.size(), 1u);
  EXPECT_EQ(vRecords[0].sValue, "v=spf1 ~all");
}

// --- makeRecordId / parseRecordId tests ---

TEST(PowerDnsRecordIdTest, MakeAndParse) {
  std::string sId = PowerDnsProvider::makeRecordId("www.example.com.", "A", "1.2.3.4");
  EXPECT_EQ(sId, "www.example.com./A/1.2.3.4");

  std::string sName, sType, sValue;
  ASSERT_TRUE(PowerDnsProvider::parseRecordId(sId, sName, sType, sValue));
  EXPECT_EQ(sName, "www.example.com.");
  EXPECT_EQ(sType, "A");
  EXPECT_EQ(sValue, "1.2.3.4");
}

TEST(PowerDnsRecordIdTest, InvalidFormat) {
  std::string sName, sType, sValue;
  EXPECT_FALSE(PowerDnsProvider::parseRecordId("invalid", sName, sType, sValue));
  EXPECT_FALSE(PowerDnsProvider::parseRecordId("only/one", sName, sType, sValue));
}

// ---------------------------------------------------------------------------
// TXT quoting (v1.1.5)
//
// The read path stripped quotes, establishing "unquoted" as the canonical
// internal form, but the write path sent values verbatim and never added them.
// Unquoted TXT was rejected by PowerDNS; storing the quotes to work around that
// then collided with the read-side stripping and produced permanent drift.
// ---------------------------------------------------------------------------

// A long TXT value arrives as several character-strings. The old naive strip
// mangled this into: part1" "part2
TEST(PowerDnsTxtTest, MultipleCharacterStringsAreJoined) {
  std::string sJson = R"({
    "name": "example.com.",
    "rrsets": [{
      "name": "dkim.example.com.",
      "type": "TXT",
      "ttl": 3600,
      "records": [{"content": "\"part1\" \"part2\"", "disabled": false}]
    }]
  })";
  auto vRecords = PowerDnsProvider::parseZoneResponse(sJson);
  ASSERT_EQ(vRecords.size(), 1u);
  EXPECT_EQ(vRecords[0].sValue, "part1part2");
}

TEST(PowerDnsTxtTest, EscapedQuotesAreUnescaped) {
  std::string sJson = R"({
    "name": "example.com.",
    "rrsets": [{
      "name": "example.com.",
      "type": "TXT",
      "ttl": 3600,
      "records": [{"content": "\"say \\\"hi\\\"\"", "disabled": false}]
    }]
  })";
  auto vRecords = PowerDnsProvider::parseZoneResponse(sJson);
  ASSERT_EQ(vRecords.size(), 1u);
  EXPECT_EQ(vRecords[0].sValue, "say \"hi\"");
}

// prepareDesiredRecords is the hook DiffEngine calls before diffing, and the
// pushed value is taken from the resulting diff — so normalising here fixes the
// comparison and the wire value together.
TEST(PowerDnsTxtTest, PrepareNormalizesStoredQuotedTxtValue) {
  PowerDnsProvider provider("http://localhost:8081", "test-key");

  DnsRecord dr;
  dr.sName = "example.com.";
  dr.sType = "TXT";
  dr.sValue = "\"v=spf1 -all\"";  // stored with quotes, as the workaround did

  auto vResult = provider.prepareDesiredRecords({dr});
  ASSERT_EQ(vResult.size(), 1u);
  EXPECT_EQ(vResult[0].sValue, "v=spf1 -all");
}

TEST(PowerDnsTxtTest, PrepareLeavesUnquotedTxtValueAlone) {
  PowerDnsProvider provider("http://localhost:8081", "test-key");

  DnsRecord dr;
  dr.sName = "example.com.";
  dr.sType = "TXT";
  dr.sValue = "v=spf1 -all";

  auto vResult = provider.prepareDesiredRecords({dr});
  ASSERT_EQ(vResult.size(), 1u);
  EXPECT_EQ(vResult[0].sValue, "v=spf1 -all");
}

TEST(PowerDnsTxtTest, PrepareLeavesNonTxtRecordsAlone) {
  PowerDnsProvider provider("http://localhost:8081", "test-key");

  DnsRecord dr;
  dr.sName = "www.example.com.";
  dr.sType = "A";
  dr.sValue = "192.168.1.1";

  auto vResult = provider.prepareDesiredRecords({dr});
  ASSERT_EQ(vResult.size(), 1u);
  EXPECT_EQ(vResult[0].sValue, "192.168.1.1");
}

// The reported symptom end to end: a value stored WITH quotes must compare
// equal to what PowerDNS returns for that same record.
TEST(PowerDnsTxtTest, StoredQuotedValueMatchesLiveResponse) {
  PowerDnsProvider provider("http://localhost:8081", "test-key");

  DnsRecord stored;
  stored.sName = "example.com.";
  stored.sType = "TXT";
  stored.sValue = "\"v=spf1 include:_spf.google.com ~all\"";

  auto vDesired = provider.prepareDesiredRecords({stored});

  std::string sJson = R"({
    "name": "example.com.",
    "rrsets": [{
      "name": "example.com.",
      "type": "TXT",
      "ttl": 3600,
      "records": [{"content": "\"v=spf1 include:_spf.google.com ~all\"", "disabled": false}]
    }]
  })";
  auto vLive = PowerDnsProvider::parseZoneResponse(sJson);

  ASSERT_EQ(vDesired.size(), 1u);
  ASSERT_EQ(vLive.size(), 1u);
  EXPECT_EQ(vDesired[0].sValue, vLive[0].sValue);
}

// Equally, a value stored WITHOUT quotes must match — this is the form the
// migration leaves behind and what new records should use.
TEST(PowerDnsTxtTest, StoredUnquotedValueMatchesLiveResponse) {
  PowerDnsProvider provider("http://localhost:8081", "test-key");

  DnsRecord stored;
  stored.sName = "example.com.";
  stored.sType = "TXT";
  stored.sValue = "v=spf1 include:_spf.google.com ~all";

  auto vDesired = provider.prepareDesiredRecords({stored});

  std::string sJson = R"({
    "name": "example.com.",
    "rrsets": [{
      "name": "example.com.",
      "type": "TXT",
      "ttl": 3600,
      "records": [{"content": "\"v=spf1 include:_spf.google.com ~all\"", "disabled": false}]
    }]
  })";
  auto vLive = PowerDnsProvider::parseZoneResponse(sJson);

  ASSERT_EQ(vDesired.size(), 1u);
  ASSERT_EQ(vLive.size(), 1u);
  EXPECT_EQ(vDesired[0].sValue, vLive[0].sValue);
}

// The original deployment failure: unquoted TXT reaching the PowerDNS API.
// buildTxtContent is what the create/update paths send.
TEST(PowerDnsTxtTest, WireContentIsAlwaysQuoted) {
  EXPECT_EQ(PowerDnsProvider::buildTxtContent("v=spf1 -all"), "\"v=spf1 -all\"");
  // Already-quoted input must not be double-quoted.
  EXPECT_EQ(PowerDnsProvider::buildTxtContent("\"v=spf1 -all\""), "\"v=spf1 -all\"");
}

TEST(PowerDnsTxtTest, WireContentChunksLongValues) {
  std::string sLong(300, 'a');
  auto sContent = PowerDnsProvider::buildTxtContent(sLong);
  EXPECT_NE(sContent.find("\" \""), std::string::npos);
  // And PowerDNS's echo of it parses back to the original value.
  EXPECT_EQ(dns::common::parseTxtPresentation(sContent), sLong);
}
