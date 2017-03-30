#ifndef ERIZO_SRC_ERIZO_RTP_SRPACKETHANDLER_H_
#define ERIZO_SRC_ERIZO_RTP_SRPACKETHANDLER_H_

#include <memory>
#include <string>

#include "logger.h"
#include "pipeline/Handler.h"

#define MAX_DELAY 450000

namespace erizo {

class WebRtcConnection;

class SRPacketHandler: public Handler {
  DECLARE_LOGGER();


 public:
  SRPacketHandler();

  void enable() override;
  void disable() override;

  std::string getName() override {
     return "sr_handler";
  }

  void read(Context *ctx, packetPtr packet) override;
  void write(Context *ctx, packetPtr packet) override;
  void notifyUpdate() override;

 private:
  struct SRInfo {
    SRInfo() : ssrc{0}, sent_octets{0}, sent_packets{0} {}
    uint32_t ssrc;
    uint32_t sent_octets;
    uint32_t sent_packets;
  };
  typedef std::shared_ptr<SRInfo> SRInfoPtr;
  bool enabled_, initialized_;
  WebRtcConnection* connection_;
  std::map<uint32_t, SRInfoPtr> sr_info_map_;

  void handleRtpPacket(packetPtr packet);
  void handleSR(packetPtr packet);
};
}  // namespace erizo

#endif  // ERIZO_SRC_ERIZO_RTP_SRPACKETHANDLER_H_
