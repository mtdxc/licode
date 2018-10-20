#ifndef ERIZO_SRC_ERIZO_DTLSTRANSPORT_H_
#define ERIZO_SRC_ERIZO_DTLSTRANSPORT_H_


#include <memory>
#include <string>
#include "dtls/DtlsSocket.h"
#include "./IceConnection.h"
#include "./Transport.h"
#include "./logger.h"

namespace erizo {
class SrtpChannel;
class TimeoutChecker;
class DtlsTransport : dtls::DtlsReceiver, public Transport {
  DECLARE_LOGGER();

 public:
  DtlsTransport(const IceConfig& iceConfig, bool bundle, bool rtcp_mux, bool isServer, 
                std::weak_ptr<TransportListener> transport_listener,
                std::shared_ptr<Worker> worker,
                std::shared_ptr<IOWorker> io_worker);
  virtual ~DtlsTransport();

  std::string getMyFingerprint() const;
  static bool isDtlsPacket(const char* buf, int len);
  void start() override;
  void close() override;
  void onIceData(packetPtr packet) override;
  void onCandidate(const CandidateInfo &candidate, IceConnection *conn) override;
  void write(char* data, int len) override;
  void onDtlsPacket(dtls::DtlsSocketContext *ctx, const unsigned char* data, unsigned int len) override;
  void onHandshakeCompleted(dtls::DtlsSocketContext *ctx, std::string clientKey, std::string serverKey,
                            std::string srtp_profile) override;
  void onHandshakeFailed(dtls::DtlsSocketContext *ctx, const std::string& error) override;
  void updateIceState(IceState state, IceConnection *conn) override;
  void processLocalSdp(SdpInfo *localSdp_) override;

  void updateIceStateSync(IceState state, IceConnection *conn);

 private:
  char protectBuf_[1500];
  std::unique_ptr<dtls::DtlsSocketContext> dtlsRtp, dtlsRtcp;
  std::mutex writeMutex_, sessionMutex_;
  std::unique_ptr<SrtpChannel> srtp_, srtcp_;
  bool readyRtp, readyRtcp;
  bool isServer_;
  std::unique_ptr<TimeoutChecker> rtcp_timeout_checker_, rtp_timeout_checker_;
};

class TimeoutChecker {
  DECLARE_LOGGER();

  const unsigned int kMaxTimeoutChecks = 15;
  const unsigned int kInitialSecsPerTimeoutCheck = 1;

 public:
  TimeoutChecker(DtlsTransport* transport, dtls::DtlsSocketContext* ctx);
  virtual ~TimeoutChecker();

  void scheduleCheck();
  void cancel();
 private:
  void scheduleNext();

 private:
  DtlsTransport* transport_;
  dtls::DtlsSocketContext* socket_context_;
  unsigned int check_seconds_;
  unsigned int max_checks_;
  int scheduled_task_;
};
}  // namespace erizo
#endif  // ERIZO_SRC_ERIZO_DTLSTRANSPORT_H_
