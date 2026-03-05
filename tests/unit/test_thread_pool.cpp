#include "core/ThreadPool.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

using dns::core::ThreadPool;

TEST(ThreadPoolTest, SubmitReturnsFuture) {
  ThreadPool tp(2);
  auto fut = tp.submit([]() { return 42; });
  EXPECT_EQ(fut.get(), 42);
}

TEST(ThreadPoolTest, MultipleTasks) {
  ThreadPool tp(4);
  std::vector<std::future<int>> vFuts;
  for (int i = 0; i < 20; ++i) {
    vFuts.push_back(tp.submit([i]() { return i * 2; }));
  }
  for (int i = 0; i < 20; ++i) {
    EXPECT_EQ(vFuts[i].get(), i * 2);
  }
}

TEST(ThreadPoolTest, ConcurrentExecution) {
  ThreadPool tp(4);
  std::atomic<int> iCounter{0};
  std::vector<std::future<void>> vFuts;
  for (int i = 0; i < 100; ++i) {
    vFuts.push_back(tp.submit([&iCounter]() { iCounter.fetch_add(1); }));
  }
  for (auto& f : vFuts) f.get();
  EXPECT_EQ(iCounter.load(), 100);
}

TEST(ThreadPoolTest, DefaultSizeUsesHardwareConcurrency) {
  ThreadPool tp(0);  // 0 = hardware_concurrency
  auto fut = tp.submit([]() { return 7; });
  EXPECT_EQ(fut.get(), 7);
}

TEST(ThreadPoolTest, ShutdownWaitsForPendingTasks) {
  ThreadPool tp(1);
  std::atomic<bool> bDone{false};
  tp.submit([&bDone]() {
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    bDone.store(true);
  });
  tp.shutdown();
  EXPECT_TRUE(bDone.load());
}

TEST(ThreadPoolTest, SubmitAfterShutdownThrows) {
  ThreadPool tp(2);
  tp.shutdown();
  EXPECT_THROW(tp.submit([]() { return 1; }), std::runtime_error);
}
