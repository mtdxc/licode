#include "rtp/RtpUtils.h"
#include <string.h>
#include <memory>

namespace erizo {


constexpr int kMaxPacketSize = 1500;

bool RtpUtils::sequenceNumberLessThan(uint16_t first, uint16_t last) {
  uint16_t result = first - last;
  return result > 0xF000;
}

void RtpUtils::updateREMB(RtcpHeader *chead, uint64_t bitrate) {
  if (chead->packettype == RTCP_PS_Feedback_PT && chead->getBlockCount() == RTCP_AFB) {
    char *uniqueId = reinterpret_cast<char*>(&chead->report.rembPacket.uniqueid);
    if (!strncmp(uniqueId, "REMB", 4)) {
      chead->setREMBBitRate(bitrate);
    }
  }
}

void RtpUtils::forEachNack(RtcpHeader *chead, std::function<void(uint16_t, uint16_t, RtcpHeader*)> f) {
  if (chead->packettype == RTCP_RTP_Feedback_PT) {
    int length = chead->getPacketSize();
    int current_position = kNackCommonHeaderLengthBytes;
    uint8_t* aux_pointer = reinterpret_cast<uint8_t*>(chead);
    RtcpHeader* aux_chead;
    while (current_position < length) {
      aux_chead = reinterpret_cast<RtcpHeader*>(aux_pointer);
      uint16_t initial_seq_num = aux_chead->getNackPid();
      uint16_t plb = aux_chead->getNackBlp();
      f(initial_seq_num, plb, aux_chead);
      current_position += 4;
      aux_pointer += 4;
    }
  }
}

bool RtpUtils::isPLI(packetPtr packet) {
  bool is_pli = false;
  forEachRRBlock(packet, [&is_pli] (RtcpHeader *header) {
    if (header->getPacketType() == RTCP_PS_Feedback_PT &&
        header->getBlockCount() == RTCP_PLI_FMT) {
          is_pli = true;
        }
  });
  return is_pli;
}

bool RtpUtils::isFIR(packetPtr packet) {
  bool is_fir = false;
  forEachRRBlock(packet, [&is_fir] (RtcpHeader *header) {
    if (header->getPacketType() == RTCP_PS_Feedback_PT &&
        header->getBlockCount() == RTCP_FIR_FMT) {
          is_fir = true;
        }
  });
  return is_fir;
}

packetPtr RtpUtils::createPLI(uint32_t source_ssrc, uint32_t sink_ssrc) {
  RtcpHeader pli;
  pli.setPacketType(RTCP_PS_Feedback_PT);
  pli.setBlockCount(RTCP_PLI_FMT);
  pli.setSSRC(sink_ssrc);
  pli.setSourceSSRC(source_ssrc);
  pli.setLength(2);
  return std::make_shared<dataPacket>(0, reinterpret_cast<char*>(&pli), pli.getPacketSize(), VIDEO_PACKET);;
}

packetPtr RtpUtils::createFIR(uint32_t source_ssrc, uint32_t sink_ssrc, uint8_t seq_number) {
  RtcpHeader fir;
  fir.setPacketType(RTCP_PS_Feedback_PT);
  fir.setBlockCount(RTCP_FIR_FMT);
  fir.setSSRC(sink_ssrc);
  fir.setSourceSSRC(source_ssrc);
  fir.setLength(4);
  fir.setFIRSourceSSRC(source_ssrc);
  fir.setFIRSequenceNumber(seq_number);
  return std::make_shared<dataPacket>(0, reinterpret_cast<char*>(&fir), fir.getPacketSize(), VIDEO_PACKET);
}


int RtpUtils::getPaddingLength(packetPtr packet) {
  RtpHeader *rtp_header = reinterpret_cast<RtpHeader*>(packet->data);
  if (rtp_header->hasPadding()) {
    return packet->data[packet->length - 1] & 0xFF;
  }
  return 0;
}

void RtpUtils::forEachRRBlock(packetPtr packet, std::function<void(RtcpHeader*)> f) {
  RtcpHeader *chead = reinterpret_cast<RtcpHeader*>(packet->data);
  if (chead->isFeedback()) {
    RtcpAccessor acs(packet);
    while (chead = acs.nextRtcp()) {
      f(chead);
    }
  }
}

packetPtr RtpUtils::makePaddingPacket(packetPtr packet, uint8_t padding_size) {
  RtpHeader *header = reinterpret_cast<RtpHeader*>(packet->data);
  uint16_t packet_length = header->getHeaderLength() + padding_size;

  char packet_buffer[kMaxPacketSize];
  RtpHeader *new_header = reinterpret_cast<RtpHeader*>(packet_buffer);
  memset(packet_buffer, 0, packet_length);
  memcpy(packet_buffer, reinterpret_cast<char*>(header), header->getHeaderLength());

  new_header->setPadding(true);

  packet_buffer[packet_length - 1] = padding_size;

  return std::make_shared<dataPacket>(packet->comp, packet_buffer, packet_length, packet->type);
}

}  // namespace erizo
