#ifndef ERIZO_SRC_ERIZO_RTP_RTPUTILS_H_
#define ERIZO_SRC_ERIZO_RTP_RTPUTILS_H_

#include "rtp/RtpHeaders.h"
#include "MediaDefinitions.h"
#include <stdint.h>
#include <memory>

namespace erizo {

class RtpUtils {
 public:
  static bool sequenceNumberLessThan(uint16_t first, uint16_t second);

  static void forEachRRBlock(packetPtr packet, std::function<void(RtcpHeader*)> f);

  static void updateREMB(RtcpHeader *chead, uint64_t bitrate);

  static bool isPLI(packetPtr packet);

  static bool isFIR(packetPtr packet);

  static void forEachNack(RtcpHeader *chead, std::function<void(uint16_t, uint16_t, RtcpHeader*)> f);

  static packetPtr createPLI(uint32_t source_ssrc, uint32_t sink_ssrc);

  static packetPtr createFIR(uint32_t source_ssrc, uint32_t sink_ssrc, uint8_t seq_number);

  static int getPaddingLength(packetPtr packet);

  static packetPtr makePaddingPacket(packetPtr packet, uint8_t padding_size);
};

}  // namespace erizo

#endif  // ERIZO_SRC_ERIZO_RTP_RTPUTILS_H_
