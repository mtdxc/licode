#include "Worker.h"
#include <assert.h>
#include <utility>
namespace erizo{

Worker::Worker(int thread_count)
	: thread_count_(thread_count),
	stop_requested_(false), stop_when_empty_(false) {
	cur_task_ = 0;
	for (int index = 0; index < thread_count_; index++) {
		threads_.push_back(std::thread(&Worker::serviceQueue, this));
	}
}

Worker::~Worker() {
	stop(false);
	assert(thread_count_ == 0);
}

void Worker::serviceQueue() {
	std::unique_lock<std::mutex> lock(task_mutex_);
#if 1
	while (!stop_requested_) { // main loop modify by cai
		if (task_queue_.empty()) {
			if (stop_when_empty_)
				break;
			task_cond_.wait(lock);
			continue;
		}
		else {
			if (std::cv_status::timeout != task_cond_.wait_until(lock, task_queue_.begin()->first))
				continue;
		}
		if (!task_queue_.empty() && !stop_requested_) {
			Task t = task_queue_.begin()->second;
			task_queue_.erase(task_queue_.begin());
			if (t.delta && !stop_when_empty_) {
				task_queue_.insert(std::make_pair(clock::now() + std::chrono::milliseconds(t.delta), t));
			}

			lock.unlock();
			try { t.func(); }
			catch (...) {}
			lock.lock();
		}
	}
#else
	while (!stop_requested_ && !(stop_when_empty_ && task_queue_.empty())) {
		try {
			while (!stop_requested_ && !stop_when_empty_ && task_queue_.empty()) {
				task_cond_.wait(lock);
			}

			while (!stop_requested_ && !task_queue_.empty() &&
				task_cond_.wait_until(lock, task_queue_.begin()->first) != std::cv_status::timeout) {
			}
			if (stop_requested_) {
				break;
			}

			if (task_queue_.empty()) {
				continue;
			}

			Task t = task_queue_.begin()->second;
			task_queue_.erase(task_queue_.begin());
			if (t.delta && !stop_when_empty_) {
				task_queue_.insert(std::make_pair(std::chrono::system_clock::now() + std::chrono::milliseconds(t.delta), t));
			}

			lock.unlock();
			t.func();
			lock.lock();
		}
		catch (...) {
			--thread_count_;
			throw;
		}
	}
#endif
	--thread_count_;
}

void Worker::stop(bool drain) {
	{
		std::unique_lock<std::mutex> lock(task_mutex_);
		if (drain) {
			stop_when_empty_ = true;
		}
		else {
			stop_requested_ = true;
		}
	}
	task_cond_.notify_all();
	for (auto &t : threads_) {
		t.join();
	}
}

int Worker::unschedule(int task_id) {
	std::unique_lock<std::mutex> lock(task_mutex_);
	for (auto it = task_queue_.begin(); it != task_queue_.end(); it++) {
		if (it->second.id == task_id) {
			task_queue_.erase(it);
			return 1;
		}
	}
	return 0;
}

int Worker::schedule(Worker::Function f, time_point t) {
	int task_id = 0;
	{
		std::unique_lock<std::mutex> lock(task_mutex_);
		task_id = ++cur_task_;
		Task tk = { task_id, 0, f };
		// Pairs in this multimap are sorted by the Key value, so begin() will always point to the earlier task
		task_queue_.insert(std::make_pair(t, tk));
	}
	task_cond_.notify_one();
	return task_id;
}

void Worker::start() {
  auto promise = std::make_shared<std::promise<void>>();
  start(promise);
}

void Worker::start(std::shared_ptr<std::promise<void>> start_promise)
{
  start_promise->set_value();
}

void Worker::task(Worker::Function f) {
	schedule(f, std::chrono::steady_clock::now());
}

int Worker::scheduleFromNow(Worker::Function f, duration delta_ms) {
	return schedule(f, clock::now() + delta_ms);
}

int Worker::scheduleEvery(Worker::Function f, duration delta_ms) {
	int task_id = 0;
	{
		std::unique_lock<std::mutex> lock(task_mutex_);
		task_id = ++cur_task_;
		Task tk = { task_id, delta_ms.count(), f };
		// Pairs in this multimap are sorted by the Key value, so begin() will always point to the earlier task
		task_queue_.insert(std::make_pair(clock::now() + delta_ms, tk));
	}
	task_cond_.notify_one();
	return task_id;
}
}
