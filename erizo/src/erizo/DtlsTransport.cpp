/*
 * DtlsConnection.cpp
 */

#include "DtlsTransport.h"

#include <string>
#include <cstring>
#include <memory>

#include "SrtpChannel.h"
#include "rtp/RtpHeaders.h"
#include "lib/LibNiceInterface.h"

using erizo::Resender;
using erizo::DtlsTransport;
using dtls::DtlsSocketContext;

DEFINE_LOGGER(DtlsTransport, "DtlsTransport");
DEFINE_LOGGER(Resender, "Resender");

using std::memcpy;
namespace erizo{
Resender::Resender(DtlsTransport* transport, dtls::DtlsSocketContext* ctx)
    : transport_(transport), socket_context_(ctx),
      resend_seconds_(kInitialSecsPerResend), max_resends_(kMaxResends) {
}

Resender::~Resender() {
  cancel();
}

void Resender::cancel() {
  if (-1!=scheduled_task_) {
    transport_->getWorker()->unschedule(scheduled_task_);
    scheduled_task_ = -1;
  }
}

void Resender::scheduleResend(packetPtr packet,unsigned int resend) {
  ELOG_DEBUG("message: Scheduling a new resender");
  transport_->getWorker()->unschedule(scheduled_task_);
  resend_seconds_ = kInitialSecsPerResend;
  max_resends_ = resend;
  packet_ = packet;
  transport_->writeDtlsPacket(socket_context_, packet_);
  if (transport_->getTransportState() != TRANSPORT_READY) {
    scheduleNext();
  }
}

void Resender::resend() {
  if (transport_ != nullptr) {
    if (max_resends_-- > 0) {
      transport_->Info("Resending DTLS message");
      transport_->writeDtlsPacket(socket_context_, packet_);
      scheduleNext();
    } else {
      transport_->Info("DTLS timeout");
      transport_->onHandshakeFailed(socket_context_, "Dtls Timeout on Resender");
    }
  }
}

void Resender::scheduleNext() {
  scheduled_task_ = transport_->getWorker()->scheduleFromNow([this]() {
    resend();
  }, std::chrono::seconds(resend_seconds_));
  // 指数退避
  resend_seconds_ = resend_seconds_ * 2;
}

DtlsTransport::DtlsTransport(MediaType med, const std::string &transport_name, const std::string& connection_id,
                            bool bundle, bool rtcp_mux, std::weak_ptr<TransportListener> transport_listener,
                            const IceConfig& iceConfig, bool isServer, std::shared_ptr<Worker> worker):
  Transport(med, transport_name, connection_id, bundle, rtcp_mux, transport_listener, iceConfig, worker),
  unprotect_packet_{std::make_shared<dataPacket>()},
  readyRtp(false), readyRtcp(false), isServer_(isServer) {
  set_log_context("%s.%s", connection_id.c_str(), bundle?"bundle":transport_name.c_str());
  Info("constructor isBundle: %d", bundle);
  dtlsRtp.reset(new DtlsSocketContext());
  int comps = 1;
  if (isServer_) {
    Info("creating passive-server");
    dtlsRtp->createServer();
    dtlsRtp->setDtlsReceiver(this);

    if (!rtcp_mux) {
      comps = 2;
      dtlsRtcp.reset(new DtlsSocketContext());
      dtlsRtcp->createServer();
      dtlsRtcp->setDtlsReceiver(this);
    }
  } else {
    Info("creating active-client");
    dtlsRtp->createClient();
    dtlsRtp->setDtlsReceiver(this);

    if (!rtcp_mux) {
      comps = 2;
      dtlsRtcp.reset(new DtlsSocketContext());
      dtlsRtcp->createClient();
      dtlsRtcp->setDtlsReceiver(this);
    }
  }
  nice_.reset(new NiceConnection(std::make_shared<LibNiceInterfaceImpl>(), med,
        transport_name, connection_id_, this, comps, iceConfig_));
  //nice_->set_log_context("Nice%s(%s)", bundle_ ? "B" : (med == AUDIO_TYPE ? "A" : "V"), connection_id_.c_str());
  nice_->set_log_context(printLogContext());
  rtp_resender_.reset(new Resender(this, dtlsRtp.get()));
  if (!rtcp_mux) {
    rtcp_resender_.reset(new Resender(this, dtlsRtcp.get()));
  }

  Info("created");
}

DtlsTransport::~DtlsTransport() {
  Info("destroying");
  if (state_ != TRANSPORT_FINISHED) {
    close();
  }
  Info("destroyed");
}

void DtlsTransport::start() {
  nice_->copyLogContextFrom(this);
  Info("starting ice");
  nice_->start();
}

void DtlsTransport::close() {
  Info("closing");
  running_ = false;
  nice_->close();
  if (dtlsRtp) {
    dtlsRtp->close();
  }
  if (dtlsRtcp) {
    dtlsRtcp->close();
  }
  if (rtp_resender_) {
    rtp_resender_->cancel();
  }
  if (rtcp_resender_) {
    rtcp_resender_->cancel();
  }
  this->state_ = TRANSPORT_FINISHED;
  Info("closed");
}

void DtlsTransport::onNiceData(packetPtr packet) {
  if (packet->length < 0) return;
  int len = packet->length;
  char *data = packet->data;
  unsigned int component_id = packet->comp;
  if (DtlsTransport::isDtlsPacket(data, len)) {
    Info("Received DTLS message componentId: %u", component_id);
    if (component_id == 1) {
      if (rtp_resender_.get() != NULL) {
        rtp_resender_->cancel();
      }
      dtlsRtp->read(reinterpret_cast<unsigned char*>(data), len);
    } else {
      if (rtcp_resender_.get() != NULL) {
        rtcp_resender_->cancel();
      }
      dtlsRtcp->read(reinterpret_cast<unsigned char*>(data), len);
    }
  } else if (getTransportState() == TRANSPORT_READY) {
    unprotect_packet_->length = len;
    unprotect_packet_->received_time_ms = packet->received_time_ms;
    memcpy(unprotect_packet_->data, data, len);

    SrtpChannel *srtp = srtp_.get();
    if (dtlsRtcp != NULL && component_id == 2) {
      srtp = srtcp_.get();
    }
    if (srtp != NULL) {
      RtcpHeader *chead = reinterpret_cast<RtcpHeader*>(unprotect_packet_->data);
      if (chead->isRtcp()) {
        if (srtp->unprotectRtcp(unprotect_packet_->data, &unprotect_packet_->length) < 0) {
          return;
        }
      } else {
        if (srtp->unprotectRtp(unprotect_packet_->data, &unprotect_packet_->length) < 0) {
          return;
        }
      }
    }

    if (auto listener = getTransportListener().lock()) {
      listener->onTransportData(unprotect_packet_, this);
    }
  }
}

void DtlsTransport::onCandidate(const CandidateInfo &candidate, NiceConnection *conn) {
  if (auto listener = getTransportListener().lock()) {
    listener->onCandidate(candidate, this);
}
}

void DtlsTransport::write(char* data, int len) {
  if (nice_ == nullptr || !running_) {
    Info("nice %p %d drop %d data", nice_.get(), running_, len);
    return;
  }
  int length = len;
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
      if (srtp && nice_->getIceState() == NICE_READY) {
        if (srtp->protectRtcp(protectBuf_, &length) < 0) {
          return;
        }
      }
    } else {
      comp = 1;

      if (srtp && nice_->getIceState() == NICE_READY) {
        if (srtp->protectRtp(protectBuf_, &length) < 0) {
          return;
        }
      }
    }
    if (length <= 10) {
      Info("packet too small drop %d data", len);
      return;
    }
    if (nice_->getIceState() == NICE_READY) {
      if (srtp)
        writeOnNice(comp, protectBuf_, length);
      else
        writeOnNice(comp, data, len);
    }
  }
}

void DtlsTransport::onDtlsPacket(DtlsSocketContext *ctx, const unsigned char* data, unsigned int len) {
  bool is_rtcp = (ctx == dtlsRtcp.get());
  int component_id = is_rtcp ? 2 : 1;
  // dtls message must trigle resend
  packetPtr packet = std::make_shared<dataPacket>(component_id, data, len);
  if (is_rtcp) {
    rtcp_resender_->scheduleResend(packet);
  } else {
    rtp_resender_->scheduleResend(packet);
  }

  Info("Sending DTLS message, componentId: %d", packet->comp);
}

void DtlsTransport::writeDtlsPacket(DtlsSocketContext *ctx, packetPtr packet) {
  writeOnNice(packet->comp, packet->data, packet->length);
}

void DtlsTransport::onHandshakeCompleted(DtlsSocketContext *ctx, std::string clientKey, std::string serverKey,
                                         std::string srtp_profile) {
  std::lock_guard<std::mutex> lock(sessionMutex_);
  std::string temp;

  if (isServer_) {  // If we are server, we swap the keys
    Info("swapping keys, isServer: %d", isServer_);
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
  Info("HandShakeCompleted, readyRtp:%d, readyRtcp:%d", readyRtp, readyRtcp);
  if (readyRtp && readyRtcp) {
    // 当没使用加密时，也要调用此函数改变状态
    updateTransportState(TRANSPORT_READY);
  }
}

void DtlsTransport::onHandshakeFailed(DtlsSocketContext *ctx, const std::string error) {
  Info("Handshake failed, transportName:%s, error: %s", transport_name.c_str(), error.c_str());
  running_ = false;
  updateTransportState(TRANSPORT_FAILED);
}

std::string DtlsTransport::getMyFingerprint() {
  return dtlsRtp->getFingerprint();
}

void DtlsTransport::updateIceState(IceState state, NiceConnection *conn) {
  Info("updateIceState state: %d, isBundle: %d", state, bundle_);
  if (state == NICE_INITIAL && getTransportState() != TRANSPORT_STARTED) {
    updateTransportState(TRANSPORT_STARTED);
  } else if (state == NICE_CANDIDATES_RECEIVED && getTransportState() != TRANSPORT_GATHERED) {
    updateTransportState(TRANSPORT_GATHERED);
  } else if (state == NICE_FAILED) {
    Info("Nice Failed");
    running_ = false;
    updateTransportState(TRANSPORT_FAILED);
  } else if (state == NICE_READY) {
    if (!isServer_ && dtlsRtp && !dtlsRtp->started) {
      Info("DTLS Rtp Start");
      dtlsRtp->start();
    }
    if (!isServer_ && dtlsRtcp && !dtlsRtcp->started) {
      Info("DTLS Rtcp Start");
      dtlsRtcp->start();
    }
  }
}

void DtlsTransport::processLocalSdp(SdpInfo *localSdp_) {
  localSdp_->isFingerprint = true;
  localSdp_->fingerprint = getMyFingerprint();
  std::string username = nice_->getLocalUsername();
  std::string password = nice_->getLocalPassword();
  if (bundle_) {
    localSdp_->setCredentials(username, password, VIDEO_TYPE);
    localSdp_->setCredentials(username, password, AUDIO_TYPE);
  } else {
    localSdp_->setCredentials(username, password, mediaType);
  }
  Info("processLocalSdp ufrag: %s, pass: %s", username.c_str(), password.c_str());
}

bool DtlsTransport::isDtlsPacket(const char* buf, int len) {
  return (DtlsSocketContext::dtls == DtlsSocketContext::demuxPacket(reinterpret_cast<const unsigned char*>(buf), len));
}
}