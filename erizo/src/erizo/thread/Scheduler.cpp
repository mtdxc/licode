#include "thread/Scheduler.h"
#include <assert.h>
#include <utility>


Scheduler::Scheduler(int thread_count)
: thread_count_(thread_count),
  stop_requested_(false), stop_when_empty_(false) {
  for (int index = 0; index < thread_count_; index++) {
    group_.create_thread(std::bind(&Scheduler::serviceQueue, this));
  }
}

Scheduler::~Scheduler() {
  stop(false);
  assert(thread_count_ == 0);
}

void Scheduler::serviceQueue() {
  std::unique_lock<std::mutex> lock(new_task_mutex_);
#if 1
  while (!stop_requested_) { // main loop modify by cai
    if (task_queue_.empty()) {
      if(stop_when_empty_)
        break;
      new_task_scheduled_.wait(lock);
      continue;
    }
    else {
      if(std::cv_status::timeout != new_task_scheduled_.wait_until(lock, task_queue_.begin()->first))
        continue;
    }
    if (!task_queue_.empty() && !stop_requested_) {
      Function f = task_queue_.begin()->second;
      task_queue_.erase(task_queue_.begin());

      lock.unlock();
      try { f(); }
      catch (...) {}
      lock.lock();
    }
  }
#else
  while (!stop_requested_ && !(stop_when_empty_ && task_queue_.empty())) {
    try {
      while (!stop_requested_ && !stop_when_empty_ && task_queue_.empty()) {
        new_task_scheduled_.wait(lock);
      }

      while (!stop_requested_ && !task_queue_.empty() &&
             new_task_scheduled_.wait_until(lock, task_queue_.begin()->first) != std::cv_status::timeout) {
      }
      if (stop_requested_) {
        break;
      }

      if (task_queue_.empty()) {
        continue;
      }

      Function f = task_queue_.begin()->second;
      task_queue_.erase(task_queue_.begin());

      lock.unlock();
      f();
      lock.lock();
    } catch (...) {
      --thread_count_;
      throw;
    }
  }
#endif
  --thread_count_;
}

void Scheduler::stop(bool drain) {
  {
    std::unique_lock<std::mutex> lock(new_task_mutex_);
    if (drain) {
      stop_when_empty_ = true;
    } else {
      stop_requested_ = true;
    }
  }
  new_task_scheduled_.notify_all();
  group_.join_all();
}

void Scheduler::schedule(Scheduler::Function f, std::chrono::system_clock::time_point t) {
  {
    std::unique_lock<std::mutex> lock(new_task_mutex_);
    // Pairs in this multimap are sorted by the Key value, so begin() will always point to the
    // earlier task
    task_queue_.insert(std::make_pair(t, f));
  }
  new_task_scheduled_.notify_one();
}

void Scheduler::scheduleFromNow(Scheduler::Function f, std::chrono::milliseconds delta_ms) {
  schedule(f, std::chrono::system_clock::now() + delta_ms);
}

// TODO(javier): Make it possible to unschedule repeated tasks before enable this code
// static void Repeat(Scheduler* s, Scheduler::Function f, std::chrono::milliseconds delta_ms) {
//   f();
//   s->scheduleFromNow(std::bind(&Repeat, s, f, delta_ms), delta_ms);
// }

// void Scheduler::scheduleEvery(Scheduler::Function f, std::chrono::milliseconds delta_ms) {
//   scheduleFromNow(std::bind(&Repeat, this, f, delta_ms), delta_ms);
// }
