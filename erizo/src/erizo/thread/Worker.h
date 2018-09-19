#ifndef ERIZO_SRC_ERIZO_THREAD_WORKER_H_
#define ERIZO_SRC_ERIZO_THREAD_WORKER_H_

#include <thread>
#include <chrono>	// NOLINT
#include <map>
#include <mutex>
#include <future>  // NOLINT
#include <vector>
#include <condition_variable>	// NOLINT
#include <atomic>
#include "../lib/clock.h"
// 提供定时执行任何Function功能. 
// Simple class for background tasks that should be run
// periodically or once "after a while"
namespace erizo {
class Worker {
public:
	explicit Worker(int thread_count);
	~Worker();

	typedef std::function<void(void)> Function;
	struct Task {
		int id; 
		duration delta;
		Function func;
	};

	virtual void start();
	virtual void start(std::shared_ptr<std::promise<void>> start_promise);
	virtual void close() { return stop(); }

	void task(Function f);
	int schedule(Function f, time_point t);
	int scheduleFromNow(Function f, duration delta_ms);
	int unschedule(int task_id);
	int scheduleEvery(Function f, duration delta_ms);

	// Tell any threads running serviceQueue to stop as soon as they're
	// done servicing whatever task they're currently servicing (drain=false)
	// or when there is no work left to be done (drain=true)
	void stop(bool drain = false);

private:
	void serviceQueue();

private:
	std::multimap<time_point, Task> task_queue_;
	std::condition_variable task_cond_;
	std::mutex task_mutex_;
	int cur_task_;
	std::atomic<int> thread_count_;
	bool stop_requested_;
	bool stop_when_empty_;
	std::vector<std::thread> threads_;
	std::thread thread_;
};
}
#endif	// ERIZO_SRC_ERIZO_THREAD_WORKER_H_
