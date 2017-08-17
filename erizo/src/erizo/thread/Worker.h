#ifndef ERIZO_SRC_ERIZO_THREAD_WORKER_H_
#define ERIZO_SRC_ERIZO_THREAD_WORKER_H_

#include <boost/asio.hpp>
#include <boost/thread.hpp>

#include <algorithm>
#include <chrono> // NOLINT
#include <map>
#include <memory>
#include <future>  // NOLINT
#include <vector>

#include "lib/Clock.h"
#include "thread/Scheduler.h"

namespace erizo {
/*
任务类.
- 利用Scheduler来触发执行
- 实际任务在asio线程中执行.
提供如下功能：
- task
- scheduleFromNow/unschedule
- scheduleEvery 
正常Scheduler只有一个，但可能会为多个Worker服务. 建议使用Worker类而不是Scheduler
*/
class ScheduledTaskReference {
 public:
  ScheduledTaskReference();
  bool isCancelled();
  void cancel();
 private:
  std::atomic<bool> cancelled;
};

class Worker : public std::enable_shared_from_this<Worker> {
 public:
  typedef std::unique_ptr<boost::asio::io_service::work> asio_worker;
  typedef std::function<void()> Task;
  typedef std::function<bool()> ScheduledTask;

  explicit Worker(std::weak_ptr<Scheduler> scheduler,
                  std::shared_ptr<Clock> the_clock = std::make_shared<SteadyClock>());
  ~Worker();
  // 将任务Task投递到asio队列中执行
  virtual void task(Task f);

  virtual void start();
  virtual void start(std::shared_ptr<std::promise<void>> start_promise);
  virtual void close();

  virtual std::shared_ptr<ScheduledTaskReference> scheduleFromNow(Task f, duration delta);
  virtual void unschedule(std::shared_ptr<ScheduledTaskReference> id);
  // 周期性执行任务
  virtual void scheduleEvery(ScheduledTask f, duration period);
  boost::asio::io_service& io_service() {return service_;}
 private:
  void scheduleEvery(ScheduledTask f, duration period, duration next_delay);
  std::function<void()> safeTask(std::function<void(std::shared_ptr<Worker>)> f);

 protected:
  int next_scheduled_ = 0;

 private:
  std::weak_ptr<Scheduler> scheduler_;
  std::shared_ptr<Clock> clock_;
  boost::asio::io_service service_;
  asio_worker service_worker_;
  boost::thread_group group_;
  std::atomic<bool> closed_;
};

// 这类好像不大完善不建议使用..
class SimulatedWorker : public Worker {
 public:
  explicit SimulatedWorker(std::shared_ptr<SimulatedClock> the_clock);

  void task(Task f) override;
  void start() override;
  void start(std::shared_ptr<std::promise<void>> start_promise) override;
  void close() override;
  std::shared_ptr<ScheduledTaskReference> scheduleFromNow(Task f, duration delta) override;

  // 周期调用这两函数来执行Task
  void executeTasks();
  void executePastScheduledTasks();

 private:
  std::shared_ptr<SimulatedClock> clock_;
  std::vector<Task> tasks_;
  std::map<time_point, Task> scheduled_tasks_;
};
}  // namespace erizo

#endif  // ERIZO_SRC_ERIZO_THREAD_WORKER_H_
