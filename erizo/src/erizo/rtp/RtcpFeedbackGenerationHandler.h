#ifndef ERIZO_SRC_ERIZO_RTP_RTCPFEEDBACKGENERATIONHANDLER_H_
#define ERIZO_SRC_ERIZO_RTP_RTCPFEEDBACKGENERATIONHANDLER_H_

#include <memory>
#include <string>
#include <map>

#include "logger.h"
#include "pipeline/Handler.h"
#include "rtp/RtcpRrGenerator.h"
#include "rtp/RtcpNackGenerator.h"
#include "lib/Clock.h"

#define MAX_DELAY 450000

namespace erizo {

class WebRtcConnection;

class RtcpGeneratorPair {
 public:
  std::shared_ptr<RtcpRrGenerator> rr_generator;
  std::shared_ptr<RtcpNackGenerator> nack_generator;
};


class RtcpFeedbackGenerationHandler: public Handler {
  DECLARE_LOGGER();

 public:
  explicit RtcpFeedbackGenerationHandler(bool nacks_enabled = true,
      std::shared_ptr<Clock> the_clock = std::make_shared<SteadyClock>());


  void enable() override;
  void disable() override;

  std::string getName() override {
     return "rtcp_feedback_generation";
  }

  void read(Context *ctx, packetPtr packet) override;
  void write(Context *ctx, packetPtr packet) override;
  void notifyUpdate() override;

 private:
  RtcpGeneratorPair* getGenerator(int ssrc) {
    if (generators_map_.count(ssrc))
      return &generators_map_[ssrc];
    return NULL;
  }
  WebRtcConnection *connection_;
  // ssrc -> RtcpGeneratorPair 放在一起要求音视频ssrc不能相同
  std::map<uint32_t, RtcpGeneratorPair> generators_map_;

  bool enabled_, initialized_;
  bool nacks_enabled_;
  std::shared_ptr<Clock> clock_;
};
}  // namespace erizo

#endif  // ERIZO_SRC_ERIZO_RTP_RTCPFEEDBACKGENERATIONHANDLER_H_
