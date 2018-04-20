#ifndef ERIZO_SRC_ERIZO_RTP_RTPUTILS_H_
#define ERIZO_SRC_ERIZO_RTP_RTPUTILS_H_

#include "rtp/RtpHeaders.h"

#include "./MediaDefinitions.h"

#include <stdint.h>

#include <memory>

namespace erizo {

class RtpUtils {
 public:
  static bool sequenceNumberLessThan(uint16_t first, uint16_t second);

  static bool numberLessThan(uint16_t first, uint16_t last, int bits);

  static void forEachRtcpBlock(PacketPtr packet, std::function<void(RtcpHeader*)> f);

  static void updateREMB(RtcpHeader *chead, uint bitrate);

  static bool isPLI(PacketPtr packet);

  static bool isFIR(PacketPtr packet);

  static void forEachNack(RtcpHeader *chead, std::function<void(uint16_t, uint16_t, RtcpHeader*)> f);

  static PacketPtr createPLI(uint32_t source_ssrc, uint32_t sink_ssrc);

  static PacketPtr createFIR(uint32_t source_ssrc, uint32_t sink_ssrc, uint8_t seq_number);
  static PacketPtr createREMB(uint32_t ssrc, std::vector<uint32_t> ssrc_list, uint32_t bitrate);

  static int getPaddingLength(PacketPtr packet);

  static PacketPtr makePaddingPacket(PacketPtr packet, uint8_t padding_size);
};

}  // namespace erizo

#endif  // ERIZO_SRC_ERIZO_RTP_RTPUTILS_H_
