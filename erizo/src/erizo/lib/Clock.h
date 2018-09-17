#ifndef ERIZO_SRC_ERIZO_LIB_CLOCK_H_
#define ERIZO_SRC_ERIZO_LIB_CLOCK_H_

#include <chrono>  // NOLINT

namespace erizo {

using clock = std::chrono::steady_clock;
using time_point = std::chrono::steady_clock::time_point;
using duration = std::chrono::steady_clock::duration;

class Clock {
 public:
  virtual time_point now() = 0;
  int64_t msTime() { return std::chrono::duration_cast<std::chrono::milliseconds>(now().time_since_epoch()).count(); }
};

class SteadyClock : public Clock {
 public:
  time_point now() override {
    return clock::now();
  }
};

class SimulatedClock : public Clock {
 public:
  SimulatedClock() : now_{clock::now()} {}

  time_point now() override {
    return now_;
  }

  void advanceTime(duration duration) {
    now_ += duration;
  }
 private:
  time_point now_;
};

class ClockUtils {
public:
  static inline int64_t durationToMs(duration dur) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(dur).count();
  }

  static inline uint64_t timePointToMs(time_point tp) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(tp.time_since_epoch()).count();
  }
};
}  // namespace erizo
#endif  // ERIZO_SRC_ERIZO_LIB_CLOCK_H_
