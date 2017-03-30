#ifndef ERIZO_SRC_ERIZO_STATS_STATNODE_H_
#define ERIZO_SRC_ERIZO_STATS_STATNODE_H_

#include <string>
#include <map>
#include <vector>
#include <memory>

#include "lib/Clock.h"

namespace erizo {

class StatNode {
 public:
  typedef std::map<std::string, std::shared_ptr<StatNode>> NodeMap;
  StatNode() {}
  virtual ~StatNode() {}

  virtual StatNode& operator[](std::string key);
  virtual StatNode& operator[](uint64_t key) { return (*this)[std::to_string(key)]; }

  template <typename Node>
  void insertStat(std::string key, Node&& stat) {  // NOLINT
    // forward ensures that Node type is passed to make_shared(). It would otherwise pass StatNode.
    if (node_map_.find(key) != node_map_.end()) {
      node_map_.erase(key);
    }
    node_map_.insert(std::make_pair(key, std::make_shared<Node>(std::forward<Node>(stat))));
  }
  template <typename Node>
  void setStat(std::string key, uint64_t value) {  // NOLINT
    if (node_map_.find(key) != node_map_.end())
      (*(Node*)node_map_[key].get()) = value;
    else
      node_map_.insert(std::make_pair(key, std::make_shared<Node>(value)));
  }
  template <typename Node>
  void addStat(std::string key, uint64_t value) {  // NOLINT
    if (node_map_.find(key) != node_map_.end())
      node_map_[key]->add(value);
    else
      node_map_.insert(std::make_pair(key, std::make_shared<Node>(value)));
  }
  virtual bool hasChild(std::string name) { return node_map_.find(name) != node_map_.end(); }
  virtual bool hasChild(uint64_t value) { return hasChild(std::to_string(value)); }

  StatNode& operator+=(uint64_t value) { add(value); return *this; }
  StatNode& operator++() { add(1); return *this; }

  virtual void add(uint64_t value) {}
  virtual uint64_t value() { return 0; }
  // to json
  virtual std::string toString();

  virtual const NodeMap& getMap() { return node_map_; }
 private:
  bool is_node_;
  NodeMap node_map_;
};

class StringStat : public StatNode {
 public:
  StringStat() : text_{} {}
  explicit StringStat(std::string text) : text_{text} {}
  explicit StringStat(const StringStat &string_stat) : text_{string_stat.text_} {}
  virtual ~StringStat() {}

  StatNode& operator=(std::string text);
  
  uint64_t value() override { return 0; }

  std::string toString() override;
 private:
  std::string text_;
};

class CumulativeStat : public StatNode {
 public:
  CumulativeStat() : total_{0} {}
  explicit CumulativeStat(uint64_t initial) : total_{initial} {}
  virtual ~CumulativeStat() {}

  StatNode& operator=(uint64_t initial) {
    total_ = initial; return *this;
  }

  void add(uint64_t value) override { total_ += value; }
  uint64_t value() override { return total_; }
  std::string toString() override { return std::to_string(total_); }

 private:
  uint64_t total_;
};

class RateStat : public StatNode {
 public:
  RateStat(duration period, double scale,
      std::shared_ptr<Clock> the_clock = std::make_shared<SteadyClock>());
  ~RateStat() {}

  void add(uint64_t value) override;
  uint64_t value() override;
  std::string toString() override;

 private:
  // check every period_ and update 
  void checkPeriod();

 private:
  duration period_; // calc period
  double scale_; 
  time_point calculation_start_; // last calc time, reset to now after peroid
  uint64_t last_; // last value
  uint64_t total_; // count
  uint64_t current_period_total_; // last sum value, reset to 0 after period_
  uint64_t last_period_calculated_rate_; // scale_ * current_period_total_ * 1000 /(now - calculation_start_)
  std::shared_ptr<Clock> clock_;
};

class MovingIntervalRateStat : public StatNode {
 public:
  MovingIntervalRateStat(duration interval_size, uint32_t intervals, double scale,
                     std::shared_ptr<Clock> the_clock = std::make_shared<SteadyClock>());
  virtual ~MovingIntervalRateStat();

  void add(uint64_t value) override;
  uint64_t value() override;
  uint64_t value(duration stat_interval);
  
  std::string toString() override;

 private:
  uint64_t calculateRateForInterval(uint64_t interval_to_calculate_ms);
  // 根据时刻获取存储索引
  uint32_t getIntervalForTimeMs(uint64_t time_ms);
  // 获取下个存储索引
  uint32_t getNextInterval(uint32_t interval) {
    return (interval + 1) % intervals_in_window_;
  }
  void updateWindowTimes();

 private:
  int64_t interval_size_ms_; // 每个间隔多长ms
  uint32_t intervals_in_window_; // 窗口大小 sample_vector_ 存储的最大尺寸
  double scale_;
  uint64_t calculation_start_ms_ = 0; // initialized_ 的时刻 
  uint64_t current_interval_ = 0; // 当前索引
  // calculation_start_ms_ + accumulated_intervals_ * interval_size_ms_ = current_window_end_ms_
  uint64_t accumulated_intervals_ = 0;
  // 窗口起始和结束
  uint64_t current_window_start_ms_ = 0;
  uint64_t current_window_end_ms_ = 0;
  std::vector<uint64_t> sample_vector_;

  bool initialized_ = false;
  std::shared_ptr<Clock> clock_;
};
// same as MovingIntervalRateStat but less code
class IntervalRateStat : public StatNode {
public:
  IntervalRateStat(duration interval, uint32_t windows, double scale,
    std::shared_ptr<Clock> the_clock = std::make_shared<SteadyClock>());

  void add(uint64_t value) override;
  uint64_t value() override {
    return getRange(sample_vector_.size());
  }
  uint64_t value(duration stat_interval) {
    int range = ClockUtils::durationToMs(stat_interval) / interval_ms_;
    return getRange(range);
  }

  std::string toString() override;
protected:
  int64_t getNowInterval() {
    return ClockUtils::timePointToMs(clock_->now()) / interval_ms_;
  }
  uint64_t getRange(int elems);
  struct Item {
    int64_t inv_time = 0; // real interval of time
    int64_t value = 0; // count value
  };
  double scale_;
  int64_t interval_ms_; // 间隔ms
  std::vector<Item> sample_vector_;
  std::shared_ptr<Clock> clock_;
};

class MovingAverageStat : public StatNode {
 public:
  explicit MovingAverageStat(uint32_t window_size);
  virtual ~MovingAverageStat();

  void add(uint64_t value) override;
  uint64_t value() override;
  uint64_t value(uint32_t sample_number);

  std::string toString() override;

 private:

  double getAverage(uint32_t sample_number);

 private:
  // save window_size sample for getAverage
  std::vector<uint64_t> sample_vector_;
  uint32_t window_size_;
  // ring write position
  uint64_t next_sample_position_;
  // calc average in real time
  double current_average_;
};

}  // namespace erizo

#endif  // ERIZO_SRC_ERIZO_STATS_STATNODE_H_
