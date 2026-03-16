// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Meridian DNS Contributors
// This file is part of Meridian DNS. See LICENSE for details.

#include "dal/MigrationRunner.hpp"

#include "common/Logger.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <vector>

#include <pqxx/pqxx>

namespace dns::dal {

namespace fs = std::filesystem;

MigrationRunner::MigrationRunner(const std::string& sDbUrl, const std::string& sMigrationsDir)
    : _sDbUrl(sDbUrl), _sMigrationsDir(sMigrationsDir) {}

void MigrationRunner::bootstrap() {
  auto spLog = common::Logger::get();
  spLog->debug("Bootstrapping schema_version and system_config tables");

  pqxx::connection conn(_sDbUrl);
  pqxx::work txn(conn);

  txn.exec("CREATE TABLE IF NOT EXISTS schema_version (version INTEGER NOT NULL)");
  txn.exec("CREATE TABLE IF NOT EXISTS system_config (key TEXT PRIMARY KEY, value TEXT NOT NULL)");
  txn.exec(
      "INSERT INTO schema_version (version) "
      "SELECT 0 WHERE NOT EXISTS (SELECT 1 FROM schema_version)");

  txn.commit();
  spLog->debug("Bootstrap complete");
}

int MigrationRunner::currentVersion() {
  try {
    pqxx::connection conn(_sDbUrl);
    pqxx::nontransaction ntxn(conn);
    auto result = ntxn.exec("SELECT version FROM schema_version");
    if (result.empty()) {
      return 0;
    }
    return result[0][0].as<int>();
  } catch (const pqxx::sql_error&) {
    return 0;
  }
}

int MigrationRunner::migrate() {
  auto spLog = common::Logger::get();

  bootstrap();
  int iLiveVersion = currentVersion();
  spLog->info("Current schema version: {}", iLiveVersion);

  // Scan for vNNN directories
  if (!fs::exists(_sMigrationsDir) || !fs::is_directory(_sMigrationsDir)) {
    spLog->warn("Migrations directory does not exist: {}", _sMigrationsDir);
    return iLiveVersion;
  }

  struct VersionDir {
    int iVersion;
    fs::path pPath;
  };
  std::vector<VersionDir> vDirs;

  for (const auto& entry : fs::directory_iterator(_sMigrationsDir)) {
    if (!entry.is_directory()) {
      continue;
    }
    std::string sDirName = entry.path().filename().string();
    if (sDirName.empty() || sDirName[0] != 'v') {
      continue;
    }
    try {
      int iDirVersion = std::stoi(sDirName.substr(1));
      vDirs.push_back({iDirVersion, entry.path()});
    } catch (const std::exception&) {
      spLog->warn("Skipping non-version directory: {}", sDirName);
    }
  }

  std::sort(vDirs.begin(), vDirs.end(),
            [](const VersionDir& a, const VersionDir& b) { return a.iVersion < b.iVersion; });

  int iFinalVersion = iLiveVersion;

  for (const auto& vd : vDirs) {
    if (vd.iVersion <= iLiveVersion) {
      continue;
    }

    // Collect .sql files in this version directory
    std::vector<fs::path> vSqlFiles;
    for (const auto& fileEntry : fs::directory_iterator(vd.pPath)) {
      if (fileEntry.is_regular_file() && fileEntry.path().extension() == ".sql") {
        vSqlFiles.push_back(fileEntry.path());
      }
    }

    std::sort(vSqlFiles.begin(), vSqlFiles.end(),
              [](const fs::path& a, const fs::path& b) {
                return a.filename().string() < b.filename().string();
              });

    if (vSqlFiles.empty()) {
      spLog->warn("Version directory v{:03d} contains no SQL files, skipping", vd.iVersion);
      continue;
    }

    try {
      pqxx::connection conn(_sDbUrl);
      pqxx::work txn(conn);

      for (const auto& pSqlFile : vSqlFiles) {
        std::ifstream ifs(pSqlFile);
        if (!ifs.is_open()) {
          throw std::runtime_error("Cannot open migration file: " + pSqlFile.string());
        }
        std::ostringstream oss;
        oss << ifs.rdbuf();
        std::string sFileContents = oss.str();

        txn.exec(sFileContents);
      }

      txn.exec("UPDATE schema_version SET version = " + std::to_string(vd.iVersion));
      txn.commit();

      iFinalVersion = vd.iVersion;
      spLog->info("Migration v{:03d} applied ({} files)", vd.iVersion, vSqlFiles.size());

    } catch (const std::exception& ex) {
      spLog->error("Migration v{:03d} failed: {}", vd.iVersion, ex.what());
      throw;
    }
  }

  spLog->info("Migrations complete. Schema version: {}", iFinalVersion);
  return iFinalVersion;
}

}  // namespace dns::dal
