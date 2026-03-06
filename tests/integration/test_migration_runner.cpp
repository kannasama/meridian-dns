#include "dal/MigrationRunner.hpp"

#include "common/Logger.hpp"

#include <gtest/gtest.h>
#include <pqxx/pqxx>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;

using dns::dal::MigrationRunner;

namespace {

std::string getDbUrl() {
  const char* pUrl = std::getenv("DNS_DB_URL");
  return pUrl ? std::string(pUrl) : std::string{};
}

}  // namespace

class MigrationRunnerTest : public ::testing::Test {
 protected:
  void SetUp() override {
    _sDbUrl = getDbUrl();
    if (_sDbUrl.empty()) {
      GTEST_SKIP() << "DNS_DB_URL not set — skipping integration test";
    }
    dns::common::Logger::init("warn");

    // Create temp migrations directory
    _sTmpDir = fs::temp_directory_path() / "meridian_test_migrations";
    fs::create_directories(_sTmpDir);

    // Clean up any leftover test tables
    cleanupDb();
  }

  void TearDown() override {
    cleanupDb();
    if (!_sTmpDir.empty()) {
      fs::remove_all(_sTmpDir);
    }
  }

  void cleanupDb() {
    try {
      pqxx::connection conn(_sDbUrl);
      pqxx::work txn(conn);
      txn.exec("DROP TABLE IF EXISTS schema_version CASCADE");
      txn.exec("DROP TABLE IF EXISTS system_config CASCADE");
      txn.exec("DROP TABLE IF EXISTS test_migration_v1 CASCADE");
      txn.exec("DROP TABLE IF EXISTS test_migration_v2 CASCADE");
      txn.commit();
    } catch (...) {
    }
  }

  void createMigrationFile(const std::string& sVersion, const std::string& sFilename,
                           const std::string& sSql) {
    fs::path versionDir = _sTmpDir / sVersion;
    fs::create_directories(versionDir);
    std::ofstream ofs(versionDir / sFilename);
    ofs << sSql;
  }

  bool tableExists(const std::string& sTableName) {
    pqxx::connection conn(_sDbUrl);
    pqxx::nontransaction ntxn(conn);
    auto result = ntxn.exec(
        "SELECT EXISTS (SELECT 1 FROM information_schema.tables WHERE table_name = $1)",
        pqxx::params{sTableName});
    return result[0][0].as<bool>();
  }

  std::string _sDbUrl;
  fs::path _sTmpDir;
};

TEST_F(MigrationRunnerTest, BootstrapCreatesSchemaVersionTable) {
  MigrationRunner mrRunner(_sDbUrl, _sTmpDir.string());
  mrRunner.migrate();

  EXPECT_TRUE(tableExists("schema_version"));

  pqxx::connection conn(_sDbUrl);
  pqxx::nontransaction ntxn(conn);
  auto result = ntxn.exec("SELECT version FROM schema_version");
  ASSERT_FALSE(result.empty());
  EXPECT_EQ(result[0][0].as<int>(), 0);
}

TEST_F(MigrationRunnerTest, BootstrapCreatesSystemConfigTable) {
  MigrationRunner mrRunner(_sDbUrl, _sTmpDir.string());
  mrRunner.migrate();

  EXPECT_TRUE(tableExists("system_config"));
}

TEST_F(MigrationRunnerTest, BootstrapIsIdempotent) {
  MigrationRunner mrRunner(_sDbUrl, _sTmpDir.string());

  int iVersion1 = mrRunner.migrate();
  int iVersion2 = mrRunner.migrate();

  EXPECT_EQ(iVersion1, 0);
  EXPECT_EQ(iVersion2, 0);

  // Verify only one row in schema_version
  pqxx::connection conn(_sDbUrl);
  pqxx::nontransaction ntxn(conn);
  auto result = ntxn.exec("SELECT COUNT(*) FROM schema_version");
  EXPECT_EQ(result[0][0].as<int>(), 1);
}

TEST_F(MigrationRunnerTest, CurrentVersionReturnsZeroForFreshDb) {
  MigrationRunner mrRunner(_sDbUrl, _sTmpDir.string());
  mrRunner.migrate();

  EXPECT_EQ(mrRunner.currentVersion(), 0);
}

TEST_F(MigrationRunnerTest, MigrateAppliesVersionDirectories) {
  createMigrationFile("v001", "001_test.sql", "CREATE TABLE test_migration_v1 (id INT);");

  MigrationRunner mrRunner(_sDbUrl, _sTmpDir.string());
  int iVersion = mrRunner.migrate();

  EXPECT_EQ(iVersion, 1);
  EXPECT_TRUE(tableExists("test_migration_v1"));
  EXPECT_EQ(mrRunner.currentVersion(), 1);
}

TEST_F(MigrationRunnerTest, MigrateSkipsAlreadyAppliedVersions) {
  createMigrationFile("v001", "001_test.sql", "CREATE TABLE test_migration_v1 (id INT);");

  MigrationRunner mrRunner(_sDbUrl, _sTmpDir.string());
  int iVersion1 = mrRunner.migrate();
  int iVersion2 = mrRunner.migrate();

  EXPECT_EQ(iVersion1, 1);
  EXPECT_EQ(iVersion2, 1);
  EXPECT_TRUE(tableExists("test_migration_v1"));
}

TEST_F(MigrationRunnerTest, MigrateAppliesMultipleVersionsInOrder) {
  createMigrationFile("v001", "001_test.sql", "CREATE TABLE test_migration_v1 (id INT);");
  createMigrationFile("v002", "001_test.sql", "CREATE TABLE test_migration_v2 (id INT);");

  MigrationRunner mrRunner(_sDbUrl, _sTmpDir.string());
  int iVersion = mrRunner.migrate();

  EXPECT_EQ(iVersion, 2);
  EXPECT_TRUE(tableExists("test_migration_v1"));
  EXPECT_TRUE(tableExists("test_migration_v2"));
  EXPECT_EQ(mrRunner.currentVersion(), 2);
}
