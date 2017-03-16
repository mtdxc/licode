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
  uint32_t getIntervalForTimeMs(uint64_t time_ms);
  uint32_t getNextInterval(uint32_t interval);
  void updateWindowTimes();

 private:
  int64_t interval_size_ms_; // 毫秒间隔时间
  uint32_t intervals_in_window_; // 窗口大小 sample_vector_的最大尺寸
  double scale_;
  uint64_t calculation_start_ms_; // initialized_ 的时刻 
  uint64_t current_interval_; // 当前索引
  uint64_t accumulated_intervals_;
  uint64_t current_window_start_ms_;
  uint64_t current_window_end_ms_;
  std::vector<uint64_t> sample_vector_;
  bool initialized_;
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
