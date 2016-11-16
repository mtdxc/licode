#include "rtp/RtpAudioMuteHandler.h"
#include "MediaDefinitions.h"
#include "WebRtcConnection.h"

namespace erizo {

DEFINE_LOGGER(RtpAudioMuteHandler, "rtp.RtpAudioMuteHandler");

RtpAudioMuteHandler::RtpAudioMuteHandler() :
  last_original_seq_num_{-1}, seq_num_offset_{0}, mute_is_active_{false}, connection_{nullptr} {}


void RtpAudioMuteHandler::enable() {
}

void RtpAudioMuteHandler::disable() {
}

void RtpAudioMuteHandler::notifyUpdate() {
  auto pipeline = getContext()->getPipelineShared();
  if (pipeline && !connection_) {
    connection_ = pipeline->getService<WebRtcConnection>().get();
  }
  muteAudio(connection_->isAudioMuted());
}

void RtpAudioMuteHandler::read(Context *ctx, packetPtr packet) {
  RtcpHeader *chead = reinterpret_cast<RtcpHeader*>(packet->data);
  if (connection_->getAudioSinkSSRC() != chead->getSourceSSRC()) {
    ctx->fireRead(packet);
    return;
  }
  uint16_t offset = seq_num_offset_;
  if (offset > 0) {
    RtcpAccessor rtcp_acs(packet);
    while(chead = rtcp_acs.nextRtcp()) {//process seq offset of rtcp
      switch (chead->packettype) {
        case RTCP_Receiver_PT:
          if ((chead->getHighestSeqnum() + offset) < chead->getHighestSeqnum()) {
            // The seqNo adjustment causes a wraparound, add to cycles
            chead->setSeqnumCycles(chead->getSeqnumCycles() + 1);
          }
          chead->setHighestSeqnum(chead->getHighestSeqnum() + offset);

          break;
        case RTCP_RTP_Feedback_PT:
          chead->setNackPid(chead->getNackPid() + offset);
          break;
        default:
          break;
      }
    }
  }
  ctx->fireRead(packet);
}

void RtpAudioMuteHandler::write(Context *ctx, packetPtr packet) {
  RtpHeader *rtp_header = reinterpret_cast<RtpHeader*>(packet->data);
  RtcpHeader *rtcp_header = reinterpret_cast<RtcpHeader*>(packet->data);
  if (packet->type != AUDIO_PACKET || rtcp_header->isRtcp()) {
    ctx->fireWrite(packet);
    return;
  }
  bool is_muted;
  uint16_t offset;
  is_muted = mute_is_active_;
  offset = seq_num_offset_;
  last_original_seq_num_ = rtp_header->getSeqNumber();
  if (!is_muted) {
    last_sent_seq_num_ = last_original_seq_num_ - offset;
    if (offset > 0) {
      setPacketSeqNumber(packet, last_sent_seq_num_);
    }
    ctx->fireWrite(packet);
  }
}

void RtpAudioMuteHandler::muteAudio(bool active) {
  if (mute_is_active_ == active) {
    return;
  }
  mute_is_active_ = active;
  connection_->Info("Mute Audio, active: %d", active);
  if (!mute_is_active_) {
    seq_num_offset_ = last_original_seq_num_ - last_sent_seq_num_;
    connection_->Info("Deactivated, original_seq_num: %u, last_sent_seq_num: %u, offset: %u",
        last_original_seq_num_, last_sent_seq_num_, seq_num_offset_);
  }
}

inline void RtpAudioMuteHandler::setPacketSeqNumber(packetPtr packet, uint16_t seq_number) {
  RtpHeader *head = reinterpret_cast<RtpHeader*> (packet->data);
  RtcpHeader *chead = reinterpret_cast<RtcpHeader*> (packet->data);
  if (chead->isRtcp()) {
    return;
  }
  head->setSeqNumber(seq_number);
}

}  // namespace erizo
