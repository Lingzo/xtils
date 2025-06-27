#pragma once

#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <utility>
namespace xtils {

template <typename T>
class ThreadSafe {
 public:
  using value_type = typename T::value_type;
  ~ThreadSafe() {
    quit_ = true;
    cv_.notify_all();
  }
  bool pop_wait(value_type& e) {
    std::unique_lock<std::mutex> lock(mtx_);
    cv_.wait(lock, [&]() { return !data_.empty() | quit_; });
    if (quit_) return false;
    e = data_.front();
    data_.pop_front();
    return true;
  }
  bool try_pop(value_type& e) {
    std::lock_guard<std::mutex> lock(mtx_);
    if (data_.empty()) return false;
    e = data_.front();
    data_.pop_front();
    return true;
  }
  void push(const value_type& e) {
    std::lock_guard<std::mutex> lock(mtx_);
    data_.push_back(e);
    cv_.notify_all();
  }

  void push(value_type&& e) {
    std::lock_guard<std::mutex> lock(mtx_);
    data_.push_back(std::forward(e));
    cv_.notify_all();
  }

  void clear() {
    std::lock_guard<std::mutex> lock(mtx_);
    data_.clear();
    cv_.notify_all();
  }

  std::size_t size() {
    std::lock_guard<std::mutex> lock(mtx_);
    return data_.size();
  }

  void quit() {
    std::lock_guard<std::mutex> lock(mtx_);
    cv_.notify_all();
    quit_ = true;
  }

 private:
  T data_;
  bool quit_{false};
  std::condition_variable cv_;
  std::mutex mtx_;
};

}  // namespace xtils
