// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Meridian DNS Contributors
// This file is part of Meridian DNS. See LICENSE for details.

#include "providers/SubprocessProvider.hpp"

#include <nlohmann/json.hpp>

#include <cerrno>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

#include <stdexcept>
#include <string>
#include <vector>

#include "common/Errors.hpp"
#include "common/Logger.hpp"

namespace dns::providers {

using json = nlohmann::json;

SubprocessProvider::SubprocessProvider(std::string /*sApiEndpoint*/, std::string sToken,
                                       nlohmann::json jDefinition)
    : _sToken(std::move(sToken)),
      _jDef(std::move(jDefinition)) {
  _sBinaryPath = _jDef.value("binary_path", "");

  if (_sBinaryPath.empty()) {
    throw common::ValidationError("SUBPROCESS_NO_BINARY",
                                  "Subprocess provider definition missing 'binary_path'");
  }

  // Validate path safety — binary_path must be absolute with safe characters
  static const std::string kAllowedChars =
      "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789/_-.";
  if (_sBinaryPath[0] != '/' ||
      _sBinaryPath.find_first_not_of(kAllowedChars) != std::string::npos) {
    throw common::ValidationError(
        "SUBPROCESS_INVALID_BINARY_PATH",
        "binary_path must be an absolute path containing only safe characters (a-z, A-Z, 0-9, /, -, _, .)");
  }
}

SubprocessProvider::~SubprocessProvider() = default;

std::string SubprocessProvider::name() const { return "subprocess"; }

// ─────────────────────────────────────────────────────────────────────────────
// callSubprocess — launch binary via posix_spawn, pipe JSON in/out, no shell
// ─────────────────────────────────────────────────────────────────────────────
std::string SubprocessProvider::callSubprocess(const std::string& sInput) const {
  // ── Create pipes ──────────────────────────────────────────────────────────
  int aStdinPipe[2];   // parent writes → child reads
  int aStdoutPipe[2];  // child writes → parent reads

  if (pipe(aStdinPipe) != 0) {
    throw common::ProviderError("SUBPROCESS_PIPE_FAILED",
                                "Failed to create I/O pipes for subprocess");
  }
  if (pipe(aStdoutPipe) != 0) {
    close(aStdinPipe[0]);
    close(aStdinPipe[1]);
    throw common::ProviderError("SUBPROCESS_PIPE_FAILED",
                                "Failed to create I/O pipes for subprocess");
  }

  // ── posix_spawn file actions ──────────────────────────────────────────────
  posix_spawn_file_actions_t fa;
  posix_spawn_file_actions_init(&fa);
  posix_spawn_file_actions_adddup2(&fa, aStdinPipe[0], STDIN_FILENO);
  posix_spawn_file_actions_adddup2(&fa, aStdoutPipe[1], STDOUT_FILENO);
  posix_spawn_file_actions_addclose(&fa, aStdinPipe[0]);   // close original after dup2
  posix_spawn_file_actions_addclose(&fa, aStdoutPipe[1]);  // close original after dup2
  posix_spawn_file_actions_addclose(&fa, aStdinPipe[1]);   // not needed in child
  posix_spawn_file_actions_addclose(&fa, aStdoutPipe[0]);  // not needed in child

  // ── Launch subprocess (no shell) ──────────────────────────────────────────
  char* const argv[] = {const_cast<char*>(_sBinaryPath.c_str()), nullptr};
  char* const envp[] = {nullptr};  // empty environment
  pid_t pid = 0;
  int iSpawnRet = posix_spawn(&pid, _sBinaryPath.c_str(), &fa, nullptr, argv, envp);
  posix_spawn_file_actions_destroy(&fa);

  close(aStdinPipe[0]);
  close(aStdoutPipe[1]);

  if (iSpawnRet != 0) {
    close(aStdinPipe[1]);
    close(aStdoutPipe[0]);
    throw common::ProviderError("SUBPROCESS_LAUNCH_FAILED",
                                "posix_spawn failed for: " + _sBinaryPath);
  }

  // ── Write JSON input to child's stdin ─────────────────────────────────────
  const char* pData = sInput.data();
  size_t nRemaining = sInput.size();
  bool bWriteError = false;
  while (nRemaining > 0) {
    ssize_t nWritten = write(aStdinPipe[1], pData, nRemaining);
    if (nWritten < 0) {
      if (errno == EINTR) continue;
      bWriteError = true; break;
    }
    pData      += nWritten;
    nRemaining -= static_cast<size_t>(nWritten);
  }
  close(aStdinPipe[1]);

  if (bWriteError) {
    close(aStdoutPipe[0]);
    waitpid(pid, nullptr, 0);
    throw common::ProviderError("SUBPROCESS_WRITE_FAILED",
                                "Failed to write JSON to subprocess stdin");
  }

  // ── Read one JSON line from child's stdout ────────────────────────────────
  static constexpr size_t kMaxResponseBytes = 16 * 1024 * 1024;  // 16 MB
  std::string sResponse;
  char aBuf[4096];
  ssize_t nRead = 0;
  while ((nRead = read(aStdoutPipe[0], aBuf, sizeof(aBuf))) > 0 ||
         (nRead < 0 && errno == EINTR)) {
    if (nRead < 0) continue;  // EINTR, retry
    sResponse.append(aBuf, static_cast<size_t>(nRead));
    if (sResponse.size() > kMaxResponseBytes) {
      close(aStdoutPipe[0]);
      waitpid(pid, nullptr, 0);
      throw common::ProviderError("SUBPROCESS_RESPONSE_TOO_LARGE",
                                  "Subprocess response exceeds 16 MB limit");
    }
    if (sResponse.find('\n') != std::string::npos) break;
  }
  close(aStdoutPipe[0]);

  int iStatus = 0;
  waitpid(pid, &iStatus, 0);
  if (WIFSIGNALED(iStatus)) {
    auto spLog = common::Logger::get();
    spLog->debug("Subprocess {} killed by signal {}", _sBinaryPath, WTERMSIG(iStatus));
  } else if (WIFEXITED(iStatus) && WEXITSTATUS(iStatus) != 0) {
    auto spLog = common::Logger::get();
    spLog->debug("Subprocess {} exited with status {}", _sBinaryPath, WEXITSTATUS(iStatus));
  }

  while (!sResponse.empty() &&
         (sResponse.back() == '\n' || sResponse.back() == '\r')) {
    sResponse.pop_back();
  }

  return sResponse;
}

// ─────────────────────────────────────────────────────────────────────────────
// invoke — build JSON-RPC request, call subprocess, parse response
// ─────────────────────────────────────────────────────────────────────────────
nlohmann::json SubprocessProvider::invoke(const std::string& sMethod,
                                           const nlohmann::json& jParams) const {
  auto spLog = common::Logger::get();

  json jRequest = {{"method", sMethod}, {"params", jParams}, {"id", 1}, {"token", _sToken}};
  std::string sInput = jRequest.dump() + "\n";

  spLog->debug("SubprocessProvider::invoke method={} binary={}", sMethod, _sBinaryPath);

  std::string sResponse = callSubprocess(sInput);

  if (sResponse.empty()) {
    throw common::ProviderError("SUBPROCESS_NO_RESPONSE",
                                "Subprocess returned no output for method: " + sMethod);
  }

  json jResponse = json::parse(sResponse, nullptr, false);
  if (jResponse.is_discarded()) {
    throw common::ProviderError("SUBPROCESS_MALFORMED_JSON",
                                "Subprocess returned malformed JSON for method: " + sMethod);
  }

  if (jResponse.contains("error") && !jResponse["error"].is_null()) {
    std::string sErrMsg = jResponse["error"].value("message", "Unknown subprocess error");
    throw common::ProviderError("SUBPROCESS_ERROR", sErrMsg);
  }

  if (!jResponse.contains("result")) {
    throw common::ProviderError("SUBPROCESS_NO_RESULT",
                                "Subprocess response missing 'result' field for method: " + sMethod);
  }
  return jResponse["result"];
}

common::DnsRecord SubprocessProvider::mapRecord(const nlohmann::json& jRecord) const {
  common::DnsRecord dr;

  if (jRecord.contains("id") && jRecord["id"].is_string()) {
    dr.sProviderRecordId = jRecord["id"].get<std::string>();
  } else if (jRecord.contains("id")) {
    dr.sProviderRecordId = jRecord["id"].dump();
  }

  dr.sName = jRecord.value("name", "");
  dr.sType = jRecord.value("type", "");
  dr.sValue = jRecord.value("value", "");
  dr.uTtl = jRecord.value("ttl", 300u);
  dr.iPriority = jRecord.value("priority", 0);

  return dr;
}

common::HealthStatus SubprocessProvider::testConnectivity() {
  try {
    invoke("testConnectivity", json::object());
    return common::HealthStatus::Ok;
  } catch (const std::exception& e) {
    auto spLog = common::Logger::get();
    spLog->warn("SubprocessProvider::testConnectivity failed: {}", e.what());
    return common::HealthStatus::Unreachable;
  }
}

std::vector<common::DnsRecord> SubprocessProvider::listRecords(const std::string& sZoneName) {
  json jResult = invoke("listRecords", {{"zone", sZoneName}});

  std::vector<common::DnsRecord> vRecords;
  if (!jResult.is_array()) {
    return vRecords;
  }

  vRecords.reserve(jResult.size());
  for (const auto& jRecord : jResult) {
    vRecords.push_back(mapRecord(jRecord));
  }
  return vRecords;
}

common::PushResult SubprocessProvider::createRecord(const std::string& sZoneName,
                                                     const common::DnsRecord& drRecord) {
  try {
    json jParams = {
        {"zone", sZoneName},
        {"name", drRecord.sName},
        {"type", drRecord.sType},
        {"value", drRecord.sValue},
        {"ttl", drRecord.uTtl},
        {"priority", drRecord.iPriority},
    };

    json jResult = invoke("createRecord", jParams);

    std::string sId;
    if (jResult.contains("id") && jResult["id"].is_string()) {
      sId = jResult["id"].get<std::string>();
    } else if (jResult.contains("id")) {
      sId = jResult["id"].dump();
    }

    return {true, sId, ""};
  } catch (const std::exception& e) {
    auto spLog = common::Logger::get();
    spLog->warn("SubprocessProvider::createRecord failed for zone {}: {}", sZoneName, e.what());
    return {false, "", e.what()};
  }
}

common::PushResult SubprocessProvider::updateRecord(const std::string& sZoneName,
                                                     const common::DnsRecord& drRecord) {
  try {
    json jParams = {
        {"zone", sZoneName},
        {"id", drRecord.sProviderRecordId},
        {"name", drRecord.sName},
        {"type", drRecord.sType},
        {"value", drRecord.sValue},
        {"ttl", drRecord.uTtl},
        {"priority", drRecord.iPriority},
    };

    invoke("updateRecord", jParams);
    return {true, drRecord.sProviderRecordId, ""};
  } catch (const std::exception& e) {
    auto spLog = common::Logger::get();
    spLog->warn("SubprocessProvider::updateRecord failed for zone {}: {}", sZoneName, e.what());
    return {false, "", e.what()};
  }
}

common::PushResult SubprocessProvider::deleteRecord(const std::string& sZoneName,
                                                     const std::string& sProviderRecordId) {
  try {
    invoke("deleteRecord", {{"zone", sZoneName}, {"id", sProviderRecordId}});
    return {true, sProviderRecordId, ""};
  } catch (const std::exception& e) {
    return {false, "", e.what()};
  }
}

}  // namespace dns::providers
