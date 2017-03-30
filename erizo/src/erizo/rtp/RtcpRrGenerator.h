#ifndef ERIZO_SRC_ERIZO_RTP_RTCPRRGENERATOR_H_
#define ERIZO_SRC_ERIZO_RTP_RTCPRRGENERATOR_H_

#include <memory>
#include <string>
#include <random>
#include <map>

#include "logger.h"
#include "pipeline/Handler.h"
#include "lib/Clock.h"

#define MAX_DELAY 450000

namespace erizo {

class WebRtcConnection;

class RtcpRrGenerator {
  DECLARE_LOGGER();

 public:
  explicit RtcpRrGenerator(uint32_t ssrc, packetType type,
      std::shared_ptr<Clock> the_clock = std::make_shared<SteadyClock>());
  explicit RtcpRrGenerator(const RtcpRrGenerator&& handler);  // NOLINT

  bool handleRtpPacket(packetPtr packet);
  void handleSr(packetPtr packet);
  packetPtr generateReceiverReport();

 private:
  bool isRetransmitOfOldPacket(packetPtr packet);
  int getAudioClockRate(uint8_t payload_type);
  int getVideoClockRate(uint8_t payload_type);
  uint16_t selectInterval();
  uint16_t getRandomValue(uint16_t min, uint16_t max);

 private:
  class Jitter {
   public:
     Jitter() : transit_time(0), jitter(0) {}
     int transit_time;
     double jitter;
  };

  class RrPacketInfo {
   public:
     RrPacketInfo(){}
     explicit RrPacketInfo(uint32_t rr_ssrc) : ssrc{rr_ssrc}{}
     uint64_t last_sr_ts = 0;
     uint64_t next_packet_ms = 0;
     uint64_t last_packet_ms = 0;
     uint32_t ssrc = 0; 
     uint32_t last_sr_mid_ntp = 0; 
     uint32_t last_rr_ts = 0; 
     uint32_t last_rtp_ts = 0;
     uint32_t packets_received;
     uint32_t extended_seq = 0;
     uint32_t lost = 0;
     uint32_t expected_prior = 0;
     uint32_t received_prior = 0;
     uint32_t last_recv_ts = 0;

     int32_t max_seq = -1;
     int32_t base_seq = -1;  // are really uint16_t, we're using the sign for unitialized values
     uint16_t cycle = 0;
     uint8_t frac_lost = 0;
     Jitter jitter;
  };

  uint8_t packet_[128];
  bool enabled_, initialized_;
  RrPacketInfo rr_info_;
  uint32_t ssrc_;
  packetType type_;
  std::random_device random_device_;
  std::mt19937 random_generator_;
  std::shared_ptr<Clock> clock_;
};
}  // namespace erizo

#endif  // ERIZO_SRC_ERIZO_RTP_RTCPRRGENERATOR_H_
