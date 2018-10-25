
/*
 * MediaStream.cpp
 */

#include <cstdio>
#include <map>
#include <algorithm>
#include <string>
#include <cstring>
#include <vector>
#include <cstdlib>
#include <ctime>

#include "./MediaStream.h"
#include "./SdpInfo.h"
#include"./WebRtcConnection.h"
#include "rtp/RtpHeaders.h"
#include "rtp/RtpVP8Parser.h"
#include "rtp/RtcpAggregator.h"
#include "rtp/RtcpForwarder.h"
#include "rtp/RtpSlideShowHandler.h"
#include "rtp/RtpTrackMuteHandler.h"
#include "rtp/BandwidthEstimationHandler.h"
#include "rtp/FecReceiverHandler.h"
#include "rtp/RtcpProcessorHandler.h"
#include "rtp/RtpRetransmissionHandler.h"
#include "rtp/RtcpFeedbackGenerationHandler.h"
#include "rtp/RtpPaddingRemovalHandler.h"
#include "rtp/StatsHandler.h"
#include "rtp/SRPacketHandler.h"
#include "rtp/SenderBandwidthEstimationHandler.h"
#include "rtp/LayerDetectorHandler.h"
#include "rtp/LayerBitrateCalculationHandler.h"
#include "rtp/QualityFilterHandler.h"
#include "rtp/QualityManager.h"
#include "rtp/PliPacerHandler.h"
#include "rtp/RtpPaddingGeneratorHandler.h"
#include "rtp/RtpUtils.h"
#include "rtp/PacketCodecParser.h"

namespace erizo {
DEFINE_LOGGER(MediaStream, "MediaStream");
log4cxx::LoggerPtr statsLogger = log4cxx::Logger::getLogger("StreamStats");

static constexpr auto kStreamStatsPeriod = std::chrono::seconds(30);

MediaStream::MediaStream(std::shared_ptr<Worker> worker,
  std::shared_ptr<WebRtcConnection> connection,
  const std::string& stream_id,
  const std::string& stream_label,
  bool is_publisher) :
    connection_{std::move(connection)},
    stream_id_{stream_id},
    mslabel_ {stream_label},
    pipeline_{Pipeline::create()},
    worker_{std::move(worker)},
    is_publisher_{is_publisher}{
  audio_enabled_ = video_enabled_ = audio_muted_ = video_muted_ = false;
  bundle_ = false; pipeline_initialized_ = false;  slide_show_mode_ = false;
  set_log_context("%cs.%s", is_publisher_ ? 'P' : 'S', stream_id_.c_str());
  Log("constructor");
  
  source_fb_sink_ = this;
  sink_fb_source_ = this;

  // use random value
  //setVideoSinkSSRC(kDefaultVideoSinkSSRC);
  //setAudioSinkSSRC(kDefaultAudioSinkSSRC);
  std::srand(std::time(nullptr));
  audio_sink_ssrc_ = std::rand();
  video_sink_ssrc_ = std::rand();

  stats_ = std::make_shared<Stats>();
  log_stats_ = std::make_shared<Stats>();
  quality_manager_ = std::make_shared<QualityManager>();
  packet_buffer_ = std::make_shared<PacketBufferService>();
  rtcp_processor_ = std::make_shared<RtcpForwarder>(static_cast<MediaSink*>(this), static_cast<MediaSource*>(this));

  should_send_feedback_ = true;
  mark_ = clock::now();
  rate_control_ = 0;
  sending_ = true;
}

MediaStream::~MediaStream() {
  Log("Destructor");
}

uint32_t MediaStream::getMaxVideoBW() {
  uint32_t bitrate = 0;
  if(rtcp_processor_) 
    bitrate = rtcp_processor_->getMaxVideoBW();
  return bitrate;
}

void MediaStream::setMaxVideoBW(uint32_t max_video_bw) {
  asyncTask([=]{
    if (rtcp_processor_) {
      Log("setMaxVideoBW %u", max_video_bw);
      rtcp_processor_->setMaxVideoBW(max_video_bw * 1000);
      if (pipeline_) {
          pipeline_->notifyUpdate();
      }
    }
  });
}

void MediaStream::syncClose() {
  Log("Close called");
  if (!sending_) {
    Log("skip close without sending_");
    return;
  }
  sending_ = false;
  video_sink_ = nullptr;
  audio_sink_ = nullptr;
  fb_sink_ = nullptr;
  pipeline_initialized_ = false;
  pipeline_->close();
  pipeline_.reset();
  connection_.reset();
  Log("Close ended");
}

void MediaStream::close() {
  Log("Async close called");
  asyncTask([this]() {
    syncClose();
  });
}

bool MediaStream::init() {
  return true;
}

bool MediaStream::isSourceSSRC(uint32_t ssrc) {
  return isVideoSourceSSRC(ssrc) || isAudioSourceSSRC(ssrc);
}

bool MediaStream::isSinkSSRC(uint32_t ssrc) {
  return isVideoSinkSSRC(ssrc) || isAudioSinkSSRC(ssrc);
}

bool MediaStream::setRemoteSdp(std::shared_ptr<SdpInfo> sdp) {
  Log("setting remote SDP");
  if (!sending_) {
    return true;
  }
  remote_sdp_ = std::make_shared<SdpInfo>(*sdp.get());
  if (remote_sdp_->videoBandwidth != 0) {
    Log("Setting remote BW, maxVideoBW: %u", remote_sdp_->videoBandwidth);
    rtcp_processor_->setMaxVideoBW(remote_sdp_->videoBandwidth*1000);
  }

  if (pipeline_initialized_ && pipeline_) {
    pipeline_->notifyUpdate();
    return true;
  }

  bundle_ = remote_sdp_->isBundle;
  auto video_ssrc_list_it = remote_sdp_->video_ssrc_map.find(getLabel());
  if (video_ssrc_list_it != remote_sdp_->video_ssrc_map.end()) {
    setVideoSourceSSRCList(video_ssrc_list_it->second);
  }

  auto audio_ssrc_it = remote_sdp_->audio_ssrc_map.find(getLabel());
  if (audio_ssrc_it != remote_sdp_->audio_ssrc_map.end()) {
    setAudioSourceSSRC(audio_ssrc_it->second);
  }

  std::vector<uint32_t> video_ssrc = getVideoSourceSSRCList();
  if (video_ssrc.empty() || (video_ssrc.size() == 1 && video_ssrc[0] == 0)) {
	video_ssrc.push_back(kDefaultVideoSinkSSRC);
    setVideoSourceSSRCList(video_ssrc);
  }

  if (getAudioSourceSSRC() == 0) {
    setAudioSourceSSRC(kDefaultAudioSinkSSRC);
  }

  audio_enabled_ = remote_sdp_->hasAudio;
  video_enabled_ = remote_sdp_->hasVideo;

  rtcp_processor_->addSourceSsrc(getAudioSourceSSRC());
  for(uint32_t ssrc: video_source_ssrc_list_){
      rtcp_processor_->addSourceSsrc(ssrc);
  };

  initializePipeline();
  initializeStats();
  return true;
}

void MediaStream::initializeStats() {
  log_stats_->getNode().insertStat("streamId", StringStat{getId()});
  log_stats_->getNode().insertStat("audioBitrate", CumulativeStat{0});
  log_stats_->getNode().insertStat("audioFL", CumulativeStat{0});
  log_stats_->getNode().insertStat("audioPL", CumulativeStat{0});
  log_stats_->getNode().insertStat("audioJitter", CumulativeStat{0});
  log_stats_->getNode().insertStat("audioMuted", CumulativeStat{0});
  log_stats_->getNode().insertStat("audioNack", CumulativeStat{0});
  log_stats_->getNode().insertStat("audioRemb", CumulativeStat{0});

  log_stats_->getNode().insertStat("videoBitrate", CumulativeStat{0});
  log_stats_->getNode().insertStat("videoFL", CumulativeStat{0});
  log_stats_->getNode().insertStat("videoPL", CumulativeStat{0});
  log_stats_->getNode().insertStat("videoJitter", CumulativeStat{0});
  log_stats_->getNode().insertStat("videoMuted", CumulativeStat{0});
  log_stats_->getNode().insertStat("slideshow", CumulativeStat{0});
  log_stats_->getNode().insertStat("videoNack", CumulativeStat{0});
  log_stats_->getNode().insertStat("videoPli", CumulativeStat{0});
  log_stats_->getNode().insertStat("videoFir", CumulativeStat{0});
  log_stats_->getNode().insertStat("videoRemb", CumulativeStat{0});
  log_stats_->getNode().insertStat("videoErizoRemb", CumulativeStat{0});
  log_stats_->getNode().insertStat("videoKeyFrames", CumulativeStat{0});

  log_stats_->getNode().insertStat("SL0TL0", CumulativeStat{0});
  log_stats_->getNode().insertStat("SL0TL1", CumulativeStat{0});
  log_stats_->getNode().insertStat("SL0TL2", CumulativeStat{0});
  log_stats_->getNode().insertStat("SL0TL3", CumulativeStat{0});
  log_stats_->getNode().insertStat("SL1TL0", CumulativeStat{0});
  log_stats_->getNode().insertStat("SL1TL1", CumulativeStat{0});
  log_stats_->getNode().insertStat("SL1TL2", CumulativeStat{0});
  log_stats_->getNode().insertStat("SL1TL3", CumulativeStat{0});
  log_stats_->getNode().insertStat("SL2TL0", CumulativeStat{0});
  log_stats_->getNode().insertStat("SL2TL1", CumulativeStat{0});
  log_stats_->getNode().insertStat("SL2TL2", CumulativeStat{0});
  log_stats_->getNode().insertStat("SL2TL3", CumulativeStat{0});
  log_stats_->getNode().insertStat("SL3TL0", CumulativeStat{0});
  log_stats_->getNode().insertStat("SL3TL1", CumulativeStat{0});
  log_stats_->getNode().insertStat("SL3TL2", CumulativeStat{0});
  log_stats_->getNode().insertStat("SL3TL3", CumulativeStat{0});

  log_stats_->getNode().insertStat("maxActiveSL", CumulativeStat{0});
  log_stats_->getNode().insertStat("maxActiveTL", CumulativeStat{0});
  log_stats_->getNode().insertStat("selectedSL", CumulativeStat{0});
  log_stats_->getNode().insertStat("selectedTL", CumulativeStat{0});
  log_stats_->getNode().insertStat("isPublisher", CumulativeStat{is_publisher_});

  log_stats_->getNode().insertStat("totalBitrate", CumulativeStat{0});
  log_stats_->getNode().insertStat("rtxBitrate", CumulativeStat{0});
  log_stats_->getNode().insertStat("paddingBitrate", CumulativeStat{0});
  log_stats_->getNode().insertStat("bwe", CumulativeStat{0});

  log_stats_->getNode().insertStat("maxVideoBW", CumulativeStat{0});

  std::weak_ptr<MediaStream> weak_this = shared_from_this();
  worker_->scheduleEvery([weak_this] () {
    if (auto stream = weak_this.lock()) {
      if (stream->sending_) {
        stream->printStats();
        return true;
      }
    }
    return false;
  }, kStreamStatsPeriod);
}

void MediaStream::transferLayerStats(std::string spatial, std::string temporal) {
  if (stats_->getNode().hasChild("qualityLayers") &&
      stats_->getNode()["qualityLayers"].hasChild(spatial) &&
      stats_->getNode()["qualityLayers"][spatial].hasChild(temporal)) {
    std::string node = "SL" + spatial + "TL" + temporal;
    log_stats_->getNode().insertStat(node, CumulativeStat{stats_->getNode()["qualityLayers"][spatial][temporal].value()});
  }
}

void MediaStream::transferMediaStats(std::string target_node, std::string source_parent, std::string source_node) {
  if (stats_->getNode().hasChild(source_parent) &&
      stats_->getNode()[source_parent].hasChild(source_node)) {
    log_stats_->getNode().insertStat(target_node, CumulativeStat{stats_->getNode()[source_parent][source_node].value()});
  }
}

void MediaStream::printStats() {
  std::string video_ssrc;
  std::string audio_ssrc;

  log_stats_->getNode().insertStat("audioEnabled", CumulativeStat{audio_enabled_});
  log_stats_->getNode().insertStat("videoEnabled", CumulativeStat{video_enabled_});

  log_stats_->getNode().insertStat("maxVideoBW", CumulativeStat{getMaxVideoBW()});

  if (audio_enabled_) {
    audio_ssrc = std::to_string(is_publisher_ ? getAudioSourceSSRC() : getAudioSinkSSRC());
    transferMediaStats("audioBitrate", audio_ssrc, "bitrateCalculated");
    transferMediaStats("audioPL",      audio_ssrc, "packetsLost");
    transferMediaStats("audioFL",      audio_ssrc, "fractionLost");
    transferMediaStats("audioJitter",  audio_ssrc, "jitter");
    transferMediaStats("audioMuted",   audio_ssrc, "erizoAudioMute");
    transferMediaStats("audioNack",    audio_ssrc, "NACK");
    transferMediaStats("audioRemb",    audio_ssrc, "bandwidth");
  }
  if (video_enabled_) {
    video_ssrc = std::to_string(is_publisher_ ? getVideoSourceSSRC() : getVideoSinkSSRC());
    transferMediaStats("videoBitrate", video_ssrc, "bitrateCalculated");
    transferMediaStats("videoPL",      video_ssrc, "packetsLost");
    transferMediaStats("videoFL",      video_ssrc, "fractionLost");
    transferMediaStats("videoJitter",  video_ssrc, "jitter");
    transferMediaStats("videoMuted",   audio_ssrc, "erizoVideoMute");
    transferMediaStats("slideshow",    video_ssrc, "erizoSlideShow");
    transferMediaStats("videoNack",    video_ssrc, "NACK");
    transferMediaStats("videoPli",     video_ssrc, "PLI");
    transferMediaStats("videoFir",     video_ssrc, "FIR");
    transferMediaStats("videoRemb",    video_ssrc, "bandwidth");
    transferMediaStats("videoErizoRemb", video_ssrc, "erizoBandwidth");
    transferMediaStats("videoKeyFrames", video_ssrc, "keyFrames");
  }

  for (uint32_t spatial = 0; spatial <= 3; spatial++) {
    for (uint32_t temporal = 0; temporal <= 3; temporal++) {
      transferLayerStats(std::to_string(spatial), std::to_string(temporal));
    }
  }

  transferMediaStats("maxActiveSL", "qualityLayers", "maxActiveSpatialLayer");
  transferMediaStats("maxActiveTL", "qualityLayers", "maxActiveTemporalLayer");
  transferMediaStats("selectedSL", "qualityLayers", "selectedSpatialLayer");
  transferMediaStats("selectedTL", "qualityLayers", "selectedTemporalLayer");
  transferMediaStats("totalBitrate", "total", "bitrateCalculated");
  transferMediaStats("paddingBitrate", "total", "paddingBitrate");
  transferMediaStats("rtxBitrate", "total", "rtxBitrate");
  transferMediaStats("bwe", "total", "senderBitrateEstimation");

  ELOG_INFO(statsLogger, "%s", log_stats_->getStats());
}

void MediaStream::initializePipeline() {
  handler_manager_ = std::make_shared<HandlerManager>(shared_from_this());
  pipeline_->addService(shared_from_this());
  pipeline_->addService(handler_manager_);
  pipeline_->addService(rtcp_processor_);
  pipeline_->addService(stats_);
  pipeline_->addService(quality_manager_);
  pipeline_->addService(packet_buffer_);

  pipeline_->addFront(std::make_shared<PacketReader>(this));

  pipeline_->addFront(std::make_shared<RtcpProcessorHandler>());
  pipeline_->addFront(std::make_shared<FecReceiverHandler>());
  pipeline_->addFront(std::make_shared<LayerBitrateCalculationHandler>());
  pipeline_->addFront(std::make_shared<QualityFilterHandler>());
  pipeline_->addFront(std::make_shared<IncomingStatsHandler>());
  pipeline_->addFront(std::make_shared<RtpTrackMuteHandler>());
  pipeline_->addFront(std::make_shared<RtpSlideShowHandler>());
  pipeline_->addFront(std::make_shared<RtpPaddingGeneratorHandler>());
  pipeline_->addFront(std::make_shared<PliPacerHandler>());
  pipeline_->addFront(std::make_shared<BandwidthEstimationHandler>());
  pipeline_->addFront(std::make_shared<RtpPaddingRemovalHandler>());
  pipeline_->addFront(std::make_shared<RtcpFeedbackGenerationHandler>());
  pipeline_->addFront(std::make_shared<RtpRetransmissionHandler>());
  pipeline_->addFront(std::make_shared<SRPacketHandler>());
  pipeline_->addFront(std::make_shared<SenderBandwidthEstimationHandler>());
  pipeline_->addFront(std::make_shared<LayerDetectorHandler>());
  pipeline_->addFront(std::make_shared<OutgoingStatsHandler>());
  pipeline_->addFront(std::make_shared<PacketCodecParser>());

  pipeline_->addFront(std::make_shared<PacketWriter>(this));
  pipeline_->finalize();
  pipeline_initialized_ = true;
}

int MediaStream::deliverAudioData_(packetPtr audio_packet) {
  if (audio_enabled_) {
    sendPacketAsync(std::make_shared<DataPacket>(*audio_packet));
  }
  return audio_packet->length;
}

int MediaStream::deliverVideoData_(packetPtr video_packet) {
  if (video_enabled_) {
    sendPacketAsync(std::make_shared<DataPacket>(*video_packet));
  }
  return video_packet->length;
}

int MediaStream::deliverFeedback_(packetPtr fb_packet) {
  RtcpHeader *chead = reinterpret_cast<RtcpHeader*>(fb_packet->data);
  uint32_t recvSSRC = chead->getSourceSSRC();
  if (chead->isREMB()) {
    for (uint8_t index = 0; index < chead->getREMBNumSSRC(); index++) {
      uint32_t ssrc = chead->getREMBFeedSSRC(index);
      if (isVideoSourceSSRC(ssrc)) {
        recvSSRC = ssrc;
        break;
      }
    }
  }
  if (isVideoSourceSSRC(recvSSRC)) {
    fb_packet->type = VIDEO_PACKET;
    sendPacketAsync(fb_packet);
  } else if (isAudioSourceSSRC(recvSSRC)) {
    fb_packet->type = AUDIO_PACKET;
    sendPacketAsync(fb_packet);
  } else {
    Log("deliverFeedback unknownSSRC: %u, localVideoSSRC: %u, localAudioSSRC: %u",
         recvSSRC, getVideoSourceSSRC(), getAudioSourceSSRC());
  }
  return fb_packet->length;
}

int MediaStream::deliverEvent_(MediaEventPtr event) {
  asyncTask([=]{
    if (!pipeline_initialized_) {
      return;
    }

    if (pipeline_) {
      pipeline_->notifyEvent(event);
    }
  });
  return 1;
}

void MediaStream::onTransportData(packetPtr incoming_packet, Transport *transport) {
  if ((!audio_sink_ && !video_sink_ && !fb_sink_)) {
    return;
  }

  packetPtr packet = std::make_shared<DataPacket>(*incoming_packet);
  if (transport->media_type() == AUDIO_TYPE) {
    packet->type = AUDIO_PACKET;
  } else if (transport->media_type() == VIDEO_TYPE) {
    packet->type = VIDEO_PACKET;
  }
  if (!packet->isRtcp()) {
    uint32_t recvSSRC = packet->rtp()->getSSRC();
    if (isVideoSourceSSRC(recvSSRC)) {
      packet->type = VIDEO_PACKET;
    }
    else if (isAudioSourceSSRC(recvSSRC)) {
      packet->type = AUDIO_PACKET;
    }
  }

  asyncTask([=]{
    if (!pipeline_initialized_) {
		  Log("Pipeline not initialized yet.");
      return;
    }

    if (pipeline_) {
      pipeline_->read(std::move(packet));
    }
  });
}

void MediaStream::read(packetPtr packet) {
  // PROCESS RTCP
  RtcpHeader *chead = reinterpret_cast<RtcpHeader*>(packet->data);
  uint32_t recvSSRC = 0;
  if (!chead->isRtcp()) {
    recvSSRC = packet->rtp()->getSSRC();
  } else if (chead->packettype == RTCP_Sender_PT || chead->packettype == RTCP_SDES_PT) {  // Sender Report
    recvSSRC = packet->rtcp()->getSSRC();
  }
  // DELIVER FEEDBACK (RR, FEEDBACK PACKETS)
  if (chead->isFeedback()) {
    if (fb_sink_ != nullptr && should_send_feedback_) {
      fb_sink_->deliverFeedback(std::move(packet));
    }
  } else {
    // RTP or RTCP Sender Report
    if (bundle_) {
      // Check incoming SSRC
      // Deliver data
      if (isVideoSourceSSRC(recvSSRC) && video_sink_) {
        parseIncomingPayloadType(packet, VIDEO_PACKET);
        video_sink_->deliverVideoData(std::move(packet));
      } else if (isAudioSourceSSRC(recvSSRC) && audio_sink_) {
        parseIncomingPayloadType(packet, AUDIO_PACKET);
        audio_sink_->deliverAudioData(std::move(packet));
      } else {
        Log("read video unknownSSRC: %u, localVideoSSRC: %u, localAudioSSRC: %u", recvSSRC, getVideoSourceSSRC(), getAudioSourceSSRC());
      }
    } else {
      if (packet->type == AUDIO_PACKET && audio_sink_) {
        parseIncomingPayloadType(packet, AUDIO_PACKET);
        // Firefox does not send SSRC in SDP
        if (getAudioSourceSSRC() == 0) {
          Log("discoveredAudioSourceSSRC:%u", recvSSRC);
          setAudioSourceSSRC(recvSSRC);
        }
        audio_sink_->deliverAudioData(std::move(packet));
      } else if (packet->type == VIDEO_PACKET && video_sink_) {
        parseIncomingPayloadType(packet, VIDEO_PACKET);
        // Firefox does not send SSRC in SDP
        if (getVideoSourceSSRC() == 0) {
          Log("discoveredVideoSourceSSRC:%u", recvSSRC);
          setVideoSourceSSRC(recvSSRC);
        }
        // change ssrc for RTP packets, don't touch here if RTCP
        video_sink_->deliverVideoData(std::move(packet));
      }
    }  // if not bundle
  }  // if not Feedback
}

void MediaStream::setMediaStreamEventListener(MediaStreamEventListener* listener) {
  AutoLock lock(event_mutex_);
  event_listener_ = listener;
}

void MediaStream::notifyMediaStreamEvent(const std::string& type, const std::string& message) {
  AutoLock lock(event_mutex_);
  if (event_listener_) {
    event_listener_->notifyMediaStreamEvent(type, message);
  }
}

void MediaStream::notifyToEventSink(MediaEventPtr event) {
  if(event_sink_)
    event_sink_->deliverEvent(std::move(event));
}

int MediaStream::sendPLI() {
  auto pkt = RtpUtils::createPLI(getVideoSourceSSRC(), getVideoSinkSSRC());
  sendPacketAsync(pkt);
  return pkt->length;
}

void MediaStream::sendPLIToFeedback() {
  if (fb_sink_) {
    fb_sink_->deliverFeedback(RtpUtils::createPLI(getVideoSinkSSRC(), getVideoSourceSSRC()));
  }
}
// changes the outgoing payload type for in the given data packet
void MediaStream::sendPacketAsync(packetPtr packet) {
  if (!sending_) {
    return;
  }
  auto stream_ptr = shared_from_this();
  if (packet->comp == -1) {
    sending_ = false;
    auto p = std::make_shared<DataPacket>();
    p->comp = -1;
    asyncTask([=] {
      sendPacket(p);
    });
    return;
  }

  changeDeliverPayloadType(packet.get(), packet->type);
  asyncTask([=] {
    sendPacket(packet);
  });
}

void MediaStream::setSlideShowMode(bool state) {
  if (slide_show_mode_ == state) {
    Log("setSlideShowMode: skip same %u", state);
    return;
  }
  asyncTask([=]() {
    stats_->getNode()[getVideoSinkSSRC()].insertStat(
      "erizoSlideShow", CumulativeStat{state});
  });
  Log("setSlideShowMode: %u", state);
  slide_show_mode_ = state;
  notifyUpdateToHandlers();
}

void MediaStream::muteStream(bool mute_video, bool mute_audio) {
  asyncTask([=] () {
	  Log("muteStream, video: %u, audio: %u", mute_video, mute_audio);
    audio_muted_ = mute_audio;
    video_muted_ = mute_video;
    stats_->getNode()[getAudioSinkSSRC()].insertStat("erizoAudioMute", CumulativeStat{mute_audio});
    stats_->getNode()[getAudioSinkSSRC()].insertStat("erizoVideoMute", CumulativeStat{mute_video});
    if (pipeline_) {
      pipeline_->notifyUpdate();
    }
  });
}

void MediaStream::setVideoConstraints(int max_video_width, int max_video_height, int max_video_frame_rate) {
  asyncTask([=]() {
    quality_manager_->setVideoConstraints(max_video_width, max_video_height, max_video_frame_rate);
  });
}

void MediaStream::setTransportInfo(std::string audio_info, std::string video_info) {
  if (video_enabled_) {
    uint32_t video_sink_ssrc = getVideoSinkSSRC();
    uint32_t video_source_ssrc = getVideoSourceSSRC();

    if (video_sink_ssrc != kDefaultVideoSinkSSRC) {
      stats_->getNode()[video_sink_ssrc].insertStat("clientHostType", StringStat{video_info});
    }
    if (video_source_ssrc != 0) {
      stats_->getNode()[video_source_ssrc].insertStat("clientHostType", StringStat{video_info});
    }
  }

  if (audio_enabled_) {
    uint32_t audio_sink_ssrc = getAudioSinkSSRC();
    uint32_t audio_source_ssrc = getAudioSourceSSRC();

    if (audio_sink_ssrc != kDefaultAudioSinkSSRC) {
      stats_->getNode()[audio_sink_ssrc].insertStat("clientHostType", StringStat{audio_info});
    }
    if (audio_source_ssrc != 0) {
      stats_->getNode()[audio_source_ssrc].insertStat("clientHostType", StringStat{audio_info});
    }
  }
}

void MediaStream::setFeedbackReports(bool will_send_fb, uint32_t target_bitrate) {
  if (slide_show_mode_) {
    Log("slide_show_mode ignore target_bitrate setting %u", target_bitrate);
    target_bitrate = 0;
  }

  Log("setFeedbackReports %d, bitrate %u", will_send_fb, target_bitrate);
  should_send_feedback_ = will_send_fb;
  if (target_bitrate == 1) {
    Log("disable video");
    video_enabled_ = false;
  }
  rate_control_ = target_bitrate;
}

void MediaStream::setMetadata(std::map<std::string, std::string> metadata) {
  for (const auto &item : metadata) {
    log_stats_->getNode().insertStat("metadata-" + item.first, StringStat{item.second});
  }
  setLogContext(metadata);
}

WebRTCEvent MediaStream::getCurrentState() {
  return connection_->getCurrentState();
}

void MediaStream::getJSONStats(std::function<void(std::string)> callback) {
  asyncTask([=] () {
    std::string requested_stats = stats_->getStats();
    // Log("stats: %s", requested_stats.c_str());
    callback(requested_stats);
  });
}

void MediaStream::changeDeliverPayloadType(DataPacket *dp, packetType type) { 
  if (dp && !dp->isRtcp()) {
    RtpHeader* h = dp->rtp();
      int internalPT = h->getPayloadType();
      int externalPT = internalPT;
      if (type == AUDIO_PACKET) {
          externalPT = remote_sdp_->getAudioExternalPT(internalPT);
      } else if (type == VIDEO_PACKET) {
          externalPT = remote_sdp_->getVideoExternalPT(externalPT);
      }
      if (internalPT != externalPT) {
          h->setPayloadType(externalPT);
      }
  }
}

// parses incoming payload type, replaces occurence in buf
void MediaStream::parseIncomingPayloadType(packetPtr packet, packetType type) {
  if (!packet->isRtcp()) {
    RtpHeader* h = packet->rtp();
    int externalPT = h->getPayloadType();
    int internalPT = externalPT;
    if (type == AUDIO_PACKET) {
      internalPT = remote_sdp_->getAudioInternalPT(externalPT);
    } else if (type == VIDEO_PACKET) {
      internalPT = remote_sdp_->getVideoInternalPT(externalPT);
    }
    if (externalPT != internalPT) {
      h->setPayloadType(internalPT);
    } else {
//    Log("onTransportData did not find mapping for %i", externalPT);
    }
  }
}

void MediaStream::write(packetPtr packet) {
  if (connection_) {
    connection_->write(packet);
  }
}

void MediaStream::enableHandler(const std::string &name) {
  asyncTask([name,this] () {
      if (pipeline_) {
        Log("enableHandler %s", name.c_str());
        pipeline_->enable(name);
      }
  });
}

void MediaStream::disableHandler(const std::string &name) {
  asyncTask([name, this] () {
    if (pipeline_) {
      Log("disableHandler %s", name.c_str());
      pipeline_->disable(name);
    }
  });
}

void MediaStream::notifyUpdateToHandlers() {
  asyncTask([this]() {
    if (pipeline_) {
      pipeline_->notifyUpdate();
    }
  });
}

void MediaStream::asyncTask(std::function<void()> f) {
  std::weak_ptr<MediaStream> weak_this = shared_from_this();
  worker_->task([weak_this, f] {
    if (auto this_ptr = weak_this.lock()) {
      f();
    }
  });
}

void MediaStream::sendPacket(packetPtr p) {
  if (!sending_) {
    return;
  }
  uint32_t partial_bitrate = 0;
  uint64_t sentVideoBytes = 0;
  uint64_t lastSecondVideoBytes = 0;

  if (rate_control_ && !slide_show_mode_) {
    if (p->type == VIDEO_PACKET) {
      if (rate_control_ == 1) {
        return;
      }
      now_ = clock::now();
      if ((now_ - mark_) >= kBitrateControlPeriod) {
        mark_ = now_;
        lastSecondVideoBytes = sentVideoBytes;
      }
      partial_bitrate = ((sentVideoBytes - lastSecondVideoBytes) * 8) * 10;
      if (partial_bitrate > this->rate_control_) {
        // Log("skip packet %p,%d for rate_control", p.get(), p->length);
        return;
      }
      sentVideoBytes += p->length;
    }
  }
  if (!pipeline_initialized_) {
    Log("Pipeline not initialized yet.");
    return;
  }

  if (pipeline_) {
    pipeline_->write(std::move(p));
  }
}

void MediaStream::setQualityLayer(int spatial_layer, int temporal_layer) {
  asyncTask([=]() {
    quality_manager_->forceLayers(spatial_layer, temporal_layer);
  });
}

void MediaStream::enableSlideShowBelowSpatialLayer(bool enabled, int spatial_layer) {
  asyncTask([=]() {
    quality_manager_->enableSlideShowBelowSpatialLayer(enabled, spatial_layer);
  });
}

}  // namespace erizo
