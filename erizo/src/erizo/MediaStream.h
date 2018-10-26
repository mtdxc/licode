
#ifndef ERIZO_SRC_ERIZO_MEDIASTREAM_H_
#define ERIZO_SRC_ERIZO_MEDIASTREAM_H_

#include <mutex>
#include <atomic>
#include <string>
#include <map>
#include <vector>

#include "./logger.h"
#include "./SdpInfo.h"
#include "./MediaDefinitions.h"
#include "./Transport.h"
#include "./WebRtcConnection.h"
#include "thread/Worker.h"
#include "rtp/RtpExtensionProcessor.h"
#include "lib/Clock.h"
#include "pipeline/Pipeline.h"
#include "pipeline/Handler.h"
#include "pipeline/HandlerManager.h"
#include "pipeline/Service.h"

namespace erizo {

class MediaStreamStatsListener {
 public:
    virtual ~MediaStreamStatsListener() {}
    virtual void notifyStats(const std::string& message) = 0;
};


class MediaStreamEventListener {
 public:
    virtual ~MediaStreamEventListener() {}
    virtual void notifyMediaStreamEvent(const std::string& type, const std::string& message) = 0;
};

class QualityManager;
class Stats;
class RtcpProcessor;
class PacketBufferService;
/**
 * A MediaStream. This class represents a Media Stream that can be established with other peers via a SDP negotiation
 */
class MediaStream: public MediaSink, public MediaSource, public FeedbackSink,
                        public FeedbackSource, public HandlerManagerListener,
                        public std::enable_shared_from_this<MediaStream>, public LogContext, public Service {
  DECLARE_LOGGER();

 public:
  typedef typename Handler::Context Context;
  bool audio_enabled_;
  bool video_enabled_;

  /**
   * Constructor.
   * Constructs an empty MediaStream without any configuration.
   */
  MediaStream(std::shared_ptr<Worker> worker, std::shared_ptr<WebRtcConnection> connection,
      const std::string& stream_id, const std::string& stream_label,
      bool is_publisher);
  /**
   * Destructor.
   */
  virtual ~MediaStream();

  bool init();
  void close() override;
  void syncClose();

  bool setRemoteSdp(std::shared_ptr<SdpInfo> sdp);

  virtual uint32_t getBitrateFromMaxQualityLayer() { return bitrate_from_max_quality_layer_; }
  void setBitrateFromMaxQualityLayer(uint64_t bitrate) { bitrate_from_max_quality_layer_ = bitrate; }
  virtual uint32_t getVideoBitrate() { return video_bitrate_; }
  void setVideoBitrate(uint32_t bitrate) { video_bitrate_ = bitrate; }
  virtual uint32_t getMaxVideoBW();
  void setMaxVideoBW(uint32_t max_video_bw);

  /**
   * Sends a PLI Packet
   * @return the size of the data sent
   */
  int sendPLI() override;
  void sendPLIToFeedback();
  void setQualityLayer(int spatial_layer, int temporal_layer);
  void enableSlideShowBelowSpatialLayer(bool enabled, int spatial_layer);

  WebRTCEvent getCurrentState();

  /**
   * Sets the Event Listener for this MediaStream
   */
  void setMediaStreamEventListener(MediaStreamEventListener* listener);
  void notifyMediaStreamEvent(const std::string& type, const std::string& message);

  /**
   * Sets the Stats Listener for this MediaStream
   */
  void setMediaStreamStatsListener(MediaStreamStatsListener* listener) {
    stats_->setStatsListener(listener);
  }

  void getJSONStats(std::function<void(std::string)> callback);

  virtual void onTransportData(packetPtr packet, Transport *transport);

  void sendPacketAsync(packetPtr packet);

  void setTransportInfo(std::string audio_info, std::string video_info);

  void setFeedbackReports(bool will_send_feedback, uint32_t target_bitrate = 0);
  void setSlideShowMode(bool state);
  void muteStream(bool mute_video, bool mute_audio);
  void setVideoConstraints(int max_video_width, int max_video_height, int max_video_frame_rate);

  void setMetadata(std::map<std::string, std::string> metadata);
  // 将Packet传递给sink等
  void read(packetPtr packet);
  // 写到connection
  void write(packetPtr packet);

  void enableHandler(const std::string &name);
  void disableHandler(const std::string &name);
  void notifyUpdateToHandlers() override;

  void notifyToEventSink(MediaEventPtr event);

  void asyncTask(std::function<void()> f);

  void initializeStats();
  void printStats();

  bool isAudioMuted() { return audio_muted_; }
  bool isVideoMuted() { return video_muted_; }

  std::shared_ptr<SdpInfo> getRemoteSdpInfo() { return remote_sdp_; }

  virtual bool isSlideShowModeEnabled() { return slide_show_mode_; }

  virtual bool isSimulcast() { return simulcast_; }
  void setSimulcast(bool simulcast) { simulcast_ = simulcast; }

  RtpExtensionProcessor& getRtpExtensionProcessor() { return connection_->getRtpExtensionProcessor(); }
  std::shared_ptr<Worker> getWorker() { return worker_; }

  std::string getId() { return stream_id_; }
  std::string getLabel() { return mslabel_; }

  bool isSourceSSRC(uint32_t ssrc);
  bool isSinkSSRC(uint32_t ssrc);
  void parseIncomingPayloadType(packetPtr packet, packetType type);

  bool isPipelineInitialized() const { return pipeline_initialized_; }
  bool isRunning() const { return pipeline_initialized_ && sending_; }
  Pipeline::Ptr getPipeline() { return pipeline_; }
  bool isPublisher() { return is_publisher_; }

  void Log(const char* fmt, ...) {
	  va_list vl;
	  va_start(vl, fmt);
	  LogStrV(logger, fmt, vl);
	  va_end(vl);
  }
 private:
  void sendPacket(packetPtr packet);
  int deliverAudioData_(packetPtr audio_packet) override;
  int deliverVideoData_(packetPtr video_packet) override;
  int deliverFeedback_(packetPtr fb_packet) override;
  int deliverEvent_(MediaEventPtr event) override;

  void initializePipeline();

  void transferLayerStats(std::string spatial, std::string temporal);
  void transferMediaStats(std::string target_node, std::string source_parent, std::string source_node);

  void changeDeliverPayloadType(packetPtr dp, packetType type);
  // parses incoming payload type, replaces occurence in buf

 private:
  std::mutex event_mutex_;
  MediaStreamEventListener* event_listener_ = nullptr;
  std::shared_ptr<WebRtcConnection> connection_;
  std::string stream_id_;
  std::string mslabel_;
  bool should_send_feedback_;
  bool slide_show_mode_;
  bool sending_;
  int bundle_;

  uint32_t rate_control_;  // Target bitrate for hacky rate control in BPS

  time_point mark_;
  std::shared_ptr<RtcpProcessor> rtcp_processor_;
  std::shared_ptr<Stats> stats_;
  std::shared_ptr<Stats> log_stats_;
  std::shared_ptr<QualityManager> quality_manager_;
  std::shared_ptr<PacketBufferService> packet_buffer_;
  std::shared_ptr<HandlerManager> handler_manager_;

  Pipeline::Ptr pipeline_;

  std::shared_ptr<Worker> worker_;

  bool audio_muted_;
  bool video_muted_;

  bool pipeline_initialized_;

  bool is_publisher_;

  std::atomic_bool simulcast_ = false;
  std::atomic<uint64_t> bitrate_from_max_quality_layer_ = 0;
  std::atomic<uint32_t> video_bitrate_ = 0;
 protected:
  std::shared_ptr<SdpInfo> remote_sdp_;
};

class PacketReader : public InboundHandler {
 public:
  explicit PacketReader(MediaStream *media_stream) : media_stream_{media_stream} {}

  void enable() override {}
  void disable() override {}

  std::string getName() override {
    return "reader";
  }

  void read(Context *ctx, packetPtr packet) override {
    media_stream_->read(std::move(packet));
  }

  void notifyUpdate() override {
  }

 private:
  MediaStream *media_stream_;
};

class PacketWriter : public OutboundHandler {
 public:
  explicit PacketWriter(MediaStream *media_stream) : media_stream_{media_stream} {}

  void enable() override {}
  void disable() override {}

  std::string getName() override {
    return "writer";
  }

  void write(Context *ctx, packetPtr packet) override {
    media_stream_->write(std::move(packet));
  }

  void notifyUpdate() override {
  }

 private:
  MediaStream *media_stream_;
};

}  // namespace erizo
#endif  // ERIZO_SRC_ERIZO_MEDIASTREAM_H_
