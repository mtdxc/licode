#ifndef ERIZO_SRC_ERIZO_WEBRTCCONNECTION_H_
#define ERIZO_SRC_ERIZO_WEBRTCCONNECTION_H_

#include <mutex>
#include <string>
#include <map>
#include <vector>

#include "logger.h"
#include "SdpInfo.h"
#include "MediaDefinitions.h"
#include "Transport.h"
#include "Stats.h"
#include "pipeline/Pipeline.h"
#include "thread/Worker.h"
#include "thread/IOWorker.h"
#include "rtp/RtcpProcessor.h"
#include "rtp/RtpExtensionProcessor.h"
#include "lib/Clock.h"
#include "pipeline/Handler.h"
#include "pipeline/Service.h"

namespace erizo {
constexpr std::chrono::milliseconds kBitrateControlPeriod(100);
class Worker;
class Stats;
class Transport;
class TransportListener;
class IceConfig;
class RtcpProcessor;
class QualityManager;
class PacketBufferService;
/**
 * WebRTC Events
 */
enum WebRTCEvent {
  CONN_INITIAL = 101, CONN_STARTED = 102, CONN_GATHERED = 103, CONN_READY = 104, CONN_FINISHED = 105,
  CONN_CANDIDATE = 201, CONN_SDP = 202,
  CONN_FAILED = 500
};

class WebRtcConnectionEventListener {
public:
  virtual ~WebRtcConnectionEventListener() {}
  virtual void notifyEvent(WebRTCEvent newEvent, const std::string& message) = 0;
};

class WebRtcConnectionStatsListener {
public:
  virtual ~WebRtcConnectionStatsListener() {}
  virtual void notifyStats(const std::string& message) = 0;
};

/**
 * A WebRTC Connection. 
 * This class represents a WebRTC Connection that can be established with other peers via a SDP negotiation
 * it comprises all the necessary Transport components.
 */
class WebRtcConnection: public MediaSink, public MediaSource, public FeedbackSink,
                        public FeedbackSource, public TransportListener, public LogContext,
                        public std::enable_shared_from_this<WebRtcConnection>, public Service {
  DECLARE_LOGGER();

 public:
  typedef typename Handler::Context Context;

  /**
   * Constructor.
   * Constructs an empty WebRTCConnection without any configuration.
   */
  WebRtcConnection(std::shared_ptr<Worker> worker, std::shared_ptr<IOWorker> io_worker,
      const std::string& connection_id, const IceConfig& iceConfig,
      const std::vector<RtpMap>& rtp_mappings, const std::vector<erizo::ExtMap>& ext_mappings,
      WebRtcConnectionEventListener* listener);
  /**
   * Destructor.
   */
  virtual ~WebRtcConnection();
  /**
   * Inits the WebConnection by starting ICE Candidate Gathering.
   * @return True if the candidates are gathered.
   */
  bool init();
  void close() override;
  /**
   * Sets the SDP of the remote peer.
   * @param sdp The SDP.
   * @return true if the SDP was received correctly.
   */
  bool setRemoteSdp(const std::string &sdp);

  bool createOffer(bool videoEnabled, bool audioEnabled, bool bundle);
  /**
   * Add new remote candidate (from remote peer).
   * @param sdp The candidate in SDP format.
   * @return true if the SDP was received correctly.
   */
  bool addRemoteCandidate(const std::string &mid, int mLineIndex, const std::string &sdp);
  /**
   * Obtains the local SDP.
   * @return The SDP as a string.
   */
  std::string getLocalSdp();

  /**
   * Sends a PLI Packet
   * @return the size of the data sent
   */
  int sendPLI() override;

  void setQualityLayer(int spatial_layer, int temporal_layer);

  /**
   * Sets the Event Listener for this WebRtcConnection
   */
  inline void setWebRtcConnectionEventListener(WebRtcConnectionEventListener* listener) {
    connEventListener_ = listener;
  }

  /**
   * Sets the Stats Listener for this WebRtcConnection
   */
  void setWebRtcConnectionStatsListener(WebRtcConnectionStatsListener* listener);

  /**
   * Gets the current state of the Ice Connection
   * @return
   */
  WebRTCEvent getCurrentState();

  void getJSONStats(std::function<void(std::string)> callback);

  // implement for TransportListener
  void onTransportData(packetPtr packet, Transport *transport) override;
  void updateState(TransportState state, Transport * transport) override;
  void onCandidate(const CandidateInfo& cand, Transport *transport) override;

  void sendPacketAsync(packetPtr packet);

  void setFeedbackReports(bool will_send_feedback, uint32_t target_bitrate = 0);
  void setSlideShowMode(bool state);
  void muteStream(bool mute_video, bool mute_audio);

  void setMetadata(std::map<std::string, std::string> metadata);

  // change rtp payload type and pass to sink
  void read(packetPtr packet);
  // send packet to transport
  void write(packetPtr packet);

  void enableHandler(const std::string &name);
  void disableHandler(const std::string &name);
  void notifyUpdateToHandlers();

  void asyncTask(std::function<void(std::shared_ptr<WebRtcConnection>)> f);

  bool isAudioMuted() { return audio_muted_; }
  bool isVideoMuted() { return video_muted_; }

  SdpInfo& getRemoteSdpInfo() { return remoteSdp_; }
  bool isSlideShowModeEnabled() { return slide_show_mode_; }
  RtpExtensionProcessor& getRtpExtensionProcessor() { return extProcessor_; }
  std::shared_ptr<Worker> getWorker() { return worker_; }

  bool isSourceSSRC(uint32_t ssrc);
  bool isSinkSSRC(uint32_t ssrc);

 private:
  void sendPacket(packetPtr packet);
  int deliverAudioData_(packetPtr audio_packet) override;
  int deliverVideoData_(packetPtr video_packet) override;
  int deliverFeedback_(packetPtr fb_packet) override;

  void initializePipeline();

  // Utils
  std::string getJSONCandidate(const std::string& mid, const std::string& sdp);
  // changes the outgoing payload type for in the given data packet
  void changeDeliverPayloadType(dataPacket *dp, packetType type);
  // parses incoming payload type, replaces occurence in buf
  void parseIncomingPayloadType(char *buf, int len, packetType type);
  void trackTransportInfo();

 private:
  WebRtcConnectionEventListener* connEventListener_;

  std::string connection_id_;
  SdpInfo remoteSdp_;
  SdpInfo localSdp_;
  bool audioEnabled_;
  bool videoEnabled_;
  bool trickleEnabled_;
  bool shouldSendFeedback_;
  bool sending_;
  bool bundle_;

  bool slide_show_mode_; // conflict with rateControl_ must only use one
  uint32_t rateControl_;  // Target bitrate for hacky rate control in bps
  time_point mark_; // last tick for rate control(no used)

  IceConfig iceConfig_;
  std::vector<RtpMap> rtp_mappings_;
  RtpExtensionProcessor extProcessor_;

  std::shared_ptr<RtcpProcessor> rtcp_processor_;
  std::shared_ptr<Transport> videoTransport_, audioTransport_;

  std::shared_ptr<Stats> stats_;
  std::shared_ptr<QualityManager> quality_manager_;
  std::shared_ptr<PacketBufferService> packet_buffer_;
  WebRTCEvent globalState_;

  std::mutex updateStateMutex_;

  Pipeline::Ptr pipeline_;
  bool pipeline_initialized_;

  std::shared_ptr<Worker> worker_;
  std::shared_ptr<IOWorker> io_worker_;

  bool audio_muted_;
  bool video_muted_;
};

class PacketReader : public InboundHandler {
 public:
  explicit PacketReader(WebRtcConnection *connection) : connection_{connection} {}

  void enable() override {}
  void disable() override {}

  std::string getName() override {
    return "reader";
  }

  void read(Context *ctx, packetPtr packet) override {
    connection_->read(packet);
  }

  void notifyUpdate() override {
  }

 private:
  WebRtcConnection *connection_;
};

class PacketWriter : public OutboundHandler {
 public:
  explicit PacketWriter(WebRtcConnection *connection) : connection_{connection} {}

  void enable() override {}
  void disable() override {}

  std::string getName() override {
    return "writer";
  }

  void write(Context *ctx, packetPtr packet) override {
    connection_->write(packet);
  }

  void notifyUpdate() override {
  }

 private:
  WebRtcConnection *connection_;
};

}  // namespace erizo
#endif  // ERIZO_SRC_ERIZO_WEBRTCCONNECTION_H_
