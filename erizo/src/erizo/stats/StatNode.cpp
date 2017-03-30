#include "stats/StatNode.h"

#include <cmath>
#include <sstream>
#include <string>
#include <memory>
#include <map>
#include <iostream>
#include <algorithm>

namespace erizo {

StatNode& StatNode::operator[](std::string key) {
  if (!hasChild(key)) {
    node_map_[key] = std::make_shared<StatNode>();
  }
  return *node_map_[key];
}

std::string StatNode::toString() {
  std::ostringstream text;
  text << "{";
  for (NodeMap::iterator node_iterator = node_map_.begin(); node_iterator != node_map_.end();) {
    text << "\"" << node_iterator->first << "\":" << node_iterator->second->toString();
    if (++node_iterator != node_map_.end()) {
      text << ",";
    }
  }
  text << "}";
  return text.str();
}

StatNode& StringStat::operator=(std::string text) {
  text_ = text;
  return *this;
}

std::string StringStat::toString() {
  std::ostringstream text;
  text << "\"" << text_ << "\"";
  return text.str();
}


RateStat::RateStat(duration period, double scale, std::shared_ptr<Clock> the_clock)
  : period_{period}, scale_{scale}, calculation_start_{the_clock->now()}, last_{0},
    total_{0}, current_period_total_{0}, last_period_calculated_rate_{0}, clock_{the_clock} {
}

void RateStat::add(uint64_t value) {
  current_period_total_ += value;
  last_ = value;
  total_++;
  checkPeriod();
}

uint64_t RateStat::value() {
  checkPeriod();
  return last_period_calculated_rate_;
}

std::string RateStat::toString() {
  return std::to_string(value());
}

void RateStat::checkPeriod() {
  time_point now = clock_->now();
  duration delay = now - calculation_start_;
  if (delay >= period_) {
    last_period_calculated_rate_ = scale_ * current_period_total_ * 1000. / ClockUtils::durationToMs(delay);
    current_period_total_ = 0;
    calculation_start_ = now;
  }
}

MovingIntervalRateStat::MovingIntervalRateStat(duration interval_size, uint32_t intervals, double scale,
  std::shared_ptr<Clock> the_clock): interval_size_ms_{ClockUtils::durationToMs(interval_size)},
  intervals_in_window_{intervals}, scale_{scale},
  sample_vector_(intervals, 0),
  initialized_{false}, clock_{the_clock} {
}

MovingIntervalRateStat::~MovingIntervalRateStat() {
}

void MovingIntervalRateStat::add(uint64_t value) {
  uint64_t now_ms = ClockUtils::timePointToMs(clock_->now());
  if (!initialized_) {
    calculation_start_ms_ = now_ms;
    initialized_ = true;
    accumulated_intervals_ = 1;
    current_window_start_ms_ = now_ms;
    current_window_end_ms_ = now_ms + accumulated_intervals_ * interval_size_ms_;
  }

  int32_t intervals_to_pass = (now_ms - current_window_end_ms_) / interval_size_ms_;
  if (intervals_to_pass > 0) {
    // 超过窗口一倍...
    if (static_cast<uint32_t>(intervals_to_pass) >= intervals_in_window_) {
      sample_vector_.assign(intervals_in_window_, 0);
      // get now index and set value
      current_interval_ = getIntervalForTimeMs(now_ms);
      sample_vector_[current_interval_]+= value;
      // reset accumulated_intervals_ to now and update new range
      accumulated_intervals_ += intervals_to_pass;
      updateWindowTimes();
      return;
    }
    // 否则把中间间隙填成0，并更新下次计算值
    for (int i = 0; i < intervals_to_pass; i++) {
      current_interval_ = getNextInterval(current_interval_);
      sample_vector_[current_interval_] = 0;
      accumulated_intervals_++;
    }
  }

  uint32_t corresponding_interval = getIntervalForTimeMs(now_ms);
  if (corresponding_interval != current_interval_) {
    sample_vector_[corresponding_interval] = 0;
    accumulated_intervals_++;
    updateWindowTimes();
    current_interval_ = corresponding_interval;
  }
  // 在当前间隔内累加值就行
  sample_vector_[current_interval_]+= value;
}

uint64_t MovingIntervalRateStat::value() {
  return calculateRateForInterval(intervals_in_window_ * interval_size_ms_);
}

uint64_t MovingIntervalRateStat::value(duration stat_interval) {
  return calculateRateForInterval(ClockUtils::durationToMs(stat_interval));
}

std::string MovingIntervalRateStat::toString() {
  return std::to_string(value());
}

uint64_t MovingIntervalRateStat::calculateRateForInterval(uint64_t interval_to_calculate_ms) {
  if (!initialized_) {
    return 0;
  }

  uint64_t now_ms = ClockUtils::msNow();
  uint64_t start_of_requested_interval = now_ms - interval_to_calculate_ms;
  uint64_t interval_start_time = std::max(start_of_requested_interval, current_window_start_ms_);
  uint32_t intervals_to_pass = (interval_start_time - current_window_start_ms_) / interval_size_ms_;
  //  We check if it's within the data we have
  if (intervals_to_pass >=  intervals_in_window_) {
    return 0;
  }

  int added_intervals = 0;
  uint64_t total_sum = 0;
  uint32_t moving_interval = getNextInterval(current_interval_ + intervals_to_pass);
  do {
    added_intervals++;
    total_sum += sample_vector_[moving_interval];
    moving_interval = getNextInterval(moving_interval);
  } while (moving_interval != current_interval_);

  double last_value_part_in_interval = (double)(now_ms - (current_window_end_ms_ - interval_size_ms_))
    /interval_size_ms_;
  double proportional_value = last_value_part_in_interval * sample_vector_[current_interval_];
  if (last_value_part_in_interval < 1) {
    total_sum += proportional_value;
  } else {
    total_sum += sample_vector_[current_interval_];
    last_value_part_in_interval = 0;
    added_intervals++;
  }
  double rate = static_cast<double> (total_sum) / (now_ms - interval_start_time);
  return (rate * 1000 * scale_);
}

uint32_t MovingIntervalRateStat::getIntervalForTimeMs(uint64_t time_ms) {
  return ((time_ms - calculation_start_ms_)/interval_size_ms_) % intervals_in_window_;
}

void MovingIntervalRateStat::updateWindowTimes() {
  current_window_end_ms_ = calculation_start_ms_ + accumulated_intervals_ * interval_size_ms_;
  current_window_start_ms_ = calculation_start_ms_ +
    (accumulated_intervals_ - std::min(accumulated_intervals_, static_cast<uint64_t>(intervals_in_window_)))
    * interval_size_ms_;
}

IntervalRateStat::IntervalRateStat(duration interval, uint32_t windows, double scale, std::shared_ptr<Clock> the_clock)
  :scale_(scale), interval_ms_(ClockUtils::durationToMs(interval)), sample_vector_(windows), clock_(the_clock) {
}

void IntervalRateStat::add(uint64_t value)
{
  int64_t now_interval = getNowInterval();
  int now_pos = now_interval % sample_vector_.size();
  Item& it = sample_vector_[now_pos];
  it.value += value;
  if (it.inv_time != now_interval)
    it.inv_time = now_interval;
}

std::string IntervalRateStat::toString()
{
  return std::to_string(value());
}

uint64_t IntervalRateStat::getRange(int range)
{
  int64_t intEnd = getNowInterval();
  int64_t intFirst = 0;
  uint64_t sum = 0;
  for (int64_t intStart = intEnd - range; intStart <= intEnd; intStart++)
  {
    int pos = intStart % sample_vector_.size();
    if (sample_vector_[pos].inv_time == intStart) {
      if (intFirst == 0) intFirst = intStart;
      sum += sample_vector_[pos].value;
    }
  }
  return sum * scale_ * 1000 / interval_ms_ / (intEnd - intFirst + 1);
}

MovingAverageStat::MovingAverageStat(uint32_t window_size)
  :sample_vector_(window_size, 0),
  window_size_{window_size}, next_sample_position_{0}, current_average_{0} {
}

MovingAverageStat::~MovingAverageStat() {
}

uint64_t MovingAverageStat::value() {
  return static_cast<uint64_t>(std::round(current_average_));
}

uint64_t MovingAverageStat::value(uint32_t sample_number) {
  if (!sample_number) return value();
  return static_cast<uint64_t>(getAverage(sample_number));
}

std::string MovingAverageStat::toString() {
  return std::to_string(value());
}

void MovingAverageStat::add(uint64_t value) {
  if (next_sample_position_ < window_size_) {
    current_average_  = static_cast<double>(current_average_ * next_sample_position_ + value)
      / (next_sample_position_ + 1);
  } else {
    uint64_t old_value = sample_vector_[next_sample_position_ % window_size_];
    if (value > old_value) {
      current_average_ = current_average_ + static_cast<double>(value - old_value) / window_size_;
    } else {
      current_average_ = current_average_ - static_cast<double>(old_value - value) / window_size_;
    }
  }

  sample_vector_[next_sample_position_ % window_size_] = value;
  next_sample_position_++;
}

double MovingAverageStat::getAverage(uint32_t sample_number) {
  uint64_t current_sample_position = next_sample_position_ - 1;
  //  We won't calculate an average for more than the window size
  sample_number = std::min(sample_number, window_size_);
  //  Check if we have enough samples
  sample_number = std::min(static_cast<uint64_t>(sample_number), current_sample_position);
  uint64_t calculated_sum = 0;
  for (uint32_t i = 0; i < sample_number;  i++) {
    calculated_sum += sample_vector_[(current_sample_position - i) % window_size_];
  }
  return static_cast<double>(calculated_sum)/sample_number;
}

}  // namespace erizo
