#include "rtp/SRPacketHandler.h"
#include "WebRtcConnection.h"

namespace erizo {

DEFINE_LOGGER(SRPacketHandler, "rtp.SRPacketHandler");

SRPacketHandler::SRPacketHandler() :
    enabled_{true}, initialized_{false}, connection_(nullptr) {}


void SRPacketHandler::enable() {
  enabled_ = true;
}

void SRPacketHandler::disable() {
  enabled_ = false;
}

void SRPacketHandler::handleRtpPacket(packetPtr packet) {
  RtpHeader *head = reinterpret_cast<RtpHeader*>(packet->data);
  uint32_t ssrc = head->getSSRC();
  if (!sr_info_map_.count(ssrc)) {
    ELOG_DEBUG("message: Inserting new SSRC in sr_info_map, ssrc: %u", ssrc);
    sr_info_map_[ssrc] = std::make_shared<SRInfo>();
  }
  SRInfoPtr selected_info = sr_info_map_[ssrc];
  selected_info->sent_packets++;
  selected_info->sent_octets += (packet->length - head->getHeaderLength());
}



void SRPacketHandler::handleSR(packetPtr packet) {
  RtcpHeader *chead = reinterpret_cast<RtcpHeader*>(packet->data);
  uint32_t ssrc = chead->getSSRC();
  auto sr_selected_info_iter = sr_info_map_.find(ssrc);
  if (sr_selected_info_iter == sr_info_map_.end()) {
    ELOG_DEBUG("message: handleSR no info for this SSRC, ssrc: %u", ssrc);
    return;
  }
  SRInfoPtr selected_info = sr_selected_info_iter->second;
  ELOG_DEBUG("message: Rewriting SR, ssrc: %u, octets_sent_before: %u, packets_sent_before: %u"
    " octets_sent_after %u packets_sent_after: %u", ssrc, chead->getOctetsSent(), chead->getPacketsSent(),
    selected_info->sent_octets, selected_info->sent_packets);
  chead->setOctetsSent(selected_info->sent_octets);
  chead->setPacketsSent(selected_info->sent_packets);
}

void SRPacketHandler::write(Context *ctx, packetPtr packet) {
  if (initialized_ && enabled_) {
    RtcpHeader *chead = reinterpret_cast<RtcpHeader*>(packet->data);
    if (!chead->isRtcp()) {
      handleRtpPacket(packet);
    } else if (chead->packettype == RTCP_Sender_PT) {
      handleSR(packet);
    }
  }
  ctx->fireWrite(packet);
}

void SRPacketHandler::read(Context *ctx, packetPtr packet) {
  ctx->fireRead(packet);
}

void SRPacketHandler::notifyUpdate() {
  if (initialized_) {
    return;
  }
  auto pipeline = getContext()->getPipelineShared();
  if (pipeline && !connection_) {
    connection_ = pipeline->getService<WebRtcConnection>().get();
  }
  if (!connection_) {
    return;
  }
  initialized_ = true;
}

}  // namespace erizo
