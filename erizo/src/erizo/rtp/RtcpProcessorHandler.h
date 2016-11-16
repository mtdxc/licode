#ifndef ERIZO_SRC_ERIZO_RTP_RTCPPROCESSORHANDLER_H_
#define ERIZO_SRC_ERIZO_RTP_RTCPPROCESSORHANDLER_H_

#include <string>

#include "logger.h"
#include "pipeline/Handler.h"

namespace erizo {
class RtcpProcessor;
class Stats;
class WebRtcConnection;

class RtcpProcessorHandler: public Handler {
  DECLARE_LOGGER();

 public:
  RtcpProcessorHandler();

  void enable() override;
  void disable() override;

  std::string getName() override {
    return "rtcp-processor";
  }

  void read(Context *ctx, packetPtr packet) override;
  void write(Context *ctx, packetPtr packet) override;
  void notifyUpdate() override;

 private:
  WebRtcConnection* connection_;
  std::shared_ptr<RtcpProcessor> processor_;
  std::shared_ptr<Stats> stats_;
};

}  // namespace erizo

#endif  // ERIZO_SRC_ERIZO_RTP_RTCPPROCESSORHANDLER_H_
