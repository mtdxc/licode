#include "rtp/PacketBufferService.h"
#include "rtp/RtpHeaders.h"

namespace erizo {
DEFINE_LOGGER(PacketBufferService, "rtp.PacketBufferService");

PacketBufferService::PacketBufferService(): audio_{kServicePacketBufferSize},
  video_{kServicePacketBufferSize} {
}

void PacketBufferService::insertPacket(packetPtr packet) {
  RtpHeader *head = reinterpret_cast<RtpHeader*> (packet->data);
  switch (packet->type) {
    case VIDEO_PACKET:
      video_[getIndexInBuffer(head->getSeqNumber())] = packet;
      break;
    case AUDIO_PACKET:
      audio_[getIndexInBuffer(head->getSeqNumber())] = packet;
      break;
    default:
      ELOG_INFO("message: Trying to store an unknown packet");
      break;
  }
}

packetPtr PacketBufferService::getVideoPacket(uint16_t seq_num) {
  return video_[getIndexInBuffer(seq_num)];
}
packetPtr PacketBufferService::getAudioPacket(uint16_t seq_num) {
  return audio_[getIndexInBuffer(seq_num)];
}
}  // namespace erizo
