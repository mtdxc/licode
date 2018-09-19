#include "rtp/PliPacerHandler.h"

#include "rtp/RtpUtils.h"
#include "./MediaDefinitions.h"
#include "./MediaStream.h"

namespace erizo {

DEFINE_LOGGER(PliPacerHandler, "rtp.PliPacerHandler");

constexpr duration PliPacerHandler::kMinPLIPeriod;
constexpr duration PliPacerHandler::kKeyframeTimeout;

PliPacerHandler::PliPacerHandler(std::shared_ptr<erizo::Clock> the_clock)
    : enabled_{true}, stream_{nullptr}, clock_{the_clock}, time_last_keyframe_{clock_->now()},
      waiting_for_keyframe_{false}, scheduled_pli_{0},
      video_sink_ssrc_{0}, video_source_ssrc_{0}, fir_seq_number_{0} {}

void PliPacerHandler::enable() {
  enabled_ = true;
}

void PliPacerHandler::disable() {
  enabled_ = false;
}

void PliPacerHandler::notifyUpdate() {
  auto pipeline = getContext()->getPipelineShared();
  if (pipeline && !stream_) {
    stream_ = pipeline->getService<MediaStream>().get();
    video_sink_ssrc_ = stream_->getVideoSinkSSRC();
    video_source_ssrc_ = stream_->getVideoSourceSSRC();
  }
}

void PliPacerHandler::read(Context *ctx, packetPtr packet) {
  if (enabled_ && packet->is_keyframe) {
    time_last_keyframe_ = clock_->now();
    waiting_for_keyframe_ = false;
    if (scheduled_pli_) {
      stream_->getWorker()->unschedule(scheduled_pli_);
      scheduled_pli_ = 0;
    }
  }
  ctx->fireRead(std::move(packet));
}

void PliPacerHandler::sendPLI() {
  getContext()->fireWrite(RtpUtils::createPLI(video_source_ssrc_, video_sink_ssrc_));
  scheduleNextPLI();
}

void PliPacerHandler::sendFIR() {
  stream_->Log("Timed out waiting for a keyframe");
  getContext()->fireWrite(RtpUtils::createFIR(video_source_ssrc_, video_sink_ssrc_, fir_seq_number_++));
  getContext()->fireWrite(RtpUtils::createFIR(video_source_ssrc_, video_sink_ssrc_, fir_seq_number_++));
  getContext()->fireWrite(RtpUtils::createFIR(video_source_ssrc_, video_sink_ssrc_, fir_seq_number_++));
  waiting_for_keyframe_ = false;
  scheduled_pli_ = 0;
}

void PliPacerHandler::scheduleNextPLI() {
  if (!waiting_for_keyframe_ || !enabled_) {
    return;
  }
  std::weak_ptr<PliPacerHandler> weak_this = shared_from_this();
  scheduled_pli_ = stream_->getWorker()->scheduleFromNow([weak_this] {
    if (auto this_ptr = weak_this.lock()) {
      if (this_ptr->clock_->now() - this_ptr->time_last_keyframe_ >= kKeyframeTimeout) {
        this_ptr->sendFIR();
        return;
      }
      this_ptr->sendPLI();
    }
  }, kMinPLIPeriod);
}

void PliPacerHandler::write(Context *ctx, packetPtr packet) {
  if (enabled_ && RtpUtils::isPLI(packet)) {
    if (waiting_for_keyframe_) {
      return;
    }
    waiting_for_keyframe_ = true;
    scheduleNextPLI();
  }
  ctx->fireWrite(std::move(packet));
}

}  // namespace erizo
