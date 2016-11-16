/*
 * WebRTCConnection.cpp
 */
#include <algorithm>
#include "thread/Worker.h"
#include "WebRtcConnection.h"
#include "DtlsTransport.h"
#include "SdpInfo.h"
#include "rtp/RtpHeaders.h"
#include "rtp/RtpVP8Parser.h"
#include "rtp/RtcpAggregator.h"
#include "rtp/RtcpForwarder.h"
#include "rtp/RtpSlideShowHandler.h"
#include "rtp/RtpAudioMuteHandler.h"
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
#include "rtp/PacketBufferService.h"
#include "rtp/PliPacerHandler.h"
#include "rtp/RtpPaddingGeneratorHandler.h"
#include "Stats.h"
namespace erizo {
DEFINE_LOGGER(WebRtcConnection, "WebRtcConnection");

constexpr uint32_t kDefaultVideoSinkSSRC = 55543;
constexpr uint32_t kDefaultAudioSinkSSRC = 44444;

WebRtcConnection::WebRtcConnection(std::shared_ptr<Worker> worker, const std::string& connection_id,
    const IceConfig& iceConfig, const std::vector<RtpMap>& rtp_mappings,
    const std::vector<erizo::ExtMap>& ext_mappings, WebRtcConnectionEventListener* listener) :
  connEventListener_{ listener }, connection_id_{connection_id}, 
  remoteSdp_{SdpInfo(rtp_mappings)}, localSdp_{SdpInfo(rtp_mappings)},
    iceConfig_{iceConfig}, rtp_mappings_{rtp_mappings}, extProcessor_{ext_mappings},
  pipeline_{Pipeline::create()}, worker_{ worker } {
  audioEnabled_ = videoEnabled_ = bundle_ = audio_muted_ = pipeline_initialized_ = false;
  set_log_context(connection_id_.c_str());
  Info("constructor, stun %s:%d, port range: %d->%d",
      iceConfig.stunServer.c_str(), iceConfig.stunPort, iceConfig.minPort, iceConfig.maxPort);
  setVideoSinkSSRC(kDefaultVideoSinkSSRC);
  setAudioSinkSSRC(kDefaultAudioSinkSSRC);
  source_fb_sink_ = this;
  sink_fb_source_ = this;

  globalState_ = CONN_INITIAL;

  stats_ = std::make_shared<Stats>();
  quality_manager_ = std::make_shared<QualityManager>();
  packet_buffer_ = std::make_shared<PacketBufferService>();
  globalState_ = CONN_INITIAL;

  rtcp_processor_ = std::make_shared<RtcpForwarder>(static_cast<MediaSink*>(this), static_cast<MediaSource*>(this));

  trickleEnabled_ = iceConfig_.shouldTrickle;
  shouldSendFeedback_ = true;

  slide_show_mode_ = false;
  rateControl_ = 0;
  mark_ = clock::now();

  sending_ = true;
}

WebRtcConnection::~WebRtcConnection() {
  Info("Destructor called");
  if (sending_) {
    close();
  }
  Info("Destructor ended");
}

void WebRtcConnection::close() {
  Info("Close called");
  if (!sending_) {
    return;
  }
  sending_ = false;
  if (videoTransport_.get()) {
    videoTransport_->close();
  }
  if (audioTransport_.get()) {
    audioTransport_->close();
  }
  globalState_ = CONN_FINISHED;
  if (connEventListener_ ) {
    connEventListener_ = nullptr;
  }
  video_sink_ = nullptr;
  audio_sink_ = nullptr;
  fb_sink_ = nullptr;
  Info("Destructor ended");
}

void WebRtcConnection::setWebRtcConnectionStatsListener(WebRtcConnectionStatsListener* listener)
{
  stats_->setStatsListener(listener);
}

bool WebRtcConnection::init() {
  if (connEventListener_ ) {
    connEventListener_->notifyEvent(globalState_, "");
  }
  return true;
}

bool WebRtcConnection::isSourceSSRC(uint32_t ssrc) {
  return isVideoSourceSSRC(ssrc) || isAudioSourceSSRC(ssrc);
}

bool WebRtcConnection::isSinkSSRC(uint32_t ssrc) {
  return isVideoSinkSSRC(ssrc) || isAudioSinkSSRC(ssrc);
}

void WebRtcConnection::asyncTask(std::function<void(std::shared_ptr<WebRtcConnection>)> f) {
  std::weak_ptr<WebRtcConnection> weak_this = shared_from_this();
  worker_->task([weak_this, f] {
    if (auto this_ptr = weak_this.lock()) {
      f(this_ptr);
    }
  });
}

bool WebRtcConnection::createOffer(bool videoEnabled, bool audioEnabled, bool bundle) {
  bundle_ = bundle;
  videoEnabled_ = videoEnabled;
  audioEnabled_ = audioEnabled;
  localSdp_.createOfferSdp(videoEnabled_, audioEnabled_, bundle_);
  localSdp_.dtlsRole = ACTPASS;

  Info("Creating sdp offer, isBundle: %d audio:%d video:%d", bundle_, audioEnabled_, videoEnabled);
  if (videoEnabled_)
    localSdp_.video_ssrc_list.push_back(getVideoSinkSSRC());
  if (audioEnabled_)
    localSdp_.audio_ssrc = getAudioSinkSSRC();

  auto listener = std::dynamic_pointer_cast<TransportListener>(shared_from_this());

  if (bundle_) {
    videoTransport_.reset(new DtlsTransport(VIDEO_TYPE, "video", connection_id_, bundle_, true,
                                            listener, iceConfig_ , true, worker_));
    videoTransport_->start();
  } else {
    if (videoTransport_.get() == nullptr && videoEnabled_) {
      // For now we don't re/check transports, if they are already created we leave them there
      videoTransport_.reset(new DtlsTransport(VIDEO_TYPE, "video", connection_id_, bundle_, true,
                                              listener, iceConfig_ , true, worker_));
      videoTransport_->start();
    }
    if (audioTransport_.get() == nullptr && audioEnabled_) {
      audioTransport_.reset(new DtlsTransport(AUDIO_TYPE, "audio", connection_id_, bundle_, true,
                                              listener, iceConfig_, true, worker_));
      audioTransport_->start();
    }
  }
  if (connEventListener_ ) {
    std::string msg = this->getLocalSdp();
    connEventListener_->notifyEvent(globalState_, msg);
  }
  return true;
}

bool WebRtcConnection::setRemoteSdp(const std::string &sdp) {

  if (!remoteSdp_.initWithSdp(sdp, "")) {
    Info("setRemoteSdp error %s", sdp.c_str());
    return false;
  }

  Info("setRemoteSdp %s", sdp.c_str());
  if (remoteSdp_.videoBandwidth != 0) {
    Info("Setting remote BW, maxVideoBW: %u", remoteSdp_.videoBandwidth);
    rtcp_processor_->setMaxVideoBW(remoteSdp_.videoBandwidth*1000);
  }

  if (pipeline_initialized_) {
    Warn("setRemoteSdp skip because pipeline not initialized");
    return true;
  }

  bundle_ = remoteSdp_.isBundle;
  localSdp_.setOfferSdp(remoteSdp_);

  extProcessor_.setSdpInfo(localSdp_);
  localSdp_.updateSupportedExtensionMap(extProcessor_.getSupportedExtensionMap());

  localSdp_.video_ssrc_list.push_back(getVideoSinkSSRC());
  localSdp_.audio_ssrc = getAudioSinkSSRC();

  if (remoteSdp_.dtlsRole == ACTPASS) {
    localSdp_.dtlsRole = ACTIVE;
  }
  setVideoSourceSSRCList(remoteSdp_.video_ssrc_list);
  setAudioSourceSSRC(remoteSdp_.audio_ssrc);

  audioEnabled_ = remoteSdp_.hasAudio;
  videoEnabled_ = remoteSdp_.hasVideo;
  rtcp_processor_->addSourceSsrc(getAudioSourceSSRC());
  std::for_each(video_source_ssrc_list_.begin(), video_source_ssrc_list_.end(), [this] (uint32_t new_ssrc){
      rtcp_processor_->addSourceSsrc(new_ssrc);
  });
  Info("setRemoteSdp bundle %d, audio %d/%u, video %d/%u with %d ssrcs", bundle_, 
    audioEnabled_, getAudioSourceSSRC(), 
    videoEnabled_, getVideoSourceSSRC(), remoteSdp_.video_ssrc_list.size());

  if (remoteSdp_.profile == SAVPF) {
    if (remoteSdp_.isFingerprint) {
      auto listener = std::dynamic_pointer_cast<TransportListener>(shared_from_this());
      if (remoteSdp_.hasVideo || bundle_) {
        std::string username = remoteSdp_.getUsername(VIDEO_TYPE);
        std::string password = remoteSdp_.getPassword(VIDEO_TYPE);
        if (videoTransport_.get() == nullptr) {
          Info("Creating videoTransport, ufrag: %s, pass: %s", username.c_str(), password.c_str());
          videoTransport_.reset(new DtlsTransport(VIDEO_TYPE, "video", 
            connection_id_, bundle_, remoteSdp_.isRtcpMux, listener, iceConfig_ , false, worker_));
          videoTransport_->start();
        }
        videoTransport_->getNiceConnection()->setRemoteCredentials(username, password);
      }
      if (!bundle_ && remoteSdp_.hasAudio) {
        std::string username = remoteSdp_.getUsername(AUDIO_TYPE);
        std::string password = remoteSdp_.getPassword(AUDIO_TYPE);
        if (audioTransport_.get() == nullptr) {
          Info("Creating audioTransport, ufrag: %s, pass: %s", username.c_str(), password.c_str());
          audioTransport_.reset(new DtlsTransport(AUDIO_TYPE, "audio", 
            connection_id_, bundle_, remoteSdp_.isRtcpMux, listener, iceConfig_, false, worker_));
          audioTransport_->start();
        }
        audioTransport_->getNiceConnection()->setRemoteCredentials(username, password);
      }
    }
  }
  if (getCurrentState() >= CONN_GATHERED) {
    if (!remoteSdp_.getCandidateInfos().empty()) {
      Info("Setting %d remote candidates after gathered", remoteSdp_.getCandidateInfos().size());
      if (remoteSdp_.hasVideo) {
        videoTransport_->setRemoteCandidates(remoteSdp_.getCandidateInfos(), bundle_);
      }
      if (!bundle_ && remoteSdp_.hasAudio) {
        audioTransport_->setRemoteCandidates(remoteSdp_.getCandidateInfos(), bundle_);
      }
    }
  }

  if (trickleEnabled_) {
    std::string object = getLocalSdp();
    if (connEventListener_) {
      connEventListener_->notifyEvent(CONN_SDP, object);
    }
  }

  initializePipeline();
  return true;
}

void WebRtcConnection::initializePipeline() {
  pipeline_->addService(shared_from_this());
  pipeline_->addService(rtcp_processor_);
  pipeline_->addService(stats_);
  pipeline_->addService(quality_manager_);
  pipeline_->addService(packet_buffer_);

  pipeline_->addFront(PacketReader(this));

  pipeline_->addFront(LayerDetectorHandler());
  pipeline_->addFront(RtcpProcessorHandler());
  pipeline_->addFront(IncomingStatsHandler());
  pipeline_->addFront(FecReceiverHandler());
  pipeline_->addFront(LayerBitrateCalculationHandler());
  pipeline_->addFront(QualityFilterHandler());
  pipeline_->addFront(RtpAudioMuteHandler());
  pipeline_->addFront(RtpSlideShowHandler());
  pipeline_->addFront(RtpPaddingGeneratorHandler());
  pipeline_->addFront(PliPacerHandler());
  pipeline_->addFront(BandwidthEstimationHandler());
  pipeline_->addFront(RtpPaddingRemovalHandler());
  pipeline_->addFront(RtcpFeedbackGenerationHandler());
  pipeline_->addFront(RtpRetransmissionHandler());
  pipeline_->addFront(SRPacketHandler());
  pipeline_->addFront(SenderBandwidthEstimationHandler());
  pipeline_->addFront(OutgoingStatsHandler());

  pipeline_->addFront(PacketWriter(this));
  pipeline_->finalize();
  pipeline_initialized_ = true;
}

bool WebRtcConnection::addRemoteCandidate(const std::string &mid, int mLineIndex, const std::string &sdp) {
  // TODO(pedro) Check type of transport.
  Info("Adding remote candidate: %s, mid: %s, sdpMLine: %d", sdp.c_str(), mid.c_str(), mLineIndex);
  if (videoTransport_ == nullptr && audioTransport_ == nullptr) {
    Warn("skip addRemoteCandidate without transport");
    return false;
  }
  MediaType theType;
  std::string theMid;
  // Checking if it's the last candidate, only works in bundle.
  if (mLineIndex == -1) {
    videoTransport_->getNiceConnection()->setReceivedLastCandidate(true);
  }
  if ((!mid.compare("video")) || (mLineIndex == remoteSdp_.videoSdpMLine)) {
    theType = VIDEO_TYPE;
    theMid = "video";
  } else {
    theType = AUDIO_TYPE;
    theMid = "audio";
  }

  // fill with some elem
  SdpInfo tempSdp(rtp_mappings_);
  std::string username = remoteSdp_.getUsername(theType);
  std::string password = remoteSdp_.getPassword(theType);
  tempSdp.setCredentials(username, password, OTHER);

  bool res = false;
  if (tempSdp.initWithSdp(sdp, theMid)) {
    std::vector<CandidateInfo>& cands = tempSdp.getCandidateInfos();
    if (theType == VIDEO_TYPE || bundle_) {
      res = videoTransport_->setRemoteCandidates(cands, bundle_);
    } else if (theType == AUDIO_TYPE) {
      res = audioTransport_->setRemoteCandidates(cands, bundle_);
    } else {
      Warn("add remote candidate with no Media (video or audio), candidate: %s", sdp.c_str() );
    }
    // save candidate to real remoteSdp_
    for (uint8_t it = 0; it < cands.size(); it++) {
      remoteSdp_.addCandidate(cands[it]);
    }
  }

  return res;
}

std::string WebRtcConnection::getLocalSdp() {
  if (videoTransport_) {
    videoTransport_->processLocalSdp(&localSdp_);
  }
  if (!bundle_ && audioTransport_) {
    audioTransport_->processLocalSdp(&localSdp_);
  }
  localSdp_.profile = remoteSdp_.profile;
  return localSdp_.getSdp();
}

std::string WebRtcConnection::getJSONCandidate(const std::string& mid, const std::string& sdp) {
  std::map <std::string, std::string> object;
  object["sdpMid"] = mid;
  object["candidate"] = sdp;
  object["sdpMLineIndex"] = std::to_string((mid.compare("video") ? localSdp_.audioSdpMLine : localSdp_.videoSdpMLine));

  std::ostringstream theString;
  theString << "{";
  for (std::map<std::string, std::string>::iterator it = object.begin(); it != object.end(); ++it) {
    theString << "\"" << it->first << "\":\"" << it->second << "\"";
    if (++it != object.end()) {
      theString << ",";
    }
    --it;
  }
  theString << "}";
  return theString.str();
}

void WebRtcConnection::onCandidate(const CandidateInfo& cand, Transport *transport) {
  std::string sdp = localSdp_.addCandidate(cand);
  Info("got new candidate: %s", sdp.c_str());
  if (trickleEnabled_) {
    if (connEventListener_) {
      if (!bundle_) {
        std::string object = getJSONCandidate(transport->transport_name, sdp);
        connEventListener_->notifyEvent(CONN_CANDIDATE, object);
      } else {
        if (remoteSdp_.hasAudio) {
          std::string object = getJSONCandidate("audio", sdp);
          connEventListener_->notifyEvent(CONN_CANDIDATE, object);
        }
        if (remoteSdp_.hasVideo) {
          std::string object2 = getJSONCandidate("video", sdp);
          connEventListener_->notifyEvent(CONN_CANDIDATE, object2);
        }
      }
    }
  }
}

int WebRtcConnection::deliverAudioData_(packetPtr audio_packet) {
  if (bundle_) {
    if (videoTransport_.get() ) {
      if (audioEnabled_) {
        sendPacketAsync(std::make_shared<dataPacket>(*audio_packet));
      }
    }
  } else if (audioTransport_.get() ) {
    if (audioEnabled_) {
        sendPacketAsync(std::make_shared<dataPacket>(*audio_packet));
    }
  }
  return audio_packet->length;
}

int WebRtcConnection::deliverVideoData_(packetPtr video_packet) {
  if (videoTransport_.get() ) {
    if (videoEnabled_) {
      sendPacketAsync(std::make_shared<dataPacket>(*video_packet));
    }
  }
  return video_packet->length;
}

int WebRtcConnection::deliverFeedback_(packetPtr fb_packet) {
  RtcpHeader *chead = reinterpret_cast<RtcpHeader*>(fb_packet->data);
  uint32_t recvSSRC = chead->getSourceSSRC();
  if (isVideoSourceSSRC(recvSSRC)) {
    fb_packet->type = VIDEO_PACKET;
    sendPacketAsync(fb_packet);
  } else if (isAudioSourceSSRC(recvSSRC)) {
    fb_packet->type = AUDIO_PACKET;
    sendPacketAsync(fb_packet);
  } else {
    Info("unknownSSRC: %u, localVideoSSRC: %u, localAudioSSRC: %u",
       recvSSRC, getVideoSourceSSRC(), getAudioSourceSSRC());
  }
  return fb_packet->length;
}

void WebRtcConnection::onTransportData(packetPtr packet, Transport *transport) {
  if ((audio_sink_ == nullptr && video_sink_ == nullptr && fb_sink_ == nullptr) ||
      getCurrentState() != CONN_READY) {
    return;
  }

  if (transport->mediaType == AUDIO_TYPE) {
    packet->type = AUDIO_PACKET;
  } else if (transport->mediaType == VIDEO_TYPE) {
    packet->type = VIDEO_PACKET;
  }

  char* buf = packet->data;
  RtpHeader *head = reinterpret_cast<RtpHeader*> (buf);
  RtcpHeader *chead = reinterpret_cast<RtcpHeader*> (buf);
  if (!chead->isRtcp()) {
    // use ssrc fixed packet type(for bundle_)
    uint32_t recvSSRC = head->getSSRC();
    if (isVideoSourceSSRC(recvSSRC)) {
      packet->type = VIDEO_PACKET;
    } else if (isAudioSourceSSRC(recvSSRC)) {
      packet->type = AUDIO_PACKET;
    }
  }

  if (!pipeline_initialized_) {
    Info("Pipeline not initialized yet. skip %s %d data", transport->transport_name.c_str(), packet->length);
    return;
  }

  // pass data to pipline
  pipeline_->read(packet);
}

// change rtp payload type and passed to sink.
void WebRtcConnection::read(packetPtr packet) {
  char* buf = packet->data;
  int len = packet->length;
  // PROCESS RTCP
  RtpHeader *head = reinterpret_cast<RtpHeader*> (buf);
  RtcpHeader *chead = reinterpret_cast<RtcpHeader*> (buf);
  uint32_t recvSSRC;
  if (!chead->isRtcp()) {
    recvSSRC = head->getSSRC();
  } else if (chead->packettype == RTCP_Sender_PT) {  // Sender Report
    recvSSRC = chead->getSSRC();
  }

  // DELIVER FEEDBACK (RR, FEEDBACK PACKETS)
  if (chead->isFeedback()) {
    if (fb_sink_ && shouldSendFeedback_) {
      fb_sink_->deliverFeedback(packet);
    }
  } else {
    // RTP or RTCP Sender Report
    if (bundle_) {
      // Check incoming SSRC
      // Deliver data
      if (isVideoSourceSSRC(recvSSRC)) {
        parseIncomingPayloadType(buf, len, VIDEO_PACKET);
        video_sink_->deliverVideoData(packet);
      } else if (isAudioSourceSSRC(recvSSRC)) {
        parseIncomingPayloadType(buf, len, AUDIO_PACKET);
        audio_sink_->deliverAudioData(packet);
      } else {
        Info("unknownSSRC: %u, localVideoSSRC: %u, localAudioSSRC: %u",
                    recvSSRC, getVideoSourceSSRC(), getAudioSourceSSRC());
      }
    } else {
      if (packet->type == AUDIO_PACKET && audio_sink_) {
        parseIncomingPayloadType(buf, len, AUDIO_PACKET);
        // Firefox does not send SSRC in SDP
        if (getAudioSourceSSRC() == 0) {
          Info("discoveredAudioSourceSSRC:%u", recvSSRC);
          setAudioSourceSSRC(recvSSRC);
        }
        audio_sink_->deliverAudioData(packet);
      } else if (packet->type == VIDEO_PACKET && video_sink_) {
        parseIncomingPayloadType(buf, len, VIDEO_PACKET);
        // Firefox does not send SSRC in SDP
        if (getVideoSourceSSRC() == 0) {
          Info("discoveredVideoSourceSSRC:%u", recvSSRC);
          setVideoSourceSSRC(recvSSRC);
        }
        // change ssrc for RTP packets, don't touch here if RTCP
        video_sink_->deliverVideoData(packet);
      }
    }  // if not bundle
  }  // if not Feedback
}

int WebRtcConnection::sendPLI() {
  RtcpHeader thePLI;
  thePLI.setPacketType(RTCP_PS_Feedback_PT);
  thePLI.setBlockCount(1);
  thePLI.setSSRC(getVideoSinkSSRC());
  thePLI.setSourceSSRC(getVideoSourceSSRC());
  thePLI.setLength(2);
  int len = thePLI.getPacketSize();
  sendPacketAsync(std::make_shared<dataPacket>(0, reinterpret_cast<char*>(&thePLI), len, VIDEO_PACKET));
  return len;
}

void WebRtcConnection::updateState(TransportState state, Transport * transport) {
  Info("transportName: %s, new_state: %d", transport->transport_name.c_str(), state);
  std::unique_lock<std::mutex> lock(updateStateMutex_);
  if (!videoTransport_.get() && !audioTransport_.get()) {
    Warn("Updating NULL transport, state: %d", state);
    return;
  }
  if (globalState_ == CONN_FAILED) {
    // if current state is failed -> noop
    Warn("CONN_FAILED, skip transport state: %d", state);
    return;
  }

  WebRTCEvent temp = globalState_;
  std::string msg = "";
  switch (state) {
    case TRANSPORT_STARTED:
      if (bundle_) {
        temp = CONN_STARTED;
      } else {
        if ((!remoteSdp_.hasAudio || (audioTransport_.get() && audioTransport_->getTransportState() == TRANSPORT_STARTED)) &&
            (!remoteSdp_.hasVideo || (videoTransport_.get() && videoTransport_->getTransportState() == TRANSPORT_STARTED))) {
            // WebRTCConnection will be ready only when all channels are ready.
            temp = CONN_STARTED;
          }
      }
      break;
    case TRANSPORT_GATHERED:
      if (bundle_) {
        if (!remoteSdp_.getCandidateInfos().empty()) {
          // Passing now new candidates that could not be passed before
          if (remoteSdp_.hasVideo) {
            videoTransport_->setRemoteCandidates(remoteSdp_.getCandidateInfos(), bundle_);
          }
          if (!bundle_ && remoteSdp_.hasAudio) {
            audioTransport_->setRemoteCandidates(remoteSdp_.getCandidateInfos(), bundle_);
          }
        }
        if (!trickleEnabled_) {
          temp = CONN_GATHERED;
          msg = getLocalSdp();
        }
      } else {
        if ((!localSdp_.hasAudio || (audioTransport_.get() && audioTransport_->getTransportState() == TRANSPORT_GATHERED)) &&
            (!localSdp_.hasVideo || (videoTransport_.get() && videoTransport_->getTransportState() == TRANSPORT_GATHERED))) {
            // WebRTCConnection will be ready only when all channels are ready.
            if (!trickleEnabled_) {
              temp = CONN_GATHERED;
              msg = getLocalSdp();
            }
          }
      }
      break;
    case TRANSPORT_READY:
      if (bundle_) {
        temp = CONN_READY;
        trackTransportInfo();
      } else {
        if ((!remoteSdp_.hasAudio || (audioTransport_.get() && audioTransport_->getTransportState() == TRANSPORT_READY)) &&
            (!remoteSdp_.hasVideo || (videoTransport_.get() && videoTransport_->getTransportState() == TRANSPORT_READY))) {
            // WebRTCConnection will be ready only when all channels are ready.
            temp = CONN_READY;
            trackTransportInfo();
          }
      }
      break;
    case TRANSPORT_FAILED:
      temp = CONN_FAILED;
      msg = remoteSdp_.getSdp();
      sending_ = false;
      Warn("Transport Failed, transportType: %s", transport->transport_name.c_str() );
      break;
    default:
      Info("Doing nothing on state, state %d", state);
      break;
  }

  if (audioTransport_.get() && videoTransport_.get()) {
    Info("Update Transport State name: %s, videoState: %d, audioState: %d, calculatedState: %d, globalState: %d",
      transport->transport_name.c_str(),
      (int)audioTransport_->getTransportState(),
      (int)videoTransport_->getTransportState(),
      (int)temp, (int)globalState_);
  }

  if (globalState_ == temp) {
    return;
  }

  Info("newGlobalState: %d->%d", globalState_, temp);
  globalState_ = temp;

  if (connEventListener_ ) {
    connEventListener_->notifyEvent(globalState_, msg);
  }
}

void WebRtcConnection::trackTransportInfo() {
  CandidatePair candidate_pair;
  if (videoEnabled_ && videoTransport_) {
    candidate_pair = videoTransport_->getNiceConnection()->getSelectedPair();
    asyncTask([candidate_pair] (std::shared_ptr<WebRtcConnection> connection) {
      std::shared_ptr<Stats> stats = connection->stats_;
      uint32_t video_sink_ssrc = connection->getVideoSinkSSRC();
      uint32_t video_source_ssrc = connection->getVideoSourceSSRC();
      if (video_sink_ssrc != kDefaultVideoSinkSSRC) {
        stats->getNode()[video_sink_ssrc].insertStat(
          "clientHostType", StringStat(candidate_pair.clientHostType));
    }
      if (video_source_ssrc != 0) {
        stats->getNode()[video_source_ssrc].insertStat(
          "clientHostType", StringStat(candidate_pair.clientHostType));
    }
    });
  }

  if (audioEnabled_ && audioTransport_) {
      candidate_pair = audioTransport_->getNiceConnection()->getSelectedPair();
    asyncTask([candidate_pair] (std::shared_ptr<WebRtcConnection> connection) {
      std::shared_ptr<Stats> stats = connection->stats_;
      uint32_t audio_sink_ssrc = connection->getAudioSinkSSRC();
      uint32_t audio_source_ssrc = connection->getAudioSourceSSRC();

      if (audio_sink_ssrc != kDefaultAudioSinkSSRC) {
        stats->getNode()[audio_sink_ssrc].insertStat(
          "clientHostType", StringStat(candidate_pair.clientHostType));
    }
      if (audio_source_ssrc != 0) {
        stats->getNode()[audio_source_ssrc].insertStat(
          "clientHostType", StringStat(candidate_pair.clientHostType));
    }
    });
  }
}

// changes the outgoing payload type for in the given data packet
void WebRtcConnection::sendPacketAsync(packetPtr packet) {
  if (!sending_ || getCurrentState() != CONN_READY) {
    return;
  }
  auto conn_ptr = shared_from_this();
  if (packet->comp == -1) {
    sending_ = false;
    auto p = std::make_shared<dataPacket>();
    p->comp = -1;
    worker_->task([conn_ptr, p]{
      conn_ptr->sendPacket(p);
    });
  }
  else {
  changeDeliverPayloadType(packet.get(), packet->type);
  worker_->task([conn_ptr, packet]{
    conn_ptr->sendPacket(packet);
  });
}
}

void WebRtcConnection::sendPacket(packetPtr p) {
  if (!sending_) {
    return;
  }
  uint32_t partial_bitrate = 0;

  // 代码不完整，这俩变量必须移到成员变量中 
  // 应该是没用此速率控制了，随机丢视频数据来控制码率
  uint64_t sentVideoBytes = 0;
  uint64_t lastSecondVideoBytes = 0;

  if (rateControl_ && !slide_show_mode_) {
    if (p->type == VIDEO_PACKET) {
      if (rateControl_ == 1) {
        return;
      }
      time_point now_ = clock::now();
      if ((now_ - mark_) >= kBitrateControlPeriod) {
        mark_ = now_;
        // 100 ms后重置计数器
        lastSecondVideoBytes = sentVideoBytes;
      }
      partial_bitrate = ((sentVideoBytes - lastSecondVideoBytes) * 8) * 100;
      if (partial_bitrate > rateControl_) {
        // just lost the packet? 保证100ms前面数据，丢失后面数据
        return;
      }
      sentVideoBytes += p->length;
    }
  }
  if (!pipeline_initialized_) {
    Info("Pipeline not initialized yet.");
    return;
  }

  pipeline_->write(p);
}

void WebRtcConnection::setSlideShowMode(bool state) {
  ELOG_DEBUG("slideShowMode: %u", state);
  if (slide_show_mode_ == state) {
    return;
  }
  asyncTask([state] (std::shared_ptr<WebRtcConnection> connection) {
    connection->stats_->getNode()[connection->getVideoSinkSSRC()].insertStat("erizoSlideShow", CumulativeStat(state));
  });
  slide_show_mode_ = state;
  notifyUpdateToHandlers();
}

void WebRtcConnection::muteStream(bool mute_video, bool mute_audio) {
  Info("muteStream video %u, audio %u", mute_video, mute_audio);
  asyncTask([mute_audio] (std::shared_ptr<WebRtcConnection> connection) {
    connection->stats_->getNode()[connection->getAudioSinkSSRC()].insertStat("erizoMute", CumulativeStat(mute_audio));
  });
  audio_muted_ = mute_audio;
  notifyUpdateToHandlers();
}

void WebRtcConnection::setFeedbackReports(bool will_send_fb, uint32_t target_bitrate) {
  if (slide_show_mode_) {
    target_bitrate = 0;
  }

  shouldSendFeedback_ = will_send_fb;
  if (target_bitrate == 1) {
    videoEnabled_ = false;
  }
  rateControl_ = target_bitrate;
}

void WebRtcConnection::setMetadata(std::map<std::string, std::string> metadata) {
  setLogContext(metadata);
}

WebRTCEvent WebRtcConnection::getCurrentState() {
  return globalState_;
}

void WebRtcConnection::getJSONStats(std::function<void(std::string)> callback) {
  asyncTask([callback] (std::shared_ptr<WebRtcConnection> connection) {
    std::string requested_stats = connection->stats_->getStats();
    // Log("getJSONStats return %s", requested_stats.c_str());
    callback(requested_stats);
  });
}

void WebRtcConnection::changeDeliverPayloadType(dataPacket *dp, packetType type) {
  RtpHeader* h = reinterpret_cast<RtpHeader*>(dp->data);
  RtcpHeader *chead = reinterpret_cast<RtcpHeader*>(dp->data);
  if (!chead->isRtcp()) {
      int internalPT = h->getPayloadType();
      int externalPT = internalPT;
      if (type == AUDIO_PACKET) {
          externalPT = remoteSdp_.getAudioExternalPT(internalPT);
      } else if (type == VIDEO_PACKET) {
          externalPT = remoteSdp_.getVideoExternalPT(internalPT);
      }
      if (internalPT != externalPT) {
          h->setPayloadType(externalPT);
      }
  }
}

// parses incoming payload type, replaces occurence in buf
void WebRtcConnection::parseIncomingPayloadType(char *buf, int len, packetType type) {
  RtcpHeader* chead = reinterpret_cast<RtcpHeader*>(buf);
  RtpHeader* h = reinterpret_cast<RtpHeader*>(buf);
  if (!chead->isRtcp()) {
    int externalPT = h->getPayloadType();
    int internalPT = externalPT;
    if (type == AUDIO_PACKET) {
      internalPT = remoteSdp_.getAudioInternalPT(externalPT);
    } else if (type == VIDEO_PACKET) {
      internalPT = remoteSdp_.getVideoInternalPT(externalPT);
    }
    if (externalPT != internalPT) {
      h->setPayloadType(internalPT);
    } else {
//    ELOG_WARN("onTransportData did not find mapping for %i", externalPT);
    }
  }
}

void WebRtcConnection::write(packetPtr packet) {
  Transport *transport = audioTransport_.get();
  if(bundle_ || packet->type == VIDEO_PACKET) 
    transport = videoTransport_.get();

  if (transport == nullptr) {
    Warn("skip write %d:%d data without transport", packet->type, packet->length);
    return;
  }
  extProcessor_.processRtpExtensions(packet);
  transport->write(packet->data, packet->length);
}

void WebRtcConnection::enableHandler(const std::string &name) {
  asyncTask([name] (std::shared_ptr<WebRtcConnection> conn) {
    conn->pipeline_->enable(name);
  });
}

void WebRtcConnection::disableHandler(const std::string &name) {
  asyncTask([name] (std::shared_ptr<WebRtcConnection> conn) {
    conn->pipeline_->disable(name);
  });
}

void WebRtcConnection::notifyUpdateToHandlers() {
  asyncTask([] (std::shared_ptr<WebRtcConnection> conn) {
    conn->pipeline_->notifyUpdate();
  });
}

void WebRtcConnection::setQualityLayer(int spatial_layer, int temporal_layer) {
  asyncTask([spatial_layer, temporal_layer] (std::shared_ptr<WebRtcConnection> connection) {
    connection->quality_manager_->forceLayers(spatial_layer, temporal_layer);
  });
}

}  // namespace erizo
