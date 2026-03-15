#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

namespace {

/// Validates the expected shape of a capture snapshot JSON.
bool isValidCaptureSnapshot(const nlohmann::json& j) {
  if (!j.contains("zone") || !j["zone"].is_string()) return false;
  if (!j.contains("view") || !j["view"].is_string()) return false;
  if (!j.contains("captured_at") || !j["captured_at"].is_string()) return false;
  if (!j.contains("generated_by") || !j["generated_by"].is_string()) return false;
  if (!j.contains("records") || !j["records"].is_array()) return false;

  for (const auto& r : j["records"]) {
    if (!r.contains("name") || !r["name"].is_string()) return false;
    if (!r.contains("type") || !r["type"].is_string()) return false;
    if (!r.contains("value") || !r["value"].is_string()) return false;
    if (!r.contains("ttl") || !r["ttl"].is_number()) return false;
  }
  return true;
}

TEST(CaptureSnapshotFormat, ValidSnapshotHasAllRequiredFields) {
  nlohmann::json j = {
      {"zone", "example.com"},
      {"view", "production"},
      {"captured_at", "2026-03-15T12:00:00Z"},
      {"generated_by", "auto-capture"},
      {"record_count", 3},
      {"records",
       {{{"name", "www"}, {"type", "A"}, {"value", "192.0.2.1"}, {"ttl", 300}, {"priority", 0}},
        {{"name", "mail"}, {"type", "MX"}, {"value", "mx.example.com."}, {"ttl", 3600}, {"priority", 10}},
        {{"name", "@"}, {"type", "TXT"}, {"value", "v=spf1 -all"}, {"ttl", 300}, {"priority", 0}}}},
  };
  EXPECT_TRUE(isValidCaptureSnapshot(j));
}

TEST(CaptureSnapshotFormat, MissingZoneIsInvalid) {
  nlohmann::json j = {
      {"view", "production"},
      {"captured_at", "2026-03-15T12:00:00Z"},
      {"generated_by", "auto-capture"},
      {"records", nlohmann::json::array()},
  };
  EXPECT_FALSE(isValidCaptureSnapshot(j));
}

TEST(CaptureSnapshotFormat, MissingGeneratedByIsInvalid) {
  nlohmann::json j = {
      {"zone", "example.com"},
      {"view", "production"},
      {"captured_at", "2026-03-15T12:00:00Z"},
      {"records", nlohmann::json::array()},
  };
  EXPECT_FALSE(isValidCaptureSnapshot(j));
}

TEST(CaptureSnapshotFormat, RecordMissingValueIsInvalid) {
  nlohmann::json j = {
      {"zone", "example.com"},
      {"view", "production"},
      {"captured_at", "2026-03-15T12:00:00Z"},
      {"generated_by", "manual-capture"},
      {"records", {{{"name", "www"}, {"type", "A"}, {"ttl", 300}}}},
  };
  EXPECT_FALSE(isValidCaptureSnapshot(j));
}

TEST(CaptureSnapshotFormat, AutoCaptureVsManualCapture) {
  nlohmann::json jAuto = {
      {"zone", "example.com"},
      {"view", "production"},
      {"captured_at", "2026-03-15T12:00:00Z"},
      {"generated_by", "auto-capture"},
      {"records", nlohmann::json::array()},
  };
  nlohmann::json jManual = jAuto;
  jManual["generated_by"] = "manual-capture";

  EXPECT_TRUE(isValidCaptureSnapshot(jAuto));
  EXPECT_TRUE(isValidCaptureSnapshot(jManual));
  EXPECT_NE(jAuto["generated_by"], jManual["generated_by"]);
}

TEST(CaptureSnapshotFormat, EmptyRecordsIsValid) {
  nlohmann::json j = {
      {"zone", "empty.example.com"},
      {"view", "staging"},
      {"captured_at", "2026-03-15T12:00:00Z"},
      {"generated_by", "auto-capture"},
      {"records", nlohmann::json::array()},
  };
  EXPECT_TRUE(isValidCaptureSnapshot(j));
}

}  // namespace
