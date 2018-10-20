#ifndef ERIZO_SRC_ERIZO_TRANSPORT_H_
#define ERIZO_SRC_ERIZO_TRANSPORT_H_

#include <string>
#include <vector>
#include <cstdio>
#include "IceConnection.h"
#include "thread/Worker.h"
#include "thread/IOWorker.h"
#include "./logger.h"

/**
 * States of Transport
 */
enum TransportState {
  TRANSPORT_INITIAL, TRANSPORT_STARTED, TRANSPORT_GATHERED, TRANSPORT_READY, TRANSPORT_FINISHED, TRANSPORT_FAILED
};

namespace erizo {
class Transport;

class TransportListener {
 public:
  virtual void onTransportData(packetPtr packet, Transport *transport) = 0;
  virtual void updateState(TransportState state, Transport *transport) = 0;
  virtual void onCandidate(const CandidateInfo& cand, Transport *transport) = 0;
};

class Transport : public std::enable_shared_from_this<Transport>, 
  public IceConnectionListener, public LogContext {
 public:  
  Transport(const IceConfig& iceConfig, bool bundle, bool rtcp_mux,
    std::weak_ptr<TransportListener> transport_listener, std::shared_ptr<Worker> worker, std::shared_ptr<IOWorker> io_worker) 
    : iceConfig_(iceConfig), state_(TRANSPORT_INITIAL), bundle_(bundle), rtcp_mux_(rtcp_mux), running_{ true },
    transport_listener_(transport_listener), worker_{ worker }, io_worker_{ io_worker } {
    set_log_context("T%s.%s", iceConfig.connection_id.c_str(), bundle ? "bundle" : iceConfig.transport_name.c_str());
  }
  virtual ~Transport() {}

  virtual void updateIceState(IceState state, IceConnection *conn) = 0;
  virtual void onIceData(packetPtr packet) = 0;
  virtual void onCandidate(const CandidateInfo &candidate, IceConnection *conn) = 0;
  virtual void write(char* data, int len) = 0;
  virtual void processLocalSdp(SdpInfo *localSdp_) = 0;
  virtual void start() = 0;
  virtual void close() = 0;

  virtual std::shared_ptr<IceConnection> getIceConnection() { return ice_; }
  void setTransportListener(std::weak_ptr<TransportListener> listener) {
    transport_listener_ = listener;
  }
  std::weak_ptr<TransportListener> getTransportListener() {
    return transport_listener_;
  }
  TransportState getTransportState() {
    return state_;
  }
  void updateTransportState(TransportState state) {
    if (state == state_) {
      return;
    }
    state_ = state;
    if (auto listener = getTransportListener().lock()) {
      listener->updateState(state, this);
    }
  }
  void writeOnIce(int comp, void* buf, int len) {
    if (!running_) {
      return;
    }
    ice_->sendData(comp, buf, len);
  }
  bool setRemoteCandidates(const std::vector<CandidateInfo> &candidates, bool isBundle) {
    return ice_->setRemoteCandidates(candidates, isBundle);
  }

  virtual void onPacketReceived(packetPtr packet) {
    std::weak_ptr<Transport> weak_transport = Transport::shared_from_this();
    worker_->task([weak_transport, packet]() {
      if (auto this_ptr = weak_transport.lock()) {
        if (packet->length > 0) {
          this_ptr->onIceData(packet);
        }
        if (packet->length == -1) {
          this_ptr->running_ = false;
          return;
        }
      }
    });
  }

  std::shared_ptr<Worker> getWorker() {
    return worker_;
  }

  MediaType media_type() { return iceConfig_.media_type; }
  const char* transport_name() { return iceConfig_.transport_name.c_str(); }
 private:
  std::weak_ptr<TransportListener> transport_listener_;

 protected:
  IceConfig iceConfig_;
  TransportState state_;
  bool bundle_;
  bool rtcp_mux_;
  bool running_;
  std::shared_ptr<IceConnection> ice_;
  std::shared_ptr<Worker> worker_;
  std::shared_ptr<IOWorker> io_worker_;
};
}  // namespace erizo
#endif  // ERIZO_SRC_ERIZO_TRANSPORT_H_
