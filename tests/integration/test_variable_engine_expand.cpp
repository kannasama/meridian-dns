#include "core/VariableEngine.hpp"

#include <gtest/gtest.h>

#include <cstdlib>
#include <memory>
#include <string>

#include "common/Errors.hpp"
#include "common/Logger.hpp"
#include "dal/ConnectionPool.hpp"
#include "dal/VariableRepository.hpp"
#include "dal/ViewRepository.hpp"
#include "dal/ZoneRepository.hpp"

using dns::core::VariableEngine;
using dns::dal::ConnectionPool;
using dns::dal::VariableRepository;
using dns::dal::ViewRepository;
using dns::dal::ZoneRepository;

namespace {
std::string getDbUrl() {
  const char* pUrl = std::getenv("DNS_DB_URL");
  return pUrl ? std::string(pUrl) : std::string{};
}
}  // namespace

class VariableEngineExpandTest : public ::testing::Test {
 protected:
  void SetUp() override {
    _sDbUrl = getDbUrl();
    if (_sDbUrl.empty()) {
      GTEST_SKIP() << "DNS_DB_URL not set — skipping integration test";
    }
    dns::common::Logger::init("warn");
    _cpPool = std::make_unique<ConnectionPool>(_sDbUrl, 2);
    _varRepo = std::make_unique<VariableRepository>(*_cpPool);
    _vrRepo = std::make_unique<ViewRepository>(*_cpPool);
    _zrRepo = std::make_unique<ZoneRepository>(*_cpPool);
    _ve = std::make_unique<VariableEngine>(*_varRepo);

    // Clean test data (order matters for FK constraints)
    auto cg = _cpPool->checkout();
    pqxx::work txn(*cg);
    txn.exec("DELETE FROM variables");
    txn.exec("DELETE FROM records");
    txn.exec("DELETE FROM zones");
    txn.exec("DELETE FROM view_providers");
    txn.exec("DELETE FROM views");
    txn.commit();

    // Create a test view and zone
    _iViewId = _vrRepo->create("test-view", "Test view for variable engine");
    _iZoneId = _zrRepo->create("example.com", _iViewId, std::nullopt);
  }

  std::string _sDbUrl;
  std::unique_ptr<ConnectionPool> _cpPool;
  std::unique_ptr<VariableRepository> _varRepo;
  std::unique_ptr<ViewRepository> _vrRepo;
  std::unique_ptr<ZoneRepository> _zrRepo;
  std::unique_ptr<VariableEngine> _ve;
  int64_t _iViewId = 0;
  int64_t _iZoneId = 0;
};

TEST_F(VariableEngineExpandTest, NoPlaceholders) {
  std::string sResult = _ve->expand("192.168.1.1", _iZoneId);
  EXPECT_EQ(sResult, "192.168.1.1");
}

TEST_F(VariableEngineExpandTest, ExpandGlobalVariable) {
  _varRepo->create("server_ip", "10.0.0.1", "ipv4", "global", std::nullopt);
  std::string sResult = _ve->expand("{{server_ip}}", _iZoneId);
  EXPECT_EQ(sResult, "10.0.0.1");
}

TEST_F(VariableEngineExpandTest, ExpandZoneScopedVariable) {
  _varRepo->create("octet", "42", "string", "zone", _iZoneId);
  std::string sResult = _ve->expand("192.168.1.{{octet}}", _iZoneId);
  EXPECT_EQ(sResult, "192.168.1.42");
}

TEST_F(VariableEngineExpandTest, ZoneScopedTakesPrecedenceOverGlobal) {
  _varRepo->create("ip", "10.0.0.1", "ipv4", "global", std::nullopt);
  _varRepo->create("ip", "10.0.0.99", "ipv4", "zone", _iZoneId);
  std::string sResult = _ve->expand("{{ip}}", _iZoneId);
  EXPECT_EQ(sResult, "10.0.0.99");
}

TEST_F(VariableEngineExpandTest, MultipleVariables) {
  _varRepo->create("prefix", "10.0", "string", "global", std::nullopt);
  _varRepo->create("suffix", "1.42", "string", "zone", _iZoneId);
  std::string sResult = _ve->expand("{{prefix}}.{{suffix}}", _iZoneId);
  EXPECT_EQ(sResult, "10.0.1.42");
}

TEST_F(VariableEngineExpandTest, UnresolvedVariableThrows) {
  EXPECT_THROW(_ve->expand("{{nonexistent}}", _iZoneId),
               dns::common::UnresolvedVariableError);
}

TEST_F(VariableEngineExpandTest, ValidateReturnsTrueWhenAllResolved) {
  _varRepo->create("host", "web01", "string", "global", std::nullopt);
  EXPECT_TRUE(_ve->validate("{{host}}.example.com", _iZoneId));
}

TEST_F(VariableEngineExpandTest, ValidateReturnsFalseWhenUnresolved) {
  EXPECT_FALSE(_ve->validate("{{missing}}.example.com", _iZoneId));
}

TEST_F(VariableEngineExpandTest, EmptyTemplateExpandsToEmpty) {
  std::string sResult = _ve->expand("", _iZoneId);
  EXPECT_EQ(sResult, "");
}
