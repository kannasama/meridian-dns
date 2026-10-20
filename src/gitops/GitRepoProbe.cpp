// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Meridian DNS Contributors
// This file is part of Meridian DNS. See LICENSE for details.

#include "gitops/GitRepoProbe.hpp"

#include <netdb.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <filesystem>
#include <string>

#include "common/Logger.hpp"

namespace dns::gitops {

namespace {

constexpr int kDefaultSshPort = 22;
constexpr int kDefaultHttpPort = 80;
constexpr int kDefaultHttpsPort = 443;

/// Splits "host:port" / "[v6::addr]:port" into host and optional port.
void splitHostPort(const std::string& sAuthority, std::string& sHost, int& iPort) {
  if (!sAuthority.empty() && sAuthority.front() == '[') {
    // Bracketed IPv6 literal — the brackets are URL syntax, not part of the host.
    auto nClose = sAuthority.find(']');
    if (nClose != std::string::npos) {
      sHost = sAuthority.substr(1, nClose - 1);
      if (nClose + 2 <= sAuthority.size() && sAuthority[nClose + 1] == ':') {
        try {
          iPort = std::stoi(sAuthority.substr(nClose + 2));
        } catch (...) {
          // Leave the scheme default in place.
        }
      }
      return;
    }
  }

  auto nColon = sAuthority.rfind(':');
  if (nColon == std::string::npos) {
    sHost = sAuthority;
    return;
  }

  sHost = sAuthority.substr(0, nColon);
  try {
    iPort = std::stoi(sAuthority.substr(nColon + 1));
  } catch (...) {
    // Not a numeric port — treat the whole thing as the host.
    sHost = sAuthority;
  }
}

/// Strips any "user[:password]@" prefix from an authority component.
std::string stripUserInfo(const std::string& sAuthority) {
  auto nAt = sAuthority.rfind('@');
  return (nAt == std::string::npos) ? sAuthority : sAuthority.substr(nAt + 1);
}

/// Non-blocking TCP connect with a bounded timeout.
/// Returns an empty string on success, else the failure reason.
std::string tcpConnect(const std::string& sHost, int iPort, int iTimeoutMs) {
  addrinfo hints{};
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;

  addrinfo* pRes = nullptr;
  int iGai = ::getaddrinfo(sHost.c_str(), std::to_string(iPort).c_str(), &hints, &pRes);
  if (iGai != 0 || !pRes) {
    if (pRes) ::freeaddrinfo(pRes);
    return "cannot resolve host '" + sHost + "': " + ::gai_strerror(iGai);
  }

  std::string sLastError = "no usable address for host '" + sHost + "'";

  for (addrinfo* pAi = pRes; pAi != nullptr; pAi = pAi->ai_next) {
    int iFd = ::socket(pAi->ai_family, pAi->ai_socktype | SOCK_NONBLOCK, pAi->ai_protocol);
    if (iFd < 0) {
      sLastError = std::string("socket() failed: ") + std::strerror(errno);
      continue;
    }

    int iRc = ::connect(iFd, pAi->ai_addr, pAi->ai_addrlen);
    if (iRc == 0) {
      ::close(iFd);
      ::freeaddrinfo(pRes);
      return "";  // connected immediately (typically loopback)
    }

    if (errno != EINPROGRESS) {
      sLastError = std::string("connect to ") + sHost + ":" + std::to_string(iPort) +
                   " failed: " + std::strerror(errno);
      ::close(iFd);
      continue;
    }

    pollfd pfd{};
    pfd.fd = iFd;
    pfd.events = POLLOUT;

    int iPoll = ::poll(&pfd, 1, iTimeoutMs);
    if (iPoll == 0) {
      sLastError = "connect to " + sHost + ":" + std::to_string(iPort) +
                   " timed out after " + std::to_string(iTimeoutMs) + "ms";
      ::close(iFd);
      continue;
    }
    if (iPoll < 0) {
      sLastError = std::string("poll() failed: ") + std::strerror(errno);
      ::close(iFd);
      continue;
    }

    // poll() reporting writability does not itself mean the connect succeeded;
    // SO_ERROR carries the real outcome.
    int iSoError = 0;
    socklen_t len = sizeof(iSoError);
    if (::getsockopt(iFd, SOL_SOCKET, SO_ERROR, &iSoError, &len) < 0) {
      sLastError = std::string("getsockopt(SO_ERROR) failed: ") + std::strerror(errno);
      ::close(iFd);
      continue;
    }
    ::close(iFd);

    if (iSoError == 0) {
      ::freeaddrinfo(pRes);
      return "";  // success
    }

    sLastError = "connect to " + sHost + ":" + std::to_string(iPort) +
                 " failed: " + std::strerror(iSoError);
  }

  ::freeaddrinfo(pRes);
  return sLastError;
}

}  // namespace

GitRepoProbe::GitRepoProbe(int iTimeoutMs, int iCacheTtlSeconds)
    : _iTimeoutMs(iTimeoutMs > 0 ? iTimeoutMs : 3000),
      _iCacheTtlSeconds(iCacheTtlSeconds >= 0 ? iCacheTtlSeconds : 0) {}

std::optional<GitEndpoint> GitRepoProbe::parseRemoteUrl(const std::string& sRemoteUrl) {
  if (sRemoteUrl.empty()) return std::nullopt;

  GitEndpoint ep;

  auto nScheme = sRemoteUrl.find("://");
  if (nScheme != std::string::npos) {
    ep.sScheme = sRemoteUrl.substr(0, nScheme);
    std::string sRest = sRemoteUrl.substr(nScheme + 3);

    if (ep.sScheme == "file") {
      ep.bLocal = true;
      ep.sLocalPath = sRest;
      return ep;
    }

    auto nSlash = sRest.find('/');
    std::string sAuthority = (nSlash == std::string::npos) ? sRest : sRest.substr(0, nSlash);
    sAuthority = stripUserInfo(sAuthority);

    if (ep.sScheme == "https") {
      ep.iPort = kDefaultHttpsPort;
    } else if (ep.sScheme == "http") {
      ep.iPort = kDefaultHttpPort;
    } else {
      // ssh://, git://, git+ssh:// — all reached over the SSH port by default.
      ep.iPort = kDefaultSshPort;
    }

    splitHostPort(sAuthority, ep.sHost, ep.iPort);
    if (ep.sHost.empty()) return std::nullopt;
    return ep;
  }

  // No scheme. Either scp-style (host:path) or a bare filesystem path.
  if (sRemoteUrl.front() == '/' || sRemoteUrl.front() == '.' || sRemoteUrl.front() == '~') {
    ep.bLocal = true;
    ep.sLocalPath = sRemoteUrl;
    return ep;
  }

  auto nColon = sRemoteUrl.find(':');
  if (nColon == std::string::npos) return std::nullopt;

  // scp-style never carries a port — everything after ':' is the path.
  ep.sScheme = "ssh";
  ep.iPort = kDefaultSshPort;
  ep.sHost = stripUserInfo(sRemoteUrl.substr(0, nColon));
  if (ep.sHost.empty()) return std::nullopt;
  return ep;
}

GitProbeResult GitRepoProbe::probeEndpoint(const std::string& sRemoteUrl, int iTimeoutMs) {
  GitProbeResult result;
  result.tpCheckedAt = std::chrono::system_clock::now();

  auto oEp = parseRemoteUrl(sRemoteUrl);
  if (!oEp) {
    result.bReachable = false;
    result.sError = "cannot parse git remote URL '" + sRemoteUrl + "'";
    return result;
  }

  if (oEp->bLocal) {
    std::error_code ec;
    if (std::filesystem::exists(oEp->sLocalPath, ec)) {
      result.bReachable = true;
    } else {
      result.bReachable = false;
      result.sError = "local repository path '" + oEp->sLocalPath + "' does not exist";
    }
    return result;
  }

  auto sError = tcpConnect(oEp->sHost, oEp->iPort, iTimeoutMs);
  result.bReachable = sError.empty();
  result.sError = sError;
  return result;
}

GitProbeResult GitRepoProbe::probe(int64_t iRepoId, const std::string& sRemoteUrl) {
  if (_iCacheTtlSeconds > 0) {
    std::lock_guard lock(_mtxCache);
    auto it = _mCache.find(iRepoId);
    if (it != _mCache.end()) {
      auto age = std::chrono::system_clock::now() - it->second.tpCheckedAt;
      if (age < std::chrono::seconds(_iCacheTtlSeconds)) return it->second;
    }
  }
  return probeNow(iRepoId, sRemoteUrl);
}

GitProbeResult GitRepoProbe::probeNow(int64_t iRepoId, const std::string& sRemoteUrl) {
  auto result = probeEndpoint(sRemoteUrl, _iTimeoutMs);

  if (!result.bReachable) {
    auto spLog = common::Logger::get();
    spLog->warn("GitRepoProbe: repo {} unreachable — {}", iRepoId, result.sError);
  }

  {
    std::lock_guard lock(_mtxCache);
    _mCache[iRepoId] = result;
  }
  return result;
}

void GitRepoProbe::invalidate(int64_t iRepoId) {
  std::lock_guard lock(_mtxCache);
  _mCache.erase(iRepoId);
}

}  // namespace dns::gitops
