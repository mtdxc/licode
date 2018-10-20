/*
 * DtlsConnection.cpp
 */
#include "rtp/RtpHeaders.h"
#include "DtlsTransport.h"

#include <string>
#include <cstring>
#include <memory>

#include "./SrtpChannel.h"
#include "./LibNiceConnection.h"
#ifdef HAS_NICER
#include "./NicerConnection.h"
#endif
using namespace erizo;

DEFINE_LOGGER(DtlsTransport, "DtlsTransport");
DEFINE_LOGGER(TimeoutChecker, "TimeoutChecker");

using std::memcpy;
using namespace dtls;
static std::mutex dtls_mutex;

TimeoutChecker::TimeoutChecker(DtlsTransport* transport, dtls::DtlsSocketContext* ctx)
    : transport_(transport), socket_context_(ctx),
      check_seconds_(kInitialSecsPerTimeoutCheck), max_checks_(kMaxTimeoutChecks),
      scheduled_task_{0} {
}

TimeoutChecker::~TimeoutChecker() {
  cancel();
}

void TimeoutChecker::cancel() {
  if (scheduled_task_) {
    transport_->getWorker()->unschedule(scheduled_task_);
    scheduled_task_ = 0;
  }
}

void TimeoutChecker::scheduleCheck() {
  ELOG_TRACE("message: Scheduling a new TimeoutChecker");
  cancel();
  check_seconds_ = kInitialSecsPerTimeoutCheck;
  if (transport_->getTransportState() != TRANSPORT_READY) {
    scheduleNext();
  }
}

void TimeoutChecker::scheduleNext() {
  scheduled_task_ = transport_->getWorker()->scheduleFromNow([this]() {
      if (transport_->getTransportState() == TRANSPORT_READY) {
        return;
      }
      if (transport_ != nullptr) {
        if (max_checks_-- > 0) {
          ELOG_DEBUG("Handling dtls timeout, checks left: %d", max_checks_);
          if (socket_context_) {
            std::lock_guard<std::mutex> guard(dtls_mutex);
            socket_context_->handleTimeout();
          }
          scheduleNext();
        } else {
          ELOG_DEBUG("DTLS timeout");
          transport_->onHandshakeFailed(socket_context_, "Dtls Timeout on TimeoutChecker");
        }
      }
  }, std::chrono::seconds(check_seconds_));
}

DtlsTransport::DtlsTransport(const IceConfig& iceConfig, bool bundle, bool rtcp_mux, bool isServer, 
                            std::weak_ptr<TransportListener> transport_listener,
                            std::shared_ptr<Worker> worker, std::shared_ptr<IOWorker> io_worker):
  Transport(iceConfig, bundle, rtcp_mux, transport_listener, worker, io_worker),
  readyRtp(false), readyRtcp(false), isServer_(isServer) {
    Debug("constructor isBundle: %d", bundle);
    dtlsRtp.reset(new DtlsSocketContext());

    int comps = 1;
    if (isServer_) {
      Debug("creating  passive-server");
      dtlsRtp->createServer();
      dtlsRtp->setDtlsReceiver(this);

      if (!rtcp_mux) {
        comps = 2;
        dtlsRtcp.reset(new DtlsSocketContext());
        dtlsRtcp->createServer();
        dtlsRtcp->setDtlsReceiver(this);
      }
    } else {
      Debug("creating active-client");
      dtlsRtp->createClient();
      dtlsRtp->setDtlsReceiver(this);

      if (!rtcp_mux) {
        comps = 2;
        dtlsRtcp.reset(new DtlsSocketContext());
        dtlsRtcp->createClient();
        dtlsRtcp->setDtlsReceiver(this);
      }
    }
    iceConfig_.ice_components = comps;
    if (iceConfig_.use_nicer) {
#ifdef HAS_NICER
      ice_ = NicerConnection::create(io_worker_, iceConfig_);
#endif
    } else {
      ice_.reset(LibNiceConnection::create(iceConfig_));
    }
    rtp_timeout_checker_.reset(new TimeoutChecker(this, dtlsRtp.get()));
    if (!rtcp_mux) {
      rtcp_timeout_checker_.reset(new TimeoutChecker(this, dtlsRtcp.get()));
    }
    Debug("created");
  }

DtlsTransport::~DtlsTransport() {
  if (this->state_ != TRANSPORT_FINISHED) {
    this->close();
  }
}

void DtlsTransport::start() {
  ice_->setIceListener(shared_from_this());
  ice_->copyLogContextFrom(this);
  Debug("starting ice");
  ice_->start();
}

void DtlsTransport::close() {
  Debug("closing");
  running_ = false;
  if (rtp_timeout_checker_) {
    rtp_timeout_checker_->cancel();
  }
  if (rtcp_timeout_checker_) {
    rtcp_timeout_checker_->cancel();
  }
  ice_->close();
  if (dtlsRtp) {
    dtlsRtp->close();
  }
  if (dtlsRtcp) {
    dtlsRtcp->close();
  }
  this->state_ = TRANSPORT_FINISHED;
  Debug("closed");
}

void DtlsTransport::onIceData(packetPtr packet) {
  if (!running_) {
    return;
  }
  int len = packet->length;
  char *data = packet->data;
  unsigned int component_id = packet->comp;

  int length = len;
  SrtpChannel *srtp = srtp_.get();
  if (DtlsTransport::isDtlsPacket(data, len)) {
    Debug("Received DTLS message componentId: %u", component_id);
    if (component_id == 1) {
      std::lock_guard<std::mutex> guard(dtls_mutex);
      dtlsRtp->read(reinterpret_cast<unsigned char*>(data), len);
    } else {
      std::lock_guard<std::mutex> guard(dtls_mutex);
      dtlsRtcp->read(reinterpret_cast<unsigned char*>(data), len);
    }
    return;
  } else if (this->getTransportState() == TRANSPORT_READY) {
    packetPtr unprotect_packet = std::make_shared<DataPacket>(component_id,
      data, len, VIDEO_PACKET, packet->received_time_ms);

    if (dtlsRtcp != NULL && component_id == 2) {
      srtp = srtcp_.get();
    }
    if (srtp != NULL) {
      RtcpHeader *chead = reinterpret_cast<RtcpHeader*>(unprotect_packet->data);
      if (chead->isRtcp()) {
        if (srtp->unprotectRtcp(unprotect_packet->data, &unprotect_packet->length) < 0) {
          return;
        }
      } else {
        if (srtp->unprotectRtp(unprotect_packet->data, &unprotect_packet->length) < 0) {
          return;
        }
      }
    } else {
      return;
    }

    if (length <= 0) {
      return;
    }
    if (auto listener = getTransportListener().lock()) {
      listener->onTransportData(unprotect_packet, this);
    }
  }
}

void DtlsTransport::onCandidate(const CandidateInfo &candidate, IceConnection *conn) {
  if (auto listener = getTransportListener().lock()) {
    listener->onCandidate(candidate, this);
  }
}

void DtlsTransport::write(char* data, int len) {
  if (!ice_ || !running_ || !data || !len) {
    return;
  }

  SrtpChannel *srtp = srtp_.get();
  if (getTransportState() == TRANSPORT_READY) {
    memcpy(protectBuf_, data, len);
    int comp = 1;
    RtcpHeader *chead = reinterpret_cast<RtcpHeader*>(protectBuf_);
    if (chead->isRtcp()) {
      if (!rtcp_mux_) {
        comp = 2;
      }
      if (dtlsRtcp != NULL) {
        srtp = srtcp_.get();
      }
      if (srtp && ice_->getIceState() == IceState::READY) {
        if (srtp->protectRtcp(protectBuf_, &len) < 0) {
          return;
        }
      }
    } else {
      comp = 1;

      if (srtp && ice_->getIceState() == IceState::READY) {
        if (srtp->protectRtp(protectBuf_, &len) < 0) {
          return;
        }
      }
    }
    if (len <= 10) {
      return;
    }
    if (ice_->getIceState() == IceState::READY) {
      writeOnIce(comp, protectBuf_, len);
    }
  }
}

void DtlsTransport::onDtlsPacket(DtlsSocketContext *ctx, const unsigned char* data, unsigned int len) {
  bool is_rtcp = ctx == dtlsRtcp.get();
  int component_id = is_rtcp ? 2 : 1;
  writeOnIce(component_id, (void*)data, len);
  Debug("Sending DTLS message, componentId: %d", component_id);
}

void DtlsTransport::onHandshakeCompleted(DtlsSocketContext *ctx, std::string clientKey, std::string serverKey,
                                         std::string srtp_profile) {
  AutoLock lock(sessionMutex_);
  if (rtp_timeout_checker_) {
    rtp_timeout_checker_->cancel();
  }
  if (rtcp_timeout_checker_) {
    rtcp_timeout_checker_->cancel();
  }

  if (isServer_) {  // If we are server, we swap the keys
    Debug("swapping keys, isServer: %d", isServer_);
    clientKey.swap(serverKey);
  }
  if (ctx == dtlsRtp.get()) {
    srtp_.reset(new SrtpChannel());
    if (srtp_->setRtpParams(clientKey, serverKey)) {
      readyRtp = true;
    } else {
      updateTransportState(TRANSPORT_FAILED);
    }
    if (dtlsRtcp == NULL) {
      readyRtcp = true;
    }
  }
  if (ctx == dtlsRtcp.get()) {
    srtcp_.reset(new SrtpChannel());
    if (srtcp_->setRtpParams(clientKey, serverKey)) {
      readyRtcp = true;
    } else {
      updateTransportState(TRANSPORT_FAILED);
    }
  }
  Debug("HandShakeCompleted, readyRtp:%d, readyRtcp:%d", readyRtp, readyRtcp);
  if (readyRtp && readyRtcp) {
    updateTransportState(TRANSPORT_READY);
  }
}

void DtlsTransport::onHandshakeFailed(DtlsSocketContext *ctx, const std::string& error) {
  Warn("Handshake failed: %s", error.c_str());
  running_ = false;
  updateTransportState(TRANSPORT_FAILED);
}

std::string DtlsTransport::getMyFingerprint() const {
  return dtlsRtp->getFingerprint();
}

void DtlsTransport::updateIceState(IceState state, IceConnection *conn) {
  std::weak_ptr<Transport> weak_transport = Transport::shared_from_this();
  worker_->task([weak_transport, state, conn, this]() {
    if (auto transport = weak_transport.lock()) {
      updateIceStateSync(state, conn);
    }
  });
}

void DtlsTransport::updateIceStateSync(IceState state, IceConnection *conn) {
  if (!running_) {
    return;
  }
  Debug("IceState state: %d, isBundle: %d", state, bundle_);
  if (state == IceState::INITIAL && this->getTransportState() != TRANSPORT_STARTED) {
    updateTransportState(TRANSPORT_STARTED);
  } else if (state == IceState::CANDIDATES_RECEIVED && this->getTransportState() != TRANSPORT_GATHERED) {
    updateTransportState(TRANSPORT_GATHERED);
  } else if (state == IceState::FAILED) {
    Debug("Ice Failed");
    running_ = false;
    updateTransportState(TRANSPORT_FAILED);
  } else if (state == IceState::READY) {
    if (!isServer_ && dtlsRtp && !dtlsRtp->started) {
      Info("DTLSRTP Start");
      dtlsRtp->start();
      rtp_timeout_checker_->scheduleCheck();
    }
    if (!isServer_ && dtlsRtcp != NULL && !dtlsRtcp->started) {
      Debug("DTLSRTCP Start");
      dtlsRtcp->start();
      rtcp_timeout_checker_->scheduleCheck();
    }
  }
}

void DtlsTransport::processLocalSdp(SdpInfo *localSdp_) {
  Debug("processing local sdp");
  localSdp_->isFingerprint = true;
  localSdp_->fingerprint = getMyFingerprint();
  std::string username(ice_->getLocalUsername());
  std::string password(ice_->getLocalPassword());
  if (bundle_) {
    localSdp_->setCredentials(username, password, VIDEO_TYPE);
    localSdp_->setCredentials(username, password, AUDIO_TYPE);
  } else {
    localSdp_->setCredentials(username, password, media_type());
  }
  Debug("processed local sdp ufrag: %s, pass: %s", username.c_str(), password.c_str());
}

bool DtlsTransport::isDtlsPacket(const char* buf, int len) {
  int data = DtlsSocketContext::demuxPacket(reinterpret_cast<const unsigned char*>(buf), len);
  switch (data) {
    case DtlsSocketContext::dtls:
      return true;
      break;
    default:
      return false;
      break;
  }
}
