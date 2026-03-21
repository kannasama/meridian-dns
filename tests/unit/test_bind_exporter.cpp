// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Meridian DNS Contributors
// This file is part of Meridian DNS. See LICENSE for details.

#include "core/BindExporter.hpp"
#include "core/VariableEngine.hpp"
#include "dal/RecordRepository.hpp"
#include "dal/ZoneRepository.hpp"

#include <gtest/gtest.h>

namespace {

dns::dal::ZoneRow makeZone(int64_t iId, const std::string& sName) {
  dns::dal::ZoneRow z;
  z.iId = iId;
  z.sName = sName;
  return z;
}

dns::dal::RecordRow makeRecord(const std::string& sName, const std::string& sType,
                                int iTtl, const std::string& sValue,
                                int iPriority = 0, bool bPendingDelete = false) {
  dns::dal::RecordRow r;
  r.sName = sName;
  r.sType = sType;
  r.iTtl = iTtl;
  r.sValueTemplate = sValue;
  r.iPriority = iPriority;
  r.bPendingDelete = bPendingDelete;
  return r;
}

}  // namespace

class BindExporterTest : public ::testing::Test {
 protected:
  dns::core::VariableEngine _veEngine;
  dns::core::BindExporter _beExporter{_veEngine};
};

TEST_F(BindExporterTest, SerializeProducesHeader) {
  auto zone = makeZone(1, "example.com");
  std::string sOut = _beExporter.serialize(zone, {});

  EXPECT_NE(sOut.find("; Zone: example.com"), std::string::npos);
  EXPECT_NE(sOut.find("$ORIGIN example.com."), std::string::npos);
  EXPECT_NE(sOut.find("$TTL 300"), std::string::npos);
}

TEST_F(BindExporterTest, SerializeARecord) {
  auto zone = makeZone(1, "example.com");
  auto vRecs = {makeRecord("@", "A", 300, "1.2.3.4")};
  std::string sOut = _beExporter.serialize(zone, vRecs);

  EXPECT_NE(sOut.find("IN\tA\t1.2.3.4"), std::string::npos);
}

TEST_F(BindExporterTest, SerializeMxRecord_IncludesPriority) {
  auto zone = makeZone(1, "example.com");
  auto vRecs = {makeRecord("@", "MX", 300, "mail.example.com.", 10)};
  std::string sOut = _beExporter.serialize(zone, vRecs);

  EXPECT_NE(sOut.find("IN\tMX\t10 mail.example.com."), std::string::npos);
}

TEST_F(BindExporterTest, SerializeSkipsPendingDelete) {
  auto zone = makeZone(1, "example.com");
  auto vRecs = {makeRecord("www", "A", 300, "1.2.3.4", 0, /*bPendingDelete=*/true)};
  std::string sOut = _beExporter.serialize(zone, vRecs);

  EXPECT_EQ(sOut.find("www"), std::string::npos);
}

TEST_F(BindExporterTest, SerializeSOARecord) {
  auto zone = makeZone(1, "example.com");
  auto vRecs = {makeRecord("@", "SOA",
      300,
      "ns1.example.com. hostmaster.example.com. 2026032000 3600 900 604800 300")};
  std::string sOut = _beExporter.serialize(zone, vRecs);

  EXPECT_NE(sOut.find("IN\tSOA\t"), std::string::npos);
}
