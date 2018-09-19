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
  RtcpHeader *chead = reinterpret_cast<RtcpHeader*>(packet->data);

  if (!initialized_) {
    ctx->fireRead(std::move(packet));
    return;
  }

  if (chead->getPacketType() == RTCP_Sender_PT) {
    uint32_t ssrc = chead->getSSRC();
    auto generator_it = generators_map_.find(ssrc);
    if (generator_it != generators_map_.end()) {
      generator_it->second->rr_generator->handleSr(packet);
    } else {
      ELOG_DEBUG("message: no RrGenerator found, ssrc: %u", ssrc);
    }
    ctx->fireRead(std::move(packet));
    return;
  }
  bool should_send_rr = false;
  bool should_send_nack = false;

  if (!chead->isRtcp()) {
    RtpHeader *head = reinterpret_cast<RtpHeader*>(packet->data);
    uint32_t ssrc = head->getSSRC();
    auto generator_it = generators_map_.find(ssrc);
    if (generator_it != generators_map_.end()) {
        should_send_rr = generator_it->second->rr_generator->handleRtpPacket(packet);
        if (nacks_enabled_) {
          should_send_nack = generator_it->second->nack_generator->handleRtpPacket(packet);
        }
    } else {
      ELOG_DEBUG("message: no Generator found, ssrc: %u", ssrc);
    }

    if (should_send_rr || should_send_nack) {
      ELOG_DEBUG("message: Should send Rtcp, ssrc %u", ssrc);
      packetPtr rtcp_packet = generator_it->second->rr_generator->generateReceiverReport();
      if (nacks_enabled_ && generator_it->second->nack_generator != nullptr) {
        generator_it->second->nack_generator->addNackPacketToRr(rtcp_packet);
      }
      ctx->fireWrite(std::move(rtcp_packet));
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
  std::for_each(video_ssrc_list.begin(), video_ssrc_list.end(), [this] (uint32_t video_ssrc) {
    if (video_ssrc != 0) {
      auto video_generator = std::make_shared<RtcpGeneratorPair>();
      generators_map_[video_ssrc] = video_generator;
      auto video_rr = std::make_shared<RtcpRrGenerator>(video_ssrc, VIDEO_PACKET, clock_);
      video_generator->rr_generator = video_rr;
      stream_->Log("Initialized video rrGenerator, ssrc: %u",  video_ssrc);
      if (nacks_enabled_) {
        stream_->Log("Initialized video nack generator, ssrc %u", video_ssrc);
        auto video_nack = std::make_shared<RtcpNackGenerator>(video_ssrc, clock_);
        video_generator->nack_generator = video_nack;
      }
    }
  });
  uint32_t audio_ssrc = stream_->getAudioSourceSSRC();
  if (audio_ssrc != 0) {
    auto audio_generator = std::make_shared<RtcpGeneratorPair>();
    generators_map_[audio_ssrc] = audio_generator;
    auto audio_rr = std::make_shared<RtcpRrGenerator>(audio_ssrc, AUDIO_PACKET, clock_);
    audio_generator->rr_generator = audio_rr;
    stream_->Log("Initialized audio, ssrc: %u", audio_ssrc);
  }
  initialized_ = true;
}

}  // namespace erizo
