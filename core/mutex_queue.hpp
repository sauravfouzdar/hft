#pragma once

#include <cstddef>
#include <deque>
#include <mutex>

// core/mutex_queue -- the deliberately-obvious SPSC queue: a std::deque behind
// a std::mutex. This is the BASELINE TO BEAT (PROGRESS 1.2), not hot-path code.
//
// Why build the slow thing first? Every try_push/try_pop takes the lock even
// though there is exactly one producer and one consumer and never any real
// contention. That unconditional lock/unlock pair -- plus the cache-line
// ping-pong of the mutex state between cores -- is the cost we will measure
// here and then eliminate with a lock-free ring in 1.3, comparing the two in
// 1.4. Bounded with a try_* API so it matches the ring's interface exactly.
namespace hft::core {
template <typename T>
class MutexQueue {
  public:
  explicit MutexQueue(size_t capacity) : capacity_(capacity) {}

  bool try_push(const T& value){
    std::lock_guard<std::mutex> lock(mutex_);
    if (queue_.size() >= capacity_){
      return false;
    }
    queue_.push_back(value);
    return true;
  }

  bool try_pop(T& out){
    std::lock_guard<std::mutex> lock(mutex_);
    if (queue_.empty()){
      return false;
    }
    out = queue_.front();
    queue_.pop_front();
    return true;
  }

  private:
  std::deque<T> queue_;
  std::mutex mutex_;
  std::size_t capacity_;
};
}