#include "rtp/BandwidthEstimationHandler.h"
#include "WebRtcConnection.h"
#include "Stats.h"
#include "lib/Clock.h"
#include "thread/Worker.h"
#include "webrtc/modules/remote_bitrate_estimator/remote_bitrate_estimator_abs_send_time.h"
#include "webrtc/modules/remote_bitrate_estimator/remote_bitrate_estimator_single_stream.h"

namespace erizo {

using webrtc::RemoteBitrateEstimatorSingleStream;
using webrtc::RemoteBitrateEstimatorAbsSendTime;

DEFINE_LOGGER(BandwidthEstimationHandler, "rtp.BandwidthEstimationHandler");

static const uint32_t kTimeOffsetSwitchThreshold = 30;
static const uint32_t kMinBitRateAllowed = 10;
const int kRembSendIntervallMs = 200;
const uint32_t BandwidthEstimationHandler::kRembMinimumBitrate = 20000;

// % threshold for if we should send a new REMB asap.
const unsigned int kSendThresholdPercent = 97;

std::unique_ptr<RemoteBitrateEstimator> RemoteBitrateEstimatorPicker::pickEstimator(
  bool using_absolute_send_time, webrtc::Clock* const clock, RemoteBitrateObserver *observer) {
  std::unique_ptr<RemoteBitrateEstimator> rbe;
  if (using_absolute_send_time) {
    rbe.reset(new webrtc::RemoteBitrateEstimatorAbsSendTime(observer, clock));
  } else {
    rbe.reset(new webrtc::RemoteBitrateEstimatorSingleStream(observer, clock));
  }
  return rbe;
}

BandwidthEstimationHandler::BandwidthEstimationHandler(std::shared_ptr<RemoteBitrateEstimatorPicker> picker) :
  connection_{nullptr}, clock_{webrtc::Clock::GetRealTimeClock()}, picker_{picker},
  using_absolute_send_time_{false}, packets_since_absolute_send_time_{0},
  min_bitrate_bps_{kMinBitRateAllowed},
  bitrate_{0}, last_send_bitrate_{0}, last_remb_time_{0},
  running_{false}, active_{true}, initialized_{false} {
}

void BandwidthEstimationHandler::enable() {
  active_ = true;
}

void BandwidthEstimationHandler::disable() {
  active_ = false;
}

void BandwidthEstimationHandler::notifyUpdate() {
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
  worker_ = connection_->getWorker();
  stats_ = pipeline->getService<Stats>();
  RtpExtensionProcessor& processor_ = connection_->getRtpExtensionProcessor();
  if (processor_.getVideoExtensionMap().size() == 0) {
    return;
  }
  // copy extend map from licode to webrtc
  updateExtensionMap(true, processor_.getVideoExtensionMap());
  updateExtensionMap(false, processor_.getAudioExtensionMap());
  pickEstimator();
  initialized_ = true;
}

void BandwidthEstimationHandler::process() {
  rbe_->Process();
  std::weak_ptr<BandwidthEstimationHandler> weak_ptr = shared_from_this();
  worker_->scheduleFromNow([weak_ptr]() {
    if (auto this_ptr = weak_ptr.lock()) {
      this_ptr->process();
    }
  }, std::chrono::milliseconds(rbe_->TimeUntilNextProcess()));
}

void BandwidthEstimationHandler::updateExtensionMap(bool is_video, std::array<RTPExtensions, 10> map) {
  webrtc::RTPExtensionType type;
  for (uint8_t id = 0; id < 10; id++) {
    RTPExtensions extension = map[id];
    switch (extension) {
      case RTP_ID:
      case UNKNOWN:
        continue;
        break;
      case SSRC_AUDIO_LEVEL:
        type = webrtc::kRtpExtensionAudioLevel;
        break;
      case ABS_SEND_TIME:
        type = webrtc::kRtpExtensionAbsoluteSendTime;
        break;
      case TOFFSET:
        type = webrtc::kRtpExtensionTransmissionTimeOffset;
        break;
      case VIDEO_ORIENTATION:
        type = webrtc::kRtpExtensionVideoRotation;
        break;
      case TRANSPORT_CC:
        type = webrtc::kRtpExtensionTransportSequenceNumber;
        break;
      case PLAYBACK_TIME:
        type = webrtc::kRtpExtensionPlayoutDelay;
        break;
    }
    if (is_video) {
      ext_map_video_.RegisterByType(id, type);
    } else {
      ext_map_audio_.RegisterByType(id, type);
    }
  }
}

void BandwidthEstimationHandler::read(Context *ctx, packetPtr packet) {
  if (initialized_ && !running_) {
    process();
    running_ = true;
  }
  RtcpHeader *chead = reinterpret_cast<RtcpHeader*> (packet->data);
  if (!chead->isRtcp() && packet->type == VIDEO_PACKET) {
    if (parsePacket(packet)) {
      int64_t arrival_time_ms = packet->received_time_ms;
      arrival_time_ms = clock_->TimeInMilliseconds() - (ClockUtils::msNow() - arrival_time_ms);
      pickEstimatorFromHeader();
      rbe_->IncomingPacket(arrival_time_ms, packet->length, header_);
    } else {
      ELOG_DEBUG("Packet not parsed %d", packet->type);
    }
  }
  ctx->fireRead(packet);
}

bool BandwidthEstimationHandler::parsePacket(packetPtr packet) {
  webrtc::RtpUtility::RtpHeaderParser rtp_parser((uint8_t*)packet->data, packet->length);
  memset(&header_, 0, sizeof(header_));
  return rtp_parser.Parse(&header_, getHeaderExtensionMap(packet));
}

RtpHeaderExtensionMap* BandwidthEstimationHandler::getHeaderExtensionMap(packetPtr packet) {
  switch (packet->type) {
    case VIDEO_PACKET:
      return &ext_map_video_;
      break;
    case AUDIO_PACKET:
      return &ext_map_audio_;
      break;
    default:
      ELOG_INFO("Won't process RTP extensions for unknown type packets");
      return NULL;
      break;
  }
}

void BandwidthEstimationHandler::write(Context *ctx, packetPtr packet) {
  ctx->fireWrite(packet);
}

void BandwidthEstimationHandler::pickEstimatorFromHeader() {
  if (header_.extension.hasAbsoluteSendTime) {
    if (!using_absolute_send_time_) {
      using_absolute_send_time_ = true;
      pickEstimator();
    }
    packets_since_absolute_send_time_ = 0;
  } else {
    if (using_absolute_send_time_) {
      ++packets_since_absolute_send_time_;
      if (packets_since_absolute_send_time_ >= kTimeOffsetSwitchThreshold) {
        using_absolute_send_time_ = false;
        pickEstimator();
      }
    }
  }
}

void BandwidthEstimationHandler::pickEstimator() {
  rbe_ = picker_->pickEstimator(using_absolute_send_time_, clock_, this);
  rbe_->SetMinBitrate(min_bitrate_bps_);
}

void BandwidthEstimationHandler::OnReceiveBitrateChanged(const std::vector<uint32_t>& ssrcs,
                                     uint32_t bitrate) {
  uint64_t now = ClockUtils::msNow();
  if (last_send_bitrate_ > 0) {
    unsigned int new_remb_bitrate = last_send_bitrate_ - bitrate_ + bitrate;
    if (new_remb_bitrate < kSendThresholdPercent * last_send_bitrate_ / 100) {
      // The new bitrate estimate is less than kSendThresholdPercent % of the
      // last report. Send a REMB asap.
      last_remb_time_ = now - kRembSendIntervallMs;
    }
  }

  if (bitrate < kRembMinimumBitrate) {
    bitrate = kRembMinimumBitrate;
  }

  bitrate_ = bitrate;

  if (now - last_remb_time_ < kRembSendIntervallMs) {
    return;
  }
  last_remb_time_ = now;
  last_send_bitrate_ = bitrate_;
  // update erizoBandwidth stats
  stats_->getNode()[connection_->getVideoSourceSSRC()].setStat<CumulativeStat>("erizoBandwidth", last_send_bitrate_);
  sendREMBPacket();
}

void BandwidthEstimationHandler::sendREMBPacket() {
  remb_packet_.setPacketType(RTCP_PS_Feedback_PT);
  remb_packet_.setBlockCount(RTCP_AFB);
  memcpy(&remb_packet_.report.rembPacket.uniqueid, "REMB", 4);

  remb_packet_.setSSRC(connection_->getVideoSinkSSRC());
  //  todo(pedro) figure out which sourceSSRC to use here
  remb_packet_.setSourceSSRC(connection_->getVideoSourceSSRC());
  remb_packet_.setLength(5);
  remb_packet_.setREMBBitRate(bitrate_);
  remb_packet_.setREMBNumSSRC(1);
  remb_packet_.setREMBFeedSSRC(connection_->getVideoSourceSSRC());
  if (active_) {
    ELOG_DEBUG("BWE Estimation is %d", last_send_bitrate_);
    getContext()->fireWrite(std::make_shared<dataPacket>(0,
      reinterpret_cast<char*>(&remb_packet_), remb_packet_.getPacketSize(), OTHER_PACKET));
  }
}
}  // namespace erizo
