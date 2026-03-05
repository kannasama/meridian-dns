#include "api/RateLimiter.hpp"

namespace dns::api {

RateLimiter::RateLimiter(int iMaxRequests, std::chrono::steady_clock::duration durWindow)
    : _iMaxRequests(iMaxRequests), _durWindow(durWindow) {}

bool RateLimiter::allow(const std::string& sKey) {
  std::lock_guard<std::mutex> lock(_mtx);
  auto tpNow = std::chrono::steady_clock::now();
  auto it = _mBuckets.find(sKey);
  if (it == _mBuckets.end()) {
    _mBuckets[sKey] = Bucket{_iMaxRequests - 1, tpNow};
    if (_mBuckets.size() % 100 == 0) evictStale();
    return true;
  }
  auto& bucket = it->second;
  if (tpNow - bucket.tpLastRefill >= _durWindow) {
    bucket.iTokens = _iMaxRequests;
    bucket.tpLastRefill = tpNow;
  }
  if (bucket.iTokens > 0) { --bucket.iTokens; return true; }
  return false;
}

void RateLimiter::evictStale() {
  auto tpNow = std::chrono::steady_clock::now();
  for (auto it = _mBuckets.begin(); it != _mBuckets.end();)
    it = (tpNow - it->second.tpLastRefill > _durWindow * 2) ? _mBuckets.erase(it) : ++it;
}

}  // namespace dns::api
