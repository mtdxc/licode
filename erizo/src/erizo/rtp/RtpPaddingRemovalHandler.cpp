#include "rtp/RtpPaddingRemovalHandler.h"
#include "rtp/RtpUtils.h"

namespace erizo {

DEFINE_LOGGER(RtpPaddingRemovalHandler, "rtp.RtpPaddingRemovalHandler");

RtpPaddingRemovalHandler::RtpPaddingRemovalHandler()
  : enabled_{true}, initialized_{false} {}

void RtpPaddingRemovalHandler::enable() {
  enabled_ = true;
}

void RtpPaddingRemovalHandler::disable() {
  enabled_ = false;
}

void RtpPaddingRemovalHandler::read(Context *ctx, packetPtr packet) {
  if (enabled_ && isVideoRtp(packet)) {
    RtpHeader* rtp_header = (RtpHeader*)packet->data;
    uint32_t ssrc = rtp_header->getSSRC();
    std::shared_ptr<SequenceNumberTranslator> translator = getTranslatorForSsrc(ssrc, true);
    if (!removePaddingBytes(packet, translator)) {
      return;
    }
    uint16_t sequence_number = rtp_header->getSeqNumber();
    SequenceNumber sequence_number_info = translator->get(sequence_number, false);

    if (sequence_number_info.type != SequenceNumberType::Valid) {
      ELOG_DEBUG("Invalid translation %u, ssrc: %u", sequence_number, ssrc);
      return;
    }
    ELOG_DEBUG("Changing seq_number from %u to %u, ssrc %u", sequence_number, sequence_number_info.output,
     ssrc);
    rtp_header->setSeqNumber(sequence_number_info.output);
  }
  ctx->fireRead(packet);
}

void RtpPaddingRemovalHandler::write(Context *ctx, packetPtr packet) {
  RtcpHeader* rtcp_head = reinterpret_cast<RtcpHeader*>(packet->data);
  if (!enabled_ || packet->type != VIDEO_PACKET || !rtcp_head->isFeedback()) {
    ctx->fireWrite(packet);
    return;
  }
  uint32_t ssrc = rtcp_head->getSourceSSRC();
  std::shared_ptr<SequenceNumberTranslator> translator = getTranslatorForSsrc(ssrc, false);
  if (!translator) {
	connection_->Info("No translator for ssrc %u", ssrc);
    ctx->fireWrite(packet);
    return;
  }
  RtpUtils::forEachRRBlock(packet, [this, translator, ssrc](RtcpHeader *chead) {
    if (chead->packettype == RTCP_RTP_Feedback_PT) {
      RtpUtils::forEachNack(chead, [this, chead, translator, ssrc](uint16_t new_seq_num, uint16_t new_plb,
      RtcpHeader* nack_header) {
        uint16_t initial_seq_num = new_seq_num;
        std::vector<uint16_t> seq_nums;
        for (int i = -1; i <= 15; i++) {
          uint16_t seq_num = initial_seq_num + i + 1;
          SequenceNumber input_seq_num = translator->reverse(seq_num);
          if (input_seq_num.type == SequenceNumberType::Valid) {
            seq_nums.push_back(input_seq_num.input);
          } else {
			  connection_->Info("Input is not valid for %u, ssrc %u", seq_num, ssrc);
          }
          ELOG_DEBUG("Lost packet %u, input %u, ssrc %u", seq_num, input_seq_num.input, ssrc);
        }
        if (seq_nums.size() > 0) {
          uint16_t pid = seq_nums[0];
          uint16_t blp = 0;
          for (uint16_t index = 1; index < seq_nums.size() ; index++) {
            uint16_t distance = seq_nums[index] - pid - 1;
            blp |= (1 << distance);
          }
          nack_header->setNackPid(pid);
          nack_header->setNackBlp(blp);
		  connection_->Info("Translated pid %u, translated blp %u, ssrc %u", pid, blp, ssrc);
        }
      });
    }
  });
  ctx->fireWrite(packet);
}

bool RtpPaddingRemovalHandler::removePaddingBytes(std::shared_ptr<dataPacket> packet,
    std::shared_ptr<SequenceNumberTranslator> translator) {
  RtpHeader *rtp_header = reinterpret_cast<RtpHeader*>(packet->data);
  int padding_length = RtpUtils::getPaddingLength(packet);
  if (padding_length + rtp_header->getHeaderLength() == packet->length) 
  {// empty packet...
    uint16_t sequence_number = rtp_header->getSeqNumber();
    translator->get(sequence_number, true);
	connection_->Info("Dropping packet %u", sequence_number);
    return false;
  }
  // remove end and set padding bits
  packet->length -= padding_length;
  rtp_header->padding = 0;
  return true;
}

std::shared_ptr<SequenceNumberTranslator> RtpPaddingRemovalHandler::getTranslatorForSsrc(uint32_t ssrc,
  bool should_create) {
    auto translator_it = translator_map_.find(ssrc);
    std::shared_ptr<SequenceNumberTranslator> translator;
    if (translator_it != translator_map_.end()) {
      connection_->Info("Found Translator for %u", ssrc);
      translator = translator_it->second;
    } else if (should_create) {
      connection_->Info("message: no Translator found creating a new one, ssrc: %u", ssrc);
      translator = std::make_shared<SequenceNumberTranslator>();
      translator_map_[ssrc] = translator;
    }
    return translator;
  }

void RtpPaddingRemovalHandler::notifyUpdate() {
  auto pipeline = getContext()->getPipelineShared();
  if (!pipeline) {
    return;
  }
  if (initialized_) {
    return;
  }
  connection_ = pipeline->getService<WebRtcConnection>().get();
  initialized_ = true;
}
}  // namespace erizo
