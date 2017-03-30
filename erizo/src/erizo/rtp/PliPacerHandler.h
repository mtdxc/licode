#ifndef ERIZO_SRC_ERIZO_RTP_PLIPACERHANDLER_H_
#define ERIZO_SRC_ERIZO_RTP_PLIPACERHANDLER_H_

#include <string>

#include "logger.h"
#include "pipeline/Handler.h"
#include "lib/Clock.h"

namespace erizo {

class WebRtcConnection;
// FIR/PLI request and check handler
class PliPacerHandler: public Handler, public std::enable_shared_from_this<PliPacerHandler> {
  DECLARE_LOGGER();

 public:
  static constexpr duration kMinPLIPeriod = std::chrono::milliseconds(200);
  static constexpr duration kKeyframeTimeout = std::chrono::seconds(10);

 public:
  explicit PliPacerHandler(std::shared_ptr<erizo::Clock> the_clock = std::make_shared<SteadyClock>());

  void enable() override;
  void disable() override;

  std::string getName() override {
    return "pli-pacer";
  }

  void read(Context *ctx, packetPtr packet) override;
  void write(Context *ctx, packetPtr packet) override;
  void notifyUpdate() override;

 private:
  void scheduleNextPLI();
  void sendPLI();
  void sendFIR();

 private:
  bool enabled_;
  WebRtcConnection* connection_;
  std::shared_ptr<erizo::Clock> clock_;
  // keep tracker of last keyframe received time.
  time_point time_last_keyframe_;
  bool waiting_for_keyframe_ = false;

  // work schedule id for pli for unschedule
  int scheduled_pli_ = -1;
  // for construct fir packet
  uint32_t video_sink_ssrc_ = 0;
  uint32_t video_source_ssrc_ = 0;
  uint8_t fir_seq_number_ = 0;
};

}  // namespace erizo

#endif  // ERIZO_SRC_ERIZO_RTP_PLIPACERHANDLER_H_
