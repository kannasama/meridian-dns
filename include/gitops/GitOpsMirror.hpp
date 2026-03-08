#pragma once

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>

struct git_repository;
struct git_credential;
struct git_cert;

namespace dns::dal {
class RecordRepository;
class ViewRepository;
class ZoneRepository;
}  // namespace dns::dal

namespace dns::core {
class VariableEngine;
}

namespace dns::gitops {

/// Maintains a local bare-clone and pushes zone snapshots on deployment.
/// Class abbreviation: gm
class GitOpsMirror {
 public:
  GitOpsMirror(dns::dal::ZoneRepository& zrRepo,
               dns::dal::ViewRepository& vrRepo,
               dns::dal::RecordRepository& rrRepo,
               dns::core::VariableEngine& veEngine);
  ~GitOpsMirror();

  /// Clone or open existing repo. If remote URL is set, clone/fetch.
  void initialize(const std::string& sRemoteUrl, const std::string& sLocalPath,
                  const std::optional<std::string>& oSshKeyPath = std::nullopt,
                  const std::optional<std::string>& oKnownHostsFile = std::nullopt);

  /// Write zone snapshot and commit+push to remote.
  /// Serialized globally via mutex. Non-fatal: logs errors but does not throw.
  void commit(int64_t iZoneId, const std::string& sActorIdentity);

  /// Fetch latest from remote (called at startup).
  void pull();

  /// Build zone snapshot JSON string (public for testing).
  std::string buildSnapshotJson(int64_t iZoneId, const std::string& sActor) const;

  /// libgit2 credentials callback — provides SSH key authentication.
  static int credentialsCb(git_credential** ppOut, const char* pUrl,
                           const char* pUsernameFromUrl, unsigned int iAllowedTypes,
                           void* pPayload);

  /// libgit2 certificate_check callback — validates against known_hosts file.
  static int certificateCheckCb(git_cert* pCert, int bValid,
                                const char* pHost, void* pPayload);

 private:
  void writeZoneSnapshot(int64_t iZoneId, const std::string& sActor);
  void gitAddCommitPush(const std::string& sMessage);

  dns::dal::ZoneRepository& _zrRepo;
  dns::dal::ViewRepository& _vrRepo;
  dns::dal::RecordRepository& _rrRepo;
  dns::core::VariableEngine& _veEngine;

  std::string _sLocalPath;
  std::string _sRemoteUrl;
  std::optional<std::string> _oSshKeyPath;
  std::optional<std::string> _oKnownHostsFile;
  git_repository* _pRepo = nullptr;
  std::mutex _mtx;  // serializes all git operations
};

}  // namespace dns::gitops
