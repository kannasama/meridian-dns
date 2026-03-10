#include "common/Config.hpp"
#include "common/SettingsDef.hpp"
#include "dal/ConnectionPool.hpp"
#include "dal/SettingsRepository.hpp"
#include "common/Logger.hpp"

#include <gtest/gtest.h>

#include <cstdlib>
#include <memory>
#include <string>

namespace {

std::string getDbUrl() {
  const char* p = std::getenv("DNS_DB_URL");
  return p ? std::string(p) : "";
}

}  // namespace

class ConfigSeedingTest : public ::testing::Test {
 protected:
  void SetUp() override {
    _sDbUrl = getDbUrl();
    if (_sDbUrl.empty()) {
      GTEST_SKIP() << "DNS_DB_URL not set — skipping DB integration test";
    }
    dns::common::Logger::init("warn");
    _cpPool = std::make_unique<dns::dal::ConnectionPool>(_sDbUrl, 2);
    _srRepo = std::make_unique<dns::dal::SettingsRepository>(*_cpPool);

    // Clear all settings except setup_completed
    auto cg = _cpPool->checkout();
    pqxx::work txn(*cg);
    txn.exec("DELETE FROM system_config WHERE key != 'setup_completed'");
    txn.commit();
  }

  std::string _sDbUrl;
  std::unique_ptr<dns::dal::ConnectionPool> _cpPool;
  std::unique_ptr<dns::dal::SettingsRepository> _srRepo;
};

TEST_F(ConfigSeedingTest, SeedToDb_InsertsCompiledDefaults) {
  dns::common::Config::seedToDb(*_srRepo);

  // Verify a known setting was seeded with its compiled default
  auto oRow = _srRepo->findByKey("deployment.retention_count");
  ASSERT_TRUE(oRow.has_value());
  EXPECT_EQ(oRow->sValue, "10");

  auto oRow2 = _srRepo->findByKey("http.threads");
  ASSERT_TRUE(oRow2.has_value());
  EXPECT_EQ(oRow2->sValue, "4");
}

TEST_F(ConfigSeedingTest, SeedToDb_DoesNotOverwriteExistingValues) {
  _srRepo->upsert("deployment.retention_count", "50", "custom");

  dns::common::Config::seedToDb(*_srRepo);

  auto oRow = _srRepo->findByKey("deployment.retention_count");
  ASSERT_TRUE(oRow.has_value());
  EXPECT_EQ(oRow->sValue, "50");
}

TEST_F(ConfigSeedingTest, SeedToDb_SeedsAllDefinedSettings) {
  dns::common::Config::seedToDb(*_srRepo);

  for (const auto& def : dns::common::kSettings) {
    auto oRow = _srRepo->findByKey(std::string(def.sKey));
    ASSERT_TRUE(oRow.has_value()) << "Missing setting: " << def.sKey;
  }
}

TEST_F(ConfigSeedingTest, LoadFromDb_PopulatesConfigFields) {
  _srRepo->upsert("deployment.retention_count", "25");
  _srRepo->upsert("http.threads", "8");
  _srRepo->upsert("audit.stdout", "true");
  _srRepo->upsert("sync.check_interval_seconds", "0");

  dns::common::Config cfg;
  cfg.loadFromDb(*_srRepo);

  EXPECT_EQ(cfg.iDeploymentRetentionCount, 25);
  EXPECT_EQ(cfg.iHttpThreads, 8);
  EXPECT_TRUE(cfg.bAuditStdout);
  EXPECT_EQ(cfg.iSyncCheckInterval, 0);
}

TEST_F(ConfigSeedingTest, LoadFromDb_UsesFieldDefaultsForMissingKeys) {
  // Don't seed anything — loadFromDb should fall back to Config field defaults
  dns::common::Config cfg;
  int iOriginalRetention = cfg.iDeploymentRetentionCount;
  cfg.loadFromDb(*_srRepo);
  EXPECT_EQ(cfg.iDeploymentRetentionCount, iOriginalRetention);
}
