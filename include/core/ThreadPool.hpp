#pragma once

#include <condition_variable>
#include <functional>
#include <future>
#include <mutex>
#include <queue>
#include <stdexcept>
#include <thread>
#include <vector>

namespace dns::core {

/// Fixed-size pool of std::jthread workers.
/// Class abbreviation: tp
class ThreadPool {
 public:
  explicit ThreadPool(int iSize = 0);
  ~ThreadPool();

  template <typename F, typename... Args>
  auto submit(F&& fnTask, Args&&... args) -> std::future<decltype(fnTask(args...))> {
    using ReturnType = decltype(fnTask(args...));
    auto spTask = std::make_shared<std::packaged_task<ReturnType()>>(
        std::bind(std::forward<F>(fnTask), std::forward<Args>(args)...));
    auto fut = spTask->get_future();
    {
      std::lock_guard lock(_mtx);
      if (_bStopping) throw std::runtime_error("ThreadPool is shutting down");
      _qTasks.emplace([spTask]() { (*spTask)(); });
    }
    _cv.notify_one();
    return fut;
  }

  void shutdown();

 private:
  std::vector<std::jthread> _vWorkers;
  std::queue<std::packaged_task<void()>> _qTasks;
  std::mutex _mtx;
  std::condition_variable _cv;
  bool _bStopping = false;
};

}  // namespace dns::core
