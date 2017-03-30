#include "rtp/RtcpFeedbackGenerationHandler.h"
#include "WebRtcConnection.h"

namespace erizo {

DEFINE_LOGGER(RtcpFeedbackGenerationHandler, "rtp.RtcpFeedbackGenerationHandler");

RtcpFeedbackGenerationHandler::RtcpFeedbackGenerationHandler(bool nacks_enabled,
    std::shared_ptr<Clock> the_clock)
  : connection_{nullptr}, enabled_{true}, initialized_{false}, nacks_enabled_{nacks_enabled}, clock_{the_clock} {}

void RtcpFeedbackGenerationHandler::enable() {
  enabled_ = true;
}

void RtcpFeedbackGenerationHandler::disable() {
  enabled_ = false;
}

void RtcpFeedbackGenerationHandler::read(Context *ctx, packetPtr packet) {
  // Pass packets to RR and NACK Generator
  RtcpHeader *chead = reinterpret_cast<RtcpHeader*>(packet->data);

  if (!initialized_) {
    ctx->fireRead(packet);
    return;
  }

  if (chead->getPacketType() == RTCP_Sender_PT) {
    uint32_t ssrc = chead->getSSRC();
    if (auto gen = getGenerator(ssrc)) {
      gen->rr_generator->handleSr(packet);
    } else {
      ELOG_DEBUG("message: no RrGenerator found, ssrc: %u", ssrc);
    }
    ctx->fireRead(packet);
    return;
  }
  bool should_send_rr = false;
  bool should_send_nack = false;

  if (!chead->isRtcp()) {
    RtpHeader *head = reinterpret_cast<RtpHeader*>(packet->data);
    uint32_t ssrc = head->getSSRC();
    auto gen = getGenerator(ssrc);
    if (gen) {
        should_send_rr = gen->rr_generator->handleRtpPacket(packet);
        if (nacks_enabled_) {
          should_send_nack = gen->nack_generator->handleRtpPacket(packet);
        }
    } else {
      ELOG_DEBUG("message: no Generator found, ssrc: %u", ssrc);
    }

    if (should_send_rr || should_send_nack) {
      ELOG_DEBUG("message: Should send Rtcp, ssrc %u", ssrc);
      packetPtr rtcp_packet = gen->rr_generator->generateReceiverReport();
      if (nacks_enabled_ && gen->nack_generator != nullptr) {
        gen->nack_generator->addNackPacketToRr(rtcp_packet);
      }
      ctx->fireWrite(rtcp_packet);
    }
  }
  ctx->fireRead(packet);
}

void RtcpFeedbackGenerationHandler::write(Context *ctx, packetPtr packet) {
  ctx->fireWrite(packet);
}

void RtcpFeedbackGenerationHandler::notifyUpdate() {
  if (initialized_) {
    return;
  }

  auto pipeline = getContext()->getPipelineShared();
  if (!pipeline) {
    return;
  }

  connection_ = pipeline->getService<WebRtcConnection>().get();
  if (!connection_) {
    return;
  }
  // TODO(pedro) detect if nacks are enabled here with the negotiated SDP scanning the rtp_mappings
  for (uint32_t video_ssrc : connection_->getVideoSourceSSRCList()) {
    if (!video_ssrc) continue;
    generators_map_[video_ssrc].rr_generator = std::make_shared<RtcpRrGenerator>(video_ssrc, VIDEO_PACKET, clock_);
    connection_->Info("Initialized video rrGenerator, ssrc: %u", video_ssrc);
    if (nacks_enabled_) {
      connection_->Info("Initialized video nack generator, ssrc %u", video_ssrc);
      generators_map_[video_ssrc].nack_generator = std::make_shared<RtcpNackGenerator>(video_ssrc, clock_);
    }
  }
  uint32_t audio_ssrc = connection_->getAudioSourceSSRC();
  if (audio_ssrc != 0) {
    generators_map_[audio_ssrc].rr_generator = std::make_shared<RtcpRrGenerator>(audio_ssrc, AUDIO_PACKET, clock_);
    connection_->Info("Initialized audio, ssrc: %u", audio_ssrc);
  }
  initialized_ = true;
}

}  // namespace erizo
