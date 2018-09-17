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
  StatNode() {}
  virtual ~StatNode() {}

  template <typename Node>
  void insertStat(std::string key, Node&& stat) {  // NOLINT
    // forward ensures that Node type is passed to make_shared(). It would otherwise pass StatNode.
    if (node_map_.find(key) != node_map_.end()) {
      node_map_.erase(key);
    }
    node_map_.insert(std::make_pair(key, std::make_shared<Node>(std::forward<Node>(stat))));
  }

  virtual StatNode& operator[](std::string key);
  virtual StatNode& operator[](uint64_t key) { return (*this)[std::to_string(key)]; }

  virtual bool hasChild(std::string name) { return node_map_.find(name) != node_map_.end(); }
  virtual bool hasChild(uint64_t value) { return hasChild(std::to_string(value)); }

  virtual StatNode& operator+=(uint64_t value) { return *this; }
  virtual StatNode operator++(int value) { return StatNode{}; }
  virtual uint64_t value() { return 0; }

  typedef std::map<std::string, std::shared_ptr<StatNode>> StatMap;
  virtual const StatMap& getMap() {return node_map_;}

  virtual std::string toString();

 private:
  bool is_node_{false};
  StatMap node_map_;
};

class StringStat : public StatNode {
 public:
  StringStat() : text_{} {}
  explicit StringStat(std::string text) : text_{text} {}
  explicit StringStat(const StringStat &string_stat) : text_{string_stat.text_} {}
  virtual ~StringStat() {}

  std::string toString() override;
  StatNode& operator=(std::string text);

  StatNode operator++(int value) override { return StringStat{text_}; }

 private:
  std::string text_;
};

class CumulativeStat : public StatNode {
 public:
  CumulativeStat() : total_{0} {}
  explicit CumulativeStat(uint64_t initial) : total_{initial} {}
  virtual ~CumulativeStat() {}

  StatNode& operator=(uint64_t initial);

  StatNode operator++(int value) override;
  StatNode& operator+=(uint64_t value) override;

  std::string toString() override { return std::to_string(total_); }
  uint64_t value() override { return total_; }

 private:
  uint64_t total_;
};

class RateStat : public StatNode {
 public:
  RateStat(duration period, double scale,
                     std::shared_ptr<Clock> the_clock = std::make_shared<SteadyClock>());
  ~RateStat() {}

  StatNode operator++(int value) override;

  StatNode& operator+=(uint64_t value) override;

  uint64_t value() override;

  std::string toString() override;

 private:
  void add(uint64_t value);
  void checkPeriod();

 private:
  double scale_; ///< 放大倍数
  duration period_; ///< 统计周期
  time_point calculation_start_; ///< 开始时间
  uint64_t last_; ///< 最后值
  uint64_t total_; ///< 总采样数
  uint64_t current_period_total_; ///< 周期内的累加
  // 上周期速率: current_period_total_ * scale / period_
  uint64_t last_period_calculated_rate_; 
  std::shared_ptr<Clock> clock_;
};

class MovingIntervalRateStat : public StatNode {
 public:
  MovingIntervalRateStat(duration interval_size, uint32_t intervals, double scale,
                     std::shared_ptr<Clock> the_clock = std::make_shared<SteadyClock>());
  virtual ~MovingIntervalRateStat();

  StatNode operator++(int value) override;
  StatNode& operator+=(uint64_t value) override;

  uint64_t value() override;
  uint64_t value(duration stat_interval);

  std::string toString() override;

 private:
  void add(uint64_t value);
  uint64_t calculateRateForInterval(uint64_t interval_to_calculate_ms);
  uint32_t getIntervalForTimeMs(uint64_t time_ms);
  uint32_t getNextInterval(uint32_t interval);
  void updateWindowTimes();

 private:
  int64_t interval_size_ms_;
  uint32_t intervals_in_window_;
  double scale_;
  uint64_t calculation_start_ms_;
  uint64_t current_interval_;
  uint64_t accumulated_intervals_;
  uint64_t current_window_start_ms_;
  uint64_t current_window_end_ms_;
  std::vector<uint64_t> sample_vector_;
  bool initialized_;
  std::shared_ptr<Clock> clock_;
};

// 取window_size_个采样的平均值
class MovingAverageStat : public StatNode {
 public:
  explicit MovingAverageStat(uint32_t window_size);
  virtual ~MovingAverageStat();

  StatNode operator++(int value) override;
  StatNode& operator+=(uint64_t value) override;

  uint64_t value() override;
  uint64_t value(uint32_t sample_number);

  std::string toString() override;

 private:
  void add(uint64_t value);
  double getAverage(uint32_t sample_number);

 private:
  // 采样数组，大小为window_size_
  std::vector<uint64_t> sample_vector_;
  uint32_t window_size_;
  // 写位置, 每写一个+1
  uint64_t next_sample_position_;
  // 实时计算的平均值
  double current_average_;
};

}  // namespace erizo

#endif  // ERIZO_SRC_ERIZO_STATS_STATNODE_H_
