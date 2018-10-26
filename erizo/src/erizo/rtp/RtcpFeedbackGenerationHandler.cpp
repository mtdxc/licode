#include "rtp/RtcpFeedbackGenerationHandler.h"
#include "./MediaStream.h"

namespace erizo {

DEFINE_LOGGER(RtcpFeedbackGenerationHandler, "rtp.RtcpFeedbackGenerationHandler");

RtcpFeedbackGenerationHandler::RtcpFeedbackGenerationHandler(bool nacks_enabled,
    std::shared_ptr<Clock> the_clock)
  : stream_{nullptr}, enabled_{true}, initialized_{false}, nacks_enabled_{nacks_enabled}, clock_{the_clock} {}

void RtcpFeedbackGenerationHandler::enable() {
  enabled_ = true;
}

void RtcpFeedbackGenerationHandler::disable() {
  enabled_ = false;
}

void RtcpFeedbackGenerationHandler::read(Context *ctx, packetPtr packet) {
  // Pass packets to RR and NACK Generator
  if (!initialized_) {
    ctx->fireRead(std::move(packet));
    return;
  }

  RtcpHeader *chead = reinterpret_cast<RtcpHeader*>(packet->data);
  if (!chead->isRtcp()) {
    RtpHeader *head = reinterpret_cast<RtpHeader*>(packet->data);
    uint32_t ssrc = head->getSSRC();
    if (!generators_map_.count(ssrc)) {
      ELOG_DEBUG("message: no Generator found, ssrc: %u", ssrc);
    }
    else {
      bool should_send_rr = false;
      bool should_send_nack = false;
      RtcpGeneratorPair& pair = generators_map_[ssrc];
      should_send_rr = pair.rr_generator->handleRtpPacket(packet);
      if (nacks_enabled_) {
        should_send_nack = pair.nack_generator->handleRtpPacket(packet);
      }

      if (should_send_rr || should_send_nack) {
        ELOG_DEBUG("message: Should send Rtcp, ssrc %u", ssrc);
        packetPtr rtcp_packet = pair.rr_generator->generateReceiverReport();
        if (nacks_enabled_ && pair.nack_generator) {
          pair.nack_generator->addNackPacketToRr(rtcp_packet);
        }
        ctx->fireWrite(std::move(rtcp_packet));
      }
    }
  }
  else if (chead->getPacketType() == RTCP_Sender_PT) {
    uint32_t ssrc = chead->getSSRC();
    if (generators_map_.count(ssrc)) {
      generators_map_[ssrc].rr_generator->handleSr(packet);
    }
    else {
      ELOG_DEBUG("message: no RrGenerator found, ssrc: %u", ssrc);
    }
  } 
  ctx->fireRead(std::move(packet));
}

void RtcpFeedbackGenerationHandler::write(Context *ctx, packetPtr packet) {
  ctx->fireWrite(std::move(packet));
}

void RtcpFeedbackGenerationHandler::notifyUpdate() {
  if (initialized_) {
    return;
  }

  auto pipeline = getContext()->getPipelineShared();
  if (!pipeline) {
    return;
  }

  stream_ = pipeline->getService<MediaStream>().get();
  if (!stream_) {
    return;
  }
  // TODO(pedro) detect if nacks are enabled here with the negotiated SDP scanning the rtp_mappings
  std::vector<uint32_t> video_ssrc_list = stream_->getVideoSourceSSRCList();
  for(uint32_t video_ssrc : video_ssrc_list) {
    if (!video_ssrc) 
      continue;
    generators_map_[video_ssrc].rr_generator = std::make_shared<RtcpRrGenerator>(video_ssrc, VIDEO_PACKET, clock_);
    stream_->Log("Initialized video rrGenerator, ssrc: %u",  video_ssrc);
    if (nacks_enabled_) {
      stream_->Log("Initialized video nack generator, ssrc %u", video_ssrc);
      generators_map_[video_ssrc].nack_generator = std::make_shared<RtcpNackGenerator>(video_ssrc, clock_);
    }
  }
  uint32_t audio_ssrc = stream_->getAudioSourceSSRC();
  if (audio_ssrc != 0) {
    generators_map_[audio_ssrc].rr_generator = std::make_shared<RtcpRrGenerator>(audio_ssrc, AUDIO_PACKET, clock_);
    stream_->Log("Initialized audio rrGenerator, ssrc: %u", audio_ssrc);
  }
  initialized_ = true;
}

}  // namespace erizo
