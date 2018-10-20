/*
 * WebRTCConnection.cpp
 */

#include <cstdio>
#include <map>
#include <algorithm>
#include <string>
#include <sstream>
#include <cstring>
#include <vector>

#include "WebRtcConnection.h"
#include "MediaStream.h"
#include "DtlsTransport.h"
#include "SdpInfo.h"
#include "bandwidth/MaxVideoBWDistributor.h"
#include "bandwidth/TargetVideoBWDistributor.h"
#include "rtp/RtpHeaders.h"
#include "rtp/RtpUtils.h"

namespace erizo {
DEFINE_LOGGER(WebRtcConnection, "WebRtcConnection");

WebRtcConnection::WebRtcConnection(std::shared_ptr<Worker> worker, std::shared_ptr<IOWorker> io_worker,
    const std::string& connection_id, const IceConfig& ice_config, const std::vector<RtpMap>& rtp_mappings,
    const std::vector<erizo::ExtMap>& ext_mappings, WebRtcConnectionEventListener* listener) :
    connection_id_{connection_id}, conn_event_listener_{listener},
    ice_config_{ice_config}, rtp_mappings_{rtp_mappings}, extension_processor_{ext_mappings},
    worker_{worker}, io_worker_{io_worker},
    remote_sdp_{std::make_shared<SdpInfo>(rtp_mappings)}, local_sdp_{std::make_shared<SdpInfo>(rtp_mappings)} {
  audio_enabled_ = video_enabled_ = bundle_ = false;
  first_remote_sdp_processed_ = false;
  ice_config_.connection_id = connection_id;
  set_log_context("PC.%s", connection_id.c_str());
  Info("constructor, stunserver: %s, stunPort: %d, minPort: %d, maxPort: %d",
      ice_config.stun_server.c_str(), ice_config.stun_port, ice_config.min_port, ice_config.max_port);
  stats_ = std::make_shared<Stats>();
  distributor_.reset(new TargetVideoBWDistributor());
  global_state_ = CONN_INITIAL;

  sending_ = true;
}

WebRtcConnection::~WebRtcConnection() {
  Info("Destructor");
}

void WebRtcConnection::syncClose() {
  if (!sending_) {
	  Log("Close with no sending skip");
	  return;
  }
  Log("Close called clear %d stream", media_streams_.size());
  sending_ = false;
  media_streams_.clear();
  if (video_transport_) {
    video_transport_->close();
  }
  if (audio_transport_) {
    audio_transport_->close();
  }
  global_state_ = CONN_FINISHED;
  if (conn_event_listener_) {
    conn_event_listener_ = nullptr;
  }
  Log("Close ended");
}

void WebRtcConnection::close() {
  Log("Async close called");
  std::shared_ptr<WebRtcConnection> shared_this = shared_from_this();
  asyncTask([shared_this] (std::shared_ptr<WebRtcConnection> connection) {
    shared_this->syncClose();
  });
}

bool WebRtcConnection::init() {
  maybeNotifyWebRtcConnectionEvent(global_state_, "");
  return true;
}

bool WebRtcConnection::createOffer(bool video_enabled, bool audioEnabled, bool bundle) {
  AutoLock lock(state_mutex_);
  bundle_ = bundle;
  video_enabled_ = video_enabled;
  audio_enabled_ = audioEnabled;
  local_sdp_->createOfferSdp(video_enabled_, audio_enabled_, bundle_);
  local_sdp_->dtlsRole = ACTPASS;

  Log("Creating sdp offer, isBundle: %d", bundle_);

  if (video_enabled_) {
    forEachMediaStream([this] (const std::shared_ptr<MediaStream> &media_stream) {
      std::vector<uint32_t> video_ssrc_list = std::vector<uint32_t>();
      video_ssrc_list.push_back(media_stream->getVideoSinkSSRC());
      local_sdp_->video_ssrc_map[media_stream->getLabel()] = video_ssrc_list;
    });
  }
  if (audio_enabled_) {
    forEachMediaStream([this] (const std::shared_ptr<MediaStream> &media_stream) {
      local_sdp_->audio_ssrc_map[media_stream->getLabel()] = media_stream->getAudioSinkSSRC();
    });
  }


  auto listener = std::dynamic_pointer_cast<TransportListener>(shared_from_this());
  IceConfig config = ice_config_;
  if (bundle_) {
    config.media_type = VIDEO_TYPE;
    config.transport_name = "video";
    video_transport_.reset(new DtlsTransport(config, bundle_, true, true,
                                            listener, worker_, io_worker_));
    video_transport_->copyLogContextFrom(this);
    video_transport_->start();
  } else {
    if (!video_transport_ && video_enabled_) {
      // For now we don't re/check transports, if they are already created we leave them there
      config.media_type = VIDEO_TYPE;
      config.transport_name = "video";
      video_transport_.reset(new DtlsTransport(ice_config_, bundle_, true, true,
                                              listener, worker_, io_worker_));
      video_transport_->copyLogContextFrom(this);
      video_transport_->start();
    }
    if (!audio_transport_ && audio_enabled_) {
      config.media_type = AUDIO_TYPE;
      config.transport_name = "audio";
      audio_transport_.reset(new DtlsTransport(config, bundle_, true, true,
                                              listener, worker_, io_worker_));
      audio_transport_->copyLogContextFrom(this);
      audio_transport_->start();
    }
  }

  maybeNotifyWebRtcConnectionEvent(global_state_, getLocalSdp());

  return true;
}

void WebRtcConnection::addMediaStream(std::shared_ptr<MediaStream> media_stream) {
  asyncTask([media_stream] (std::shared_ptr<WebRtcConnection> connection) {
    connection->Log("Adding mediaStream, id: %s", media_stream->getId().c_str());
    connection->media_streams_.push_back(media_stream);
  });
}

void WebRtcConnection::removeMediaStream(const std::string& stream_id) {
  asyncTask([stream_id] (std::shared_ptr<WebRtcConnection> connection) {
    AutoLock lock(connection->state_mutex_);
    connection->Log("removing mediaStream, id: %s", stream_id.c_str());
    connection->media_streams_.erase(std::remove_if(connection->media_streams_.begin(),
                                                    connection->media_streams_.end(),
      [stream_id, connection](const std::shared_ptr<MediaStream> &stream) {
        bool isStream = stream->getId() == stream_id;
        if (isStream) {
          auto video_it = connection->local_sdp_->video_ssrc_map.find(stream->getLabel());
          if (video_it != connection->local_sdp_->video_ssrc_map.end()) {
            connection->local_sdp_->video_ssrc_map.erase(video_it);
          }
          auto audio_it = connection->local_sdp_->audio_ssrc_map.find(stream->getLabel());
          if (audio_it != connection->local_sdp_->audio_ssrc_map.end()) {
            connection->local_sdp_->audio_ssrc_map.erase(audio_it);
          }
        }
        return isStream;
      }));
    });
}

void WebRtcConnection::forEachMediaStream(std::function<void(const std::shared_ptr<MediaStream>&)> func) {
  std::for_each(media_streams_.begin(), media_streams_.end(), func);
}

void WebRtcConnection::forEachMediaStreamAsync(std::function<void(const std::shared_ptr<MediaStream>&)> func) {
  std::for_each(media_streams_.begin(), media_streams_.end(),
    [func] (const std::shared_ptr<MediaStream> &stream) {
    stream->asyncTask([func] (const std::shared_ptr<MediaStream> &stream) {
      func(stream);
    });
  });
}

bool WebRtcConnection::setRemoteSdpInfo(std::shared_ptr<SdpInfo> sdp, std::string stream_id) {
  asyncTask([sdp, stream_id] (std::shared_ptr<WebRtcConnection> connection) {
	connection->Log("setting remote SDPInfo");
    if (!connection->sending_) {
      return;
    }

    connection->remote_sdp_ = sdp;
    connection->processRemoteSdp(stream_id);
  });
  return true;
}

std::shared_ptr<SdpInfo> WebRtcConnection::getLocalSdpInfo() {
  AutoLock lock(state_mutex_);
  Log("getting local SDPInfo");
  forEachMediaStream([this] (const std::shared_ptr<MediaStream> &media_stream) {
    if (!media_stream->isRunning() || media_stream->isPublisher()) {
      Log("getting local SDPInfo stream not running, stream_id: %s", media_stream->getId());
      return;
    }
    std::vector<uint32_t> video_ssrc_list;
    if (media_stream->getVideoSinkSSRC() != kDefaultVideoSinkSSRC && media_stream->getVideoSinkSSRC() != 0) {
      video_ssrc_list.push_back(media_stream->getVideoSinkSSRC());
    }
    Log("getting local SDPInfo, stream_id: %s, audio_ssrc: %u", media_stream->getId(), media_stream->getAudioSinkSSRC());
    if (!video_ssrc_list.empty()) {
      local_sdp_->video_ssrc_map[media_stream->getLabel()] = video_ssrc_list;
    }
    if (media_stream->getAudioSinkSSRC() != kDefaultAudioSinkSSRC && media_stream->getAudioSinkSSRC() != 0) {
      local_sdp_->audio_ssrc_map[media_stream->getLabel()] = media_stream->getAudioSinkSSRC();
    }
  });

  bool sending_audio = local_sdp_->audio_ssrc_map.size() > 0;
  bool sending_video = local_sdp_->video_ssrc_map.size() > 0;
  bool receiving_audio = remote_sdp_->audio_ssrc_map.size() > 0;
  bool receiving_video = remote_sdp_->video_ssrc_map.size() > 0;
  Log("audio send %d, recv %d; video send %d recv %d\n", sending_audio, receiving_audio, sending_video, receiving_video);
  if (!sending_audio && receiving_audio) {
    local_sdp_->audioDirection = RECVONLY;
  } else if (sending_audio && !receiving_audio) {
    local_sdp_->audioDirection = SENDONLY;
  } else {
    local_sdp_->audioDirection = SENDRECV;
  }

  if (!sending_video && receiving_video) {
    local_sdp_->videoDirection = RECVONLY;
  } else if (sending_video && !receiving_video) {
    local_sdp_->videoDirection = SENDONLY;
  } else {
    local_sdp_->videoDirection = SENDRECV;
  }

  return local_sdp_;
}

bool WebRtcConnection::setRemoteSdp(const std::string &sdp, std::string stream_id) {
  asyncTask([sdp, stream_id] (std::shared_ptr<WebRtcConnection> connection) {
    connection->Log("setting remote SDP");
    if (!connection->sending_) {
      return;
    }

    connection->remote_sdp_->initWithSdp(sdp, "");
    connection->processRemoteSdp(stream_id);
  });
  return true;
}

void WebRtcConnection::setRemoteSdpsToMediaStreams(std::string stream_id) {
  Log("setting remote SDP, stream: %s", stream_id);

  auto stream = std::find_if(media_streams_.begin(), media_streams_.end(),
    [stream_id](const std::shared_ptr<MediaStream> &media_stream) {
      return media_stream->getId() == stream_id;
    });

  if (stream != media_streams_.end()) {
    std::weak_ptr<WebRtcConnection> weak_this = shared_from_this();
    (*stream)->asyncTask([weak_this, stream_id] (const std::shared_ptr<MediaStream> &media_stream) {
      if (auto connection = weak_this.lock()) {
		// apply remote and local sdp to stream
        media_stream->setRemoteSdp(connection->remote_sdp_);
        connection->Log("setting remote SDP to stream, stream: %s", media_stream->getId());
        connection->onRemoteSdpsSetToMediaStreams(stream_id);
      }
    });
  } else {
    onRemoteSdpsSetToMediaStreams(stream_id);
  }
}

void WebRtcConnection::onRemoteSdpsSetToMediaStreams(std::string stream_id) {
  asyncTask([stream_id] (std::shared_ptr<WebRtcConnection> connection) {
	connection->Log("SDP processed");
    std::string sdp = connection->getLocalSdp();
    connection->maybeNotifyWebRtcConnectionEvent(CONN_SDP_PROCESSED, sdp, stream_id);
  });
}

bool WebRtcConnection::processRemoteSdp(std::string stream_id) {
  Log("processing remote SDP");
  if (first_remote_sdp_processed_) {
    setRemoteSdpsToMediaStreams(stream_id);
    return true;
  }

  bundle_ = remote_sdp_->isBundle;
  local_sdp_->setOfferSdp(remote_sdp_);
  // update extension map
  extension_processor_.setSdpInfo(local_sdp_);
  local_sdp_->updateSupportedExtensionMap(extension_processor_.getSupportedExtensionMap());

  if (remote_sdp_->dtlsRole == ACTPASS) {
    local_sdp_->dtlsRole = ACTIVE;
  }

  audio_enabled_ = remote_sdp_->hasAudio;
  video_enabled_ = remote_sdp_->hasVideo;

  if (remote_sdp_->profile == SAVPF) {
    if (remote_sdp_->isFingerprint) {
      auto listener = std::dynamic_pointer_cast<TransportListener>(shared_from_this());
      if (remote_sdp_->hasVideo || bundle_) {
        if (!video_transport_) {
          Log("Creating videoTransport");
          IceConfig config = ice_config_;
          config.media_type = VIDEO_TYPE;
          config.transport_name = "video";
          video_transport_.reset(new DtlsTransport(config, bundle_, remote_sdp_->isRtcpMux, false,
                                                  listener, worker_, io_worker_));
          video_transport_->copyLogContextFrom(this);
          video_transport_->start();
        }
        
        std::string username = remote_sdp_->getUsername(VIDEO_TYPE);
        std::string password = remote_sdp_->getPassword(VIDEO_TYPE);
        Log("Updating videoTransport, ufrag: %s, pass: %s", username.c_str(), password.c_str());
        video_transport_->getIceConnection()->setRemoteCredentials(username, password);
      }
      if (!bundle_ && remote_sdp_->hasAudio) {
        if (!audio_transport_) {
          Log("Creating audioTransport");
          IceConfig config = ice_config_;
          config.media_type = AUDIO_TYPE;
          config.transport_name = "audio";
          audio_transport_.reset(new DtlsTransport(config, bundle_, remote_sdp_->isRtcpMux, false,
                                                  listener, worker_, io_worker_));
          audio_transport_->copyLogContextFrom(this);
          audio_transport_->start();
        }
        
        std::string username = remote_sdp_->getUsername(AUDIO_TYPE);
        std::string password = remote_sdp_->getPassword(AUDIO_TYPE);
        Log("Update audioTransport, ufrag: %s, pass: %s", username.c_str(), password.c_str());
        audio_transport_->getIceConnection()->setRemoteCredentials(username, password);
      }
    }
  }
  if (getCurrentState() >= CONN_GATHERED) {
    if (!remote_sdp_->getCandidateInfos().empty()) {
      Log("Setting remote candidates after gathered");
      if (remote_sdp_->hasVideo) {
        video_transport_->setRemoteCandidates(remote_sdp_->getCandidateInfos(), bundle_);
      }
      if (!bundle_ && remote_sdp_->hasAudio) {
        audio_transport_->setRemoteCandidates(remote_sdp_->getCandidateInfos(), bundle_);
      }
    }
  }
  setRemoteSdpsToMediaStreams(stream_id);
  first_remote_sdp_processed_ = true;
  return true;
}


bool WebRtcConnection::addRemoteCandidate(const std::string &mid, int mLineIndex, const std::string &sdp) {
  // TODO(pedro) Check type of transport.
  Log("Adding remote Candidate, candidate: %s, mid: %s, sdpMLine: %d", sdp.c_str(), mid.c_str(), mLineIndex);
  if (!video_transport_ && !audio_transport_) {
    Warn("addRemoteCandidate on NULL transport");
    return false;
  }
  MediaType theType;
  std::string theMid;

  // TODO(pedro) check if this works with video+audio and no bundle
  if (mLineIndex == -1) {
    Log("All candidates received");
    if (video_transport_) {
      video_transport_->getIceConnection()->setReceivedLastCandidate(true);
    } else if (audio_transport_) {
      audio_transport_->getIceConnection()->setReceivedLastCandidate(true);
    }
    return true;
  }

  if ((!mid.compare("video")) || (mLineIndex == remote_sdp_->videoSdpMLine)) {
    theType = VIDEO_TYPE;
    theMid = "video";
  } else {
    theType = AUDIO_TYPE;
    theMid = "audio";
  }

  bool res = false;
  SdpInfo tempSdp(rtp_mappings_);
  std::string username = remote_sdp_->getUsername(theType);
  std::string password = remote_sdp_->getPassword(theType);
  tempSdp.setCredentials(username, password, OTHER);
  if (tempSdp.initWithSdp(sdp, theMid)) {
    if (theType == VIDEO_TYPE || bundle_) {
      res = video_transport_->setRemoteCandidates(tempSdp.getCandidateInfos(), bundle_);
    } else if (theType == AUDIO_TYPE) {
      res = audio_transport_->setRemoteCandidates(tempSdp.getCandidateInfos(), bundle_);
    } else {
      Warn("add remote candidate with no Media (video or audio), candidate: %s", sdp.c_str() );
    }
  }

  for (uint8_t it = 0; it < tempSdp.getCandidateInfos().size(); it++) {
    remote_sdp_->addCandidate(tempSdp.getCandidateInfos()[it]);
  }
  return res;
}

std::string WebRtcConnection::getLocalSdp() {
  Log("Getting Local Sdp");
  if (video_transport_ && getCurrentState() != CONN_READY) {
    video_transport_->processLocalSdp(local_sdp_.get());
  }
  if (!bundle_ && audio_transport_ && getCurrentState() != CONN_READY) {
    audio_transport_->processLocalSdp(local_sdp_.get());
  }
  local_sdp_->profile = remote_sdp_->profile;
  return local_sdp_->getSdp();
}

std::string WebRtcConnection::getJSONCandidate(const std::string& mid, const std::string& sdp) {
  std::map <std::string, std::string> object;
  object["sdpMid"] = mid;
  object["candidate"] = sdp;
  object["sdpMLineIndex"] = std::to_string((mid.compare("video")?local_sdp_->audioSdpMLine : local_sdp_->videoSdpMLine));

  std::ostringstream theString;
  theString << "{";
  for (std::map<std::string, std::string>::const_iterator it = object.begin(); it != object.end(); ++it) {
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
  std::string sdp = local_sdp_->addCandidate(cand);
  Log("Discovered New Candidate, candidate: %s", sdp.c_str());
  if (ice_config_.should_trickle) {
    if (!bundle_) {
      std::string object = getJSONCandidate(transport->transport_name(), sdp);
      maybeNotifyWebRtcConnectionEvent(CONN_CANDIDATE, object);
    } else {
      if (remote_sdp_->hasAudio) {
        std::string object = getJSONCandidate("audio", sdp);
        maybeNotifyWebRtcConnectionEvent(CONN_CANDIDATE, object);
      }
      if (remote_sdp_->hasVideo) {
        std::string object2 = getJSONCandidate("video", sdp);
        maybeNotifyWebRtcConnectionEvent(CONN_CANDIDATE, object2);
      }
    }
  }
}

void WebRtcConnection::onREMBFromTransport(RtcpHeader *chead, Transport *transport) {
  std::vector<std::shared_ptr<MediaStream>> streams;
  for (uint8_t index = 0; index < chead->getREMBNumSSRC(); index++) {
    uint32_t ssrc_feed = chead->getREMBFeedSSRC(index);
    forEachMediaStream([ssrc_feed, &streams] (const std::shared_ptr<MediaStream> &media_stream) {
      if (media_stream->isSinkSSRC(ssrc_feed)) {
        streams.push_back(media_stream);
      }
    });
  }

  distributor_->distribute(chead->getREMBBitRate(), chead->getSSRC(), streams, transport);
}

void WebRtcConnection::onRtcpFromTransport(packetPtr packet, Transport *transport) {
  RtpUtils::forEachRtcpBlock(packet, [this, packet, transport](RtcpHeader *chead) {
    if (chead->isREMB()) {
      onREMBFromTransport(chead, transport);
      return;
    }
	uint32_t ssrc = chead->isFeedback() ? chead->getSourceSSRC() : chead->getSSRC();
	packetPtr rtcp = std::make_shared<DataPacket>(packet->comp, (unsigned char*)chead, chead->getSize());
    forEachMediaStream([rtcp, transport, ssrc] (const std::shared_ptr<MediaStream> &media_stream) {
      if (media_stream->isSourceSSRC(ssrc) || media_stream->isSinkSSRC(ssrc)) {
        media_stream->onTransportData(rtcp, transport);
      }
    });
  });
}

void WebRtcConnection::onTransportData(packetPtr packet, Transport *transport) {
  if (getCurrentState() != CONN_READY) {
    return;
  }
  char* buf = packet->data;
  RtcpHeader *chead = reinterpret_cast<RtcpHeader*> (buf);
  if (chead->isRtcp()) {
    onRtcpFromTransport(packet, transport);
    return;
  } else {
    RtpHeader *head = reinterpret_cast<RtpHeader*> (buf);
    uint32_t ssrc = head->getSSRC();
    forEachMediaStream([packet, transport, ssrc] (const std::shared_ptr<MediaStream> &media_stream) {
      if (media_stream->isSourceSSRC(ssrc) || media_stream->isSinkSSRC(ssrc)) {
        media_stream->onTransportData(packet, transport);
      }
    });
  }
}

void WebRtcConnection::maybeNotifyWebRtcConnectionEvent(const WebRTCEvent& event, const std::string& message, const std::string& stream_id) {
  AutoLock lock(event_listener_mutex_);
  if (conn_event_listener_) {
    conn_event_listener_->notifyEvent(event, message, stream_id);
  }
}

void WebRtcConnection::setWebRtcConnectionEventListener(WebRtcConnectionEventListener* listener) {
	AutoLock lock(event_listener_mutex_);
	conn_event_listener_ = listener;
}

void WebRtcConnection::asyncTask(std::function<void(std::shared_ptr<WebRtcConnection>)> f) {
  std::weak_ptr<WebRtcConnection> weak_this = shared_from_this();
  worker_->task([weak_this, f] {
    if (auto this_ptr = weak_this.lock()) {
      f(this_ptr);
    }
  });
}

void WebRtcConnection::updateState(TransportState state, Transport * transport) {
  Log("updateState new_state: %d", state);
  AutoLock lock(state_mutex_);
  WebRTCEvent temp = global_state_;
  std::string msg;
  if (!video_transport_ && !audio_transport_) {
    Warn("Updating NULL transport, state: %d", state);
    return;
  }
  if (global_state_ == CONN_FAILED) {
    // if current state is failed -> noop
    return;
  }
  switch (state) {
    case TRANSPORT_STARTED:
      if (bundle_) {
        temp = CONN_STARTED;
      } else {
        if ((!remote_sdp_->hasAudio || (audio_transport_ && audio_transport_->getTransportState() == TRANSPORT_STARTED)) &&
            (!remote_sdp_->hasVideo || (video_transport_ && video_transport_->getTransportState() == TRANSPORT_STARTED))) {
            // WebRTCConnection will be ready only when all channels are ready.
            temp = CONN_STARTED;
          }
      }
      break;
    case TRANSPORT_GATHERED:
      if (bundle_) {
        if (!remote_sdp_->getCandidateInfos().empty()) {
          // Passing now new candidates that could not be passed before
          if (remote_sdp_->hasVideo) {
            video_transport_->setRemoteCandidates(remote_sdp_->getCandidateInfos(), bundle_);
          }
          if (!bundle_ && remote_sdp_->hasAudio) {
            audio_transport_->setRemoteCandidates(remote_sdp_->getCandidateInfos(), bundle_);
          }
        }
        if (!ice_config_.should_trickle) {
          temp = CONN_GATHERED;
          msg = this->getLocalSdp();
        }
      } else {
        if ((!local_sdp_->hasAudio || (audio_transport_ && audio_transport_->getTransportState() == TRANSPORT_GATHERED)) &&
            (!local_sdp_->hasVideo || (video_transport_ && video_transport_->getTransportState() == TRANSPORT_GATHERED))) {
            // WebRTCConnection will be ready only when all channels are ready.
            if (!ice_config_.should_trickle) {
              temp = CONN_GATHERED;
              msg = this->getLocalSdp();
            }
          }
      }
      break;
    case TRANSPORT_READY:
      if (bundle_) {
        temp = CONN_READY;
        trackTransportInfo();
        forEachMediaStreamAsync([] (const std::shared_ptr<MediaStream> &media_stream) {
          media_stream->sendPLIToFeedback();
        });
      } else {
        if ((!remote_sdp_->hasAudio || (audio_transport_ && audio_transport_->getTransportState() == TRANSPORT_READY)) &&
            (!remote_sdp_->hasVideo || (video_transport_ && video_transport_->getTransportState() == TRANSPORT_READY))) {
            // WebRTCConnection will be ready only when all channels are ready.
            temp = CONN_READY;
            trackTransportInfo();
            forEachMediaStreamAsync([] (const std::shared_ptr<MediaStream> &media_stream) {
              media_stream->sendPLIToFeedback();
            });
          }
      }
      break;
    case TRANSPORT_FAILED:
      temp = CONN_FAILED;
      sending_ = false;
      msg = remote_sdp_->getSdp();
      Warn("Transport Failed");
      cond_.notify_one();
      break;
    default:
      Log("do nothing on state, state %d", state);
      break;
  }

  if (audio_transport_ && video_transport_) {
    Log("Update Transport State transportName: %s, videoTransportState: %d , audioTransportState: %d, calculatedState: %d, globalState: %d",
      transport->transport_name(),
      static_cast<int>(audio_transport_->getTransportState()),
      static_cast<int>(video_transport_->getTransportState()),
      static_cast<int>(temp),
      static_cast<int>(global_state_));
  }

  if (global_state_ == temp) {
    return;
  }
  global_state_ = temp;
  Info("newGlobalState: %d", global_state_);
  maybeNotifyWebRtcConnectionEvent(global_state_, msg);
}

void WebRtcConnection::trackTransportInfo() {
  CandidatePair candidate_pair;
  std::string audio_info;
  std::string video_info;
  if (video_enabled_ && video_transport_) {
    candidate_pair = video_transport_->getIceConnection()->getSelectedPair();
    video_info = candidate_pair.clientHostType;
  }

  if (audio_enabled_ && audio_transport_) {
    candidate_pair = audio_transport_->getIceConnection()->getSelectedPair();
    audio_info = candidate_pair.clientHostType;
  }

  asyncTask([audio_info, video_info] (std::shared_ptr<WebRtcConnection> connection) {
    connection->forEachMediaStreamAsync(
      [audio_info, video_info] (const std::shared_ptr<MediaStream> &media_stream) {
        media_stream->setTransportInfo(audio_info, video_info);
      });
  });
}

void WebRtcConnection::setMetadata(std::map<std::string, std::string> metadata) {
  setLogContext(metadata);
}

WebRTCEvent WebRtcConnection::getCurrentState() {
  return global_state_;
}

void WebRtcConnection::write(packetPtr packet) {
  asyncTask([packet] (std::shared_ptr<WebRtcConnection> connection) {
    connection->syncWrite(packet);
  });
}

void WebRtcConnection::syncWrite(packetPtr packet) {
  if (!sending_) {
    return;
  }
  Transport *transport = (bundle_ || packet->type == VIDEO_PACKET) ? video_transport_.get() : audio_transport_.get();
  if (transport) {
    extension_processor_.processRtpExtensions(packet);
    transport->write(packet->data, packet->length);
  }
}

void WebRtcConnection::setTransport(std::shared_ptr<Transport> transport) {  // Only for Testing purposes
  video_transport_ = std::move(transport);
  bundle_ = true;
}

}  // namespace erizo
