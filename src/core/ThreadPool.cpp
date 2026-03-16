// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Meridian DNS Contributors
// This file is part of Meridian DNS. See LICENSE for details.

#include "core/ThreadPool.hpp"

#include <stdexcept>

namespace dns::core {

ThreadPool::ThreadPool(int iSize) {
  int iActual = (iSize <= 0) ? static_cast<int>(std::thread::hardware_concurrency()) : iSize;
  if (iActual <= 0) iActual = 4;  // fallback

  for (int i = 0; i < iActual; ++i) {
    _vWorkers.emplace_back([this](std::stop_token st) {
      while (true) {
        std::packaged_task<void()> task;
        {
          std::unique_lock lock(_mtx);
          _cv.wait(lock, [this, &st]() { return _bStopping || st.stop_requested() || !_qTasks.empty(); });
          if ((_bStopping || st.stop_requested()) && _qTasks.empty()) return;
          task = std::move(_qTasks.front());
          _qTasks.pop();
        }
        task();
      }
    });
  }
}

ThreadPool::~ThreadPool() {
  shutdown();
}

void ThreadPool::shutdown() {
  {
    std::lock_guard lock(_mtx);
    if (_bStopping) return;
    _bStopping = true;
  }
  _cv.notify_all();
  for (auto& w : _vWorkers) {
    if (w.joinable()) w.join();
  }
}

}  // namespace dns::core
