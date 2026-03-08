#include "gitops/GitOpsMirror.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>

#include <git2.h>
#include <nlohmann/json.hpp>

#include "common/Errors.hpp"
#include "common/Logger.hpp"
#include "core/VariableEngine.hpp"
#include "dal/RecordRepository.hpp"
#include "dal/ViewRepository.hpp"
#include "dal/ZoneRepository.hpp"

namespace dns::gitops {

GitOpsMirror::GitOpsMirror(dns::dal::ZoneRepository& zrRepo,
                           dns::dal::ViewRepository& vrRepo,
                           dns::dal::RecordRepository& rrRepo,
                           dns::core::VariableEngine& veEngine)
    : _zrRepo(zrRepo), _vrRepo(vrRepo), _rrRepo(rrRepo), _veEngine(veEngine) {
  git_libgit2_init();
}

GitOpsMirror::~GitOpsMirror() {
  if (_pRepo) git_repository_free(_pRepo);
  git_libgit2_shutdown();
}

// ── libgit2 callbacks ──────────────────────────────────────────────────────

int GitOpsMirror::credentialsCb(git_credential** ppOut, const char* /*pUrl*/,
                                const char* pUsernameFromUrl, unsigned int iAllowedTypes,
                                void* pPayload) {
  auto* pSelf = static_cast<const GitOpsMirror*>(pPayload);

  if ((iAllowedTypes & GIT_CREDENTIAL_SSH_KEY) && pSelf->_oSshKeyPath.has_value()) {
    const char* pUsername = (pUsernameFromUrl && pUsernameFromUrl[0])
                                ? pUsernameFromUrl
                                : "git";
    return git_credential_ssh_key_new(ppOut, pUsername,
                                      nullptr,  // public key path (derived from private)
                                      pSelf->_oSshKeyPath->c_str(),
                                      nullptr);  // passphrase
  }

  return GIT_PASSTHROUGH;
}

int GitOpsMirror::certificateCheckCb(git_cert* /*pCert*/, int /*bValid*/,
                                     const char* pHost, void* pPayload) {
  auto* pSelf = static_cast<const GitOpsMirror*>(pPayload);

  if (!pSelf->_oKnownHostsFile.has_value()) {
    // No known_hosts file configured — accept all hosts (matches previous behavior).
    // When a known_hosts file is provided, this should be tightened.
    return 0;
  }

  // Validate host against known_hosts file using libgit2's built-in check.
  // git_libgit2_opts can set the known_hosts path globally, but that's process-wide.
  // Instead, we do a manual line-based check for the hostname.
  std::ifstream ifs(pSelf->_oKnownHostsFile.value());
  if (!ifs.is_open()) {
    auto spLog = common::Logger::get();
    spLog->error("GitOpsMirror: cannot open known_hosts file '{}'",
                 pSelf->_oKnownHostsFile.value());
    return GIT_ECERTIFICATE;
  }

  std::string sHost(pHost ? pHost : "");
  std::string sLine;
  while (std::getline(ifs, sLine)) {
    if (sLine.empty() || sLine[0] == '#') continue;
    // known_hosts format: hostname[,hostname...] key-type base64-key [comment]
    // Check if the line starts with our hostname
    auto iSpace = sLine.find(' ');
    if (iSpace == std::string::npos) continue;
    std::string sHosts = sLine.substr(0, iSpace);
    // Check comma-separated host list
    std::istringstream iss(sHosts);
    std::string sEntry;
    while (std::getline(iss, sEntry, ',')) {
      // Strip brackets and port (e.g., [hostname]:port)
      if (!sEntry.empty() && sEntry[0] == '[') {
        auto iBracket = sEntry.find(']');
        if (iBracket != std::string::npos) {
          sEntry = sEntry.substr(1, iBracket - 1);
        }
      }
      if (sEntry == sHost) {
        return 0;  // Host found — accept
      }
    }
  }

  auto spLog = common::Logger::get();
  spLog->error("GitOpsMirror: host '{}' not found in known_hosts file '{}'",
               sHost, pSelf->_oKnownHostsFile.value());
  return GIT_ECERTIFICATE;
}

// ── Options helpers ─────────────────────────────────────────────────────────

namespace {

void applyRemoteCallbacks(git_remote_callbacks& cb, GitOpsMirror* pMirror,
                          bool bHasSshKey) {
  if (bHasSshKey) {
    cb.credentials = &GitOpsMirror::credentialsCb;
  }
  cb.certificate_check = &GitOpsMirror::certificateCheckCb;
  cb.payload = pMirror;
}

}  // namespace

// ── Core operations ─────────────────────────────────────────────────────────

void GitOpsMirror::initialize(const std::string& sRemoteUrl, const std::string& sLocalPath,
                              const std::optional<std::string>& oSshKeyPath,
                              const std::optional<std::string>& oKnownHostsFile) {
  auto spLog = common::Logger::get();
  _sRemoteUrl = sRemoteUrl;
  _sLocalPath = sLocalPath;
  _oSshKeyPath = oSshKeyPath;
  _oKnownHostsFile = oKnownHostsFile;

  namespace fs = std::filesystem;

  // libgit2's SSH transport reads $HOMEDIR/.ssh/known_hosts internally before
  // invoking our certificate_check callback. In container/service contexts,
  // ~/.ssh/known_hosts often doesn't exist, causing "error loading known_hosts".
  // Fix: set HOMEDIR to a sibling directory of the repo path so it stays outside
  // the git working tree but within the same mount (e.g. /var/meridian-dns/.gitssh).
  fs::path sshHome = fs::path(sLocalPath).parent_path() / ".gitssh";
  fs::path sshDir = sshHome / ".ssh";
  fs::create_directories(sshDir);
  fs::path knownHostsPath = sshDir / "known_hosts";
  if (oKnownHostsFile.has_value()) {
    // Symlink to the user-provided known_hosts file
    std::error_code ec;
    fs::remove(knownHostsPath, ec);
    fs::create_symlink(oKnownHostsFile.value(), knownHostsPath, ec);
    if (ec) {
      // Fallback: copy the file if symlink fails
      fs::copy_file(oKnownHostsFile.value(), knownHostsPath,
                    fs::copy_options::overwrite_existing, ec);
    }
    spLog->info("GitOpsMirror: using known_hosts from '{}'", oKnownHostsFile.value());
  } else if (!fs::exists(knownHostsPath)) {
    // Create empty known_hosts so libgit2 SSH transport doesn't error.
    // Our certificate_check callback accepts all hosts when no file is configured.
    std::ofstream ofs(knownHostsPath);
    ofs.close();
  }
  git_libgit2_opts(GIT_OPT_SET_HOMEDIR, sshHome.c_str());
  if (fs::exists(sLocalPath + "/.git") || fs::exists(sLocalPath + "/HEAD")) {
    // Open existing repo
    int iErr = git_repository_open(&_pRepo, sLocalPath.c_str());
    if (iErr < 0) {
      const git_error* pErr = git_error_last();
      spLog->error("GitOpsMirror: failed to open repo at '{}': {}", sLocalPath,
                   pErr ? pErr->message : "unknown");
      throw common::GitMirrorError("GIT_OPEN_FAILED",
                                   "Failed to open git repo: " +
                                       std::string(pErr ? pErr->message : "unknown"));
    }
    spLog->info("GitOpsMirror: opened existing repo at '{}'", sLocalPath);
  } else {
    // Clone from remote
    fs::create_directories(sLocalPath);

    git_clone_options cloneOpts;
    git_clone_options_init(&cloneOpts, GIT_CLONE_OPTIONS_VERSION);
    applyRemoteCallbacks(cloneOpts.fetch_opts.callbacks, const_cast<GitOpsMirror*>(this),
                         _oSshKeyPath.has_value());

    int iErr = git_clone(&_pRepo, sRemoteUrl.c_str(), sLocalPath.c_str(), &cloneOpts);
    if (iErr < 0) {
      const git_error* pErr = git_error_last();
      spLog->error("GitOpsMirror: failed to clone '{}' to '{}': {}", sRemoteUrl, sLocalPath,
                   pErr ? pErr->message : "unknown");
      throw common::GitMirrorError("GIT_CLONE_FAILED",
                                   "Failed to clone git repo: " +
                                       std::string(pErr ? pErr->message : "unknown"));
    }
    spLog->info("GitOpsMirror: cloned '{}' to '{}'", sRemoteUrl, sLocalPath);
  }
}

void GitOpsMirror::pull() {
  // Fetch from origin and reset to FETCH_HEAD
  // This is a simplified pull — the mirror is append/overwrite only
  if (!_pRepo) return;

  std::lock_guard lock(_mtx);
  auto spLog = common::Logger::get();

  git_remote* pRemote = nullptr;
  int iErr = git_remote_lookup(&pRemote, _pRepo, "origin");
  if (iErr < 0) {
    spLog->warn("GitOpsMirror: no 'origin' remote — skipping pull");
    return;
  }

  git_fetch_options fetchOpts;
  git_fetch_options_init(&fetchOpts, GIT_FETCH_OPTIONS_VERSION);
  applyRemoteCallbacks(fetchOpts.callbacks, const_cast<GitOpsMirror*>(this),
                       _oSshKeyPath.has_value());

  iErr = git_remote_fetch(pRemote, nullptr, &fetchOpts, nullptr);
  git_remote_free(pRemote);
  if (iErr < 0) {
    const git_error* pErr = git_error_last();
    spLog->warn("GitOpsMirror: fetch failed: {}", pErr ? pErr->message : "unknown");
  } else {
    spLog->info("GitOpsMirror: fetched from origin");
  }
}

std::string GitOpsMirror::buildSnapshotJson(int64_t iZoneId,
                                            const std::string& sActor) const {
  auto oZone = _zrRepo.findById(iZoneId);
  if (!oZone) {
    throw common::NotFoundError("ZONE_NOT_FOUND",
                                "Zone " + std::to_string(iZoneId) + " not found");
  }

  auto oView = _vrRepo.findById(oZone->iViewId);
  std::string sViewName = oView ? oView->sName : "unknown";

  auto vRecords = _rrRepo.listByZoneId(iZoneId);

  // Build records array with expanded values
  nlohmann::json jRecords = nlohmann::json::array();
  for (const auto& rec : vRecords) {
    std::string sExpandedValue;
    try {
      sExpandedValue = _veEngine.expand(rec.sValueTemplate, iZoneId);
    } catch (...) {
      sExpandedValue = rec.sValueTemplate;  // fallback to raw if expansion fails
    }

    jRecords.push_back({
        {"record_id", rec.iId},
        {"name", rec.sName},
        {"type", rec.sType},
        {"ttl", rec.iTtl},
        {"value_template", rec.sValueTemplate},
        {"value", sExpandedValue},
        {"priority", rec.iPriority},
    });
  }

  // ISO 8601 timestamp
  auto tpNow = std::chrono::system_clock::now();
  auto ttNow = std::chrono::system_clock::to_time_t(tpNow);
  std::ostringstream oss;
  oss << std::put_time(std::gmtime(&ttNow), "%FT%TZ");

  nlohmann::json j = {
      {"zone", oZone->sName},
      {"view", sViewName},
      {"generated_at", oss.str()},
      {"generated_by", sActor},
      {"records", jRecords},
  };
  return j.dump(2);
}

void GitOpsMirror::writeZoneSnapshot(int64_t iZoneId, const std::string& sActor) {
  auto oZone = _zrRepo.findById(iZoneId);
  if (!oZone) return;

  auto oView = _vrRepo.findById(oZone->iViewId);
  std::string sViewName = oView ? oView->sName : "unknown";

  // Build path: {local_path}/{view_name}/{zone_name}.json
  namespace fs = std::filesystem;
  fs::path dirPath = fs::path(_sLocalPath) / sViewName;
  fs::create_directories(dirPath);

  fs::path filePath = dirPath / (oZone->sName + ".json");
  std::string sJson = buildSnapshotJson(iZoneId, sActor);

  std::ofstream ofs(filePath);
  ofs << sJson;
  ofs.close();
}

void GitOpsMirror::gitAddCommitPush(const std::string& sMessage) {
  if (!_pRepo) return;
  auto spLog = common::Logger::get();

  // Stage all changes
  git_index* pIndex = nullptr;
  git_repository_index(&pIndex, _pRepo);
  git_index_add_all(pIndex, nullptr, 0, nullptr, nullptr);
  git_index_write(pIndex);

  // Create tree from index
  git_oid treeOid;
  git_index_write_tree(&treeOid, pIndex);
  git_index_free(pIndex);

  git_tree* pTree = nullptr;
  git_tree_lookup(&pTree, _pRepo, &treeOid);

  // Get HEAD commit as parent (if exists)
  git_reference* pHead = nullptr;
  git_commit* pParent = nullptr;
  bool bHasParent = false;
  if (git_repository_head(&pHead, _pRepo) == 0) {
    git_oid parentOid;
    git_reference_name_to_id(&parentOid, _pRepo, "HEAD");
    git_commit_lookup(&pParent, _pRepo, &parentOid);
    bHasParent = true;
  }

  // Create commit
  git_signature* pSig = nullptr;
  git_signature_now(&pSig, "meridian-dns", "meridian@dns.local");

  git_oid commitOid;
  const git_commit* vParents[] = {pParent};
  git_commit_create(&commitOid, _pRepo, "HEAD", pSig, pSig, "UTF-8", sMessage.c_str(),
                    pTree, bHasParent ? 1 : 0, bHasParent ? vParents : nullptr);

  git_signature_free(pSig);
  git_tree_free(pTree);
  if (pParent) git_commit_free(pParent);
  if (pHead) git_reference_free(pHead);

  // Push to origin
  git_remote* pRemote = nullptr;
  if (git_remote_lookup(&pRemote, _pRepo, "origin") == 0) {
    const char* vRefspecs[] = {"refs/heads/main:refs/heads/main"};
    git_strarray refspecs = {const_cast<char**>(vRefspecs), 1};

    git_push_options pushOpts;
    git_push_options_init(&pushOpts, GIT_PUSH_OPTIONS_VERSION);
    applyRemoteCallbacks(pushOpts.callbacks, const_cast<GitOpsMirror*>(this),
                         _oSshKeyPath.has_value());

    int iErr = git_remote_push(pRemote, &refspecs, &pushOpts);
    if (iErr < 0) {
      const git_error* pErr = git_error_last();
      spLog->warn("GitOpsMirror: push failed: {}", pErr ? pErr->message : "unknown");
    }
    git_remote_free(pRemote);
  }
}

void GitOpsMirror::commit(int64_t iZoneId, const std::string& sActorIdentity) {
  std::lock_guard lock(_mtx);
  auto spLog = common::Logger::get();

  try {
    auto oZone = _zrRepo.findById(iZoneId);
    std::string sZoneName = oZone ? oZone->sName : std::to_string(iZoneId);

    writeZoneSnapshot(iZoneId, sActorIdentity);
    gitAddCommitPush("Update " + sZoneName + " by " + sActorIdentity + " via API");
    spLog->info("GitOpsMirror: committed zone '{}' by {}", sZoneName, sActorIdentity);
  } catch (const std::exception& ex) {
    // Non-fatal: log and continue. GitMirror failure should not block deployment.
    spLog->error("GitOpsMirror: commit failed for zone {}: {}", iZoneId, ex.what());
  }
}

}  // namespace dns::gitops
