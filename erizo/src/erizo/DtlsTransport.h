#ifndef ERIZO_SRC_ERIZO_DTLSTRANSPORT_H_
#define ERIZO_SRC_ERIZO_DTLSTRANSPORT_H_

#include <mutex>
#include <memory>
#include <string>
#include "dtls/DtlsSocket.h"
#include "IceConnection.h"
#include "Transport.h"
#include "logger.h"

namespace erizo {
class SrtpChannel;
class Resender;

class DtlsTransport : dtls::DtlsReceiver, public Transport {
  DECLARE_LOGGER();

 public:
  DtlsTransport(MediaType med, const std::string& transport_name, const std::string& connection_id, bool bundle,
                bool rtcp_mux, std::weak_ptr<TransportListener> transport_listener, const IceConfig& iceConfig,
                bool isServer, std::shared_ptr<Worker> worker,
                std::shared_ptr<IOWorker> io_worker);
  virtual ~DtlsTransport();

  std::string getMyFingerprint();
  static bool isDtlsPacket(const char* buf, int len);

  // implement for Transport
  void start() override;
  void close() override;
  void onIceData(packetPtr packet) override;
  void onCandidate(const CandidateInfo &candidate, IceConnection *conn) override;
  void write(char* data, int len) override;
 
  // call by dtls render
  void writeDtlsPacket(dtls::DtlsSocketContext *ctx, packetPtr packet);
  // implement for dtls::DtlsReceiver
  void onDtlsPacket(dtls::DtlsSocketContext *ctx, const unsigned char* data, unsigned int len) override;
  void onHandshakeCompleted(dtls::DtlsSocketContext *ctx, std::string clientKey, std::string serverKey,
                            std::string srtp_profile) override;
  void onHandshakeFailed(dtls::DtlsSocketContext *ctx, const std::string error) override;
  void updateIceState(IceState state, IceConnection *conn) override;
  void processLocalSdp(SdpInfo *localSdp_) override;

  void updateIceStateSync(IceState state, IceConnection *conn);

 private:
  char protectBuf_[5000];
  packetPtr unprotect_packet_;
  std::unique_ptr<dtls::DtlsSocketContext> dtlsRtp, dtlsRtcp;
  std::mutex writeMutex_, sessionMutex_;
  std::unique_ptr<SrtpChannel> srtp_, srtcp_;
  bool readyRtp, readyRtcp;
  std::unique_ptr<Resender> rtcp_resender_, rtp_resender_;
  bool isServer_;
};

class Resender {
  DECLARE_LOGGER();

  // These values follow recommendations from section 4.2.4.1 in https://tools.ietf.org/html/rfc4347
  const unsigned int kMaxResends = 6;
  const unsigned int kInitialSecsPerResend = 1;

 public:
  Resender(DtlsTransport* transport, dtls::DtlsSocketContext* ctx);
  virtual ~Resender();
  void scheduleResend(packetPtr packet,unsigned int resend=6);
  void cancel();

 private:
  void scheduleNext();
  void resend();

 private:
  DtlsTransport* transport_;
  dtls::DtlsSocketContext* socket_context_;
  packetPtr packet_;
  unsigned int resend_seconds_;
  unsigned int max_resends_;
  std::shared_ptr<ScheduledTaskReference> scheduled_task_;
};
}  // namespace erizo
#endif  // ERIZO_SRC_ERIZO_DTLSTRANSPORT_H_
