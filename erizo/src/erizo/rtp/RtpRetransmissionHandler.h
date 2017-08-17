#ifndef ERIZO_SRC_ERIZO_RTP_RTPRETRANSMISSIONHANDLER_H_
#define ERIZO_SRC_ERIZO_RTP_RTPRETRANSMISSIONHANDLER_H_

#include <memory>
#include <vector>
#include "logger.h"
#include "pipeline/Handler.h"
#include "lib/Clock.h"
#include "lib/TokenBucket.h"

static constexpr uint32_t kRetransmissionsBufferSize = 256;
static constexpr int kNackBlpSize = 16;

static constexpr erizo::duration kTimeToUpdateBitrate = std::chrono::milliseconds(500);
static constexpr float kMarginRtxBitrate = 0.1f;
static constexpr int kBurstSize = 1300 * 20;  // 20 packets with almost max size

namespace erizo {
class Stats;
class WebRtcConnection;
class PacketBufferService;
class MovingIntervalRateStat;
// 负责对对端发起的FIR请求进行响应
class RtpRetransmissionHandler : public Handler {
 public:
  DECLARE_LOGGER();

  explicit RtpRetransmissionHandler(std::shared_ptr<erizo::Clock> the_clock = std::make_shared<erizo::SteadyClock>());

  void enable() override;
  void disable() override;

  std::string getName() override {
    return "retransmissions";
  }

  void read(Context *ctx, packetPtr packet) override;
  void write(Context *ctx, packetPtr packet) override;
  void notifyUpdate() override;

 private:
  MovingIntervalRateStat& getRtxBitrateStat();
  uint64_t getBitrateCalculated();
  void calculateRtxBitrate();
  // 为性能考虑,不测试系列号是否相同
 private:
  std::shared_ptr<erizo::Clock> clock_;
  WebRtcConnection *connection_;
  bool initialized_, enabled_;
  std::shared_ptr<Stats> stats_;
  std::shared_ptr<PacketBufferService> packet_buffer_;
  TokenBucket bucket_;
  erizo::time_point last_bitrate_time_;
};
}  // namespace erizo

#endif  // ERIZO_SRC_ERIZO_RTP_RTPRETRANSMISSIONHANDLER_H_
