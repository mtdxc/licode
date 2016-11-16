/*
 * mediadefinitions.h
 */
#ifndef ERIZO_SRC_ERIZO_MEDIADEFINITIONS_H_
#define ERIZO_SRC_ERIZO_MEDIADEFINITIONS_H_

#include <mutex>
#include <vector>
#include <memory>

namespace erizo {

enum packetType {
    VIDEO_PACKET,
    AUDIO_PACKET,
    OTHER_PACKET
};

struct DataPacket {
  DataPacket() = default;
  DataPacket(int comp_, const char *data_, int length_, packetType type_, uint64_t received_time_ms_);
  DataPacket(int comp_, const char *data_, int length_, packetType type_);
  DataPacket(int comp_, const unsigned char *data_, int length_);

  bool belongsToSpatialLayer(int spatial_layer_);
  bool belongsToTemporalLayer(int temporal_layer_);

  int comp;
  packetType type;
  char data[1500];
  int length;
  uint64_t received_time_ms;
  // why save vector in this?
  std::vector<int> compatible_spatial_layers;
  std::vector<int> compatible_temporal_layers;
  bool is_keyframe;  // Note: It can be just a keyframe first packet in VP8
  bool ending_of_layer_frame;
  int picture_id;
  std::string codec;
};
typedef std::shared_ptr<DataPacket> packetPtr;

class Monitor {
 protected:
    std::mutex monitor_mutex_;
};

class MediaEvent {
 public:
  MediaEvent() = default;
  virtual ~MediaEvent() {}
  virtual std::string getType() const {
    return "event";
  }
};

using MediaEventPtr = std::shared_ptr<MediaEvent>;

class FeedbackSink {
 public:
    virtual ~FeedbackSink() {}
    int deliverFeedback(packetPtr data_packet) {
        return this->deliverFeedback_(data_packet);
    }
 private:
    virtual int deliverFeedback_(packetPtr data_packet) = 0;
};

class FeedbackSource {
 protected:
    FeedbackSink* fb_sink_;
 public:
    FeedbackSource(): fb_sink_{nullptr} {}
    virtual ~FeedbackSource() {}
    void setFeedbackSink(FeedbackSink* sink) {
        fb_sink_ = sink;
    }
};

/*
 * A MediaSink
 */
class MediaSink: public virtual Monitor {
 protected:
    // SSRCs received by the SINK
    uint32_t audio_sink_ssrc_;
    uint32_t video_sink_ssrc_;
    // Is it able to provide Feedback
    FeedbackSource* sink_fb_source_;

 public:
    int deliverAudioData(packetPtr data_packet) {
        return this->deliverAudioData_(data_packet);
    }
    int deliverVideoData(packetPtr data_packet) {
        return this->deliverVideoData_(data_packet);
    }
    uint32_t getVideoSinkSSRC() {
        std::lock_guard<std::mutex> lock(monitor_mutex_);
        return video_sink_ssrc_;
    }
    void setVideoSinkSSRC(uint32_t ssrc) {
        std::lock_guard<std::mutex> lock(monitor_mutex_);
        video_sink_ssrc_ = ssrc;
    }
    uint32_t getAudioSinkSSRC() {
        std::lock_guard<std::mutex> lock(monitor_mutex_);
        return audio_sink_ssrc_;
    }
    void setAudioSinkSSRC(uint32_t ssrc) {
        std::lock_guard<std::mutex> lock(monitor_mutex_);
        audio_sink_ssrc_ = ssrc;
    }
    bool isVideoSinkSSRC(uint32_t ssrc) {
      return ssrc == video_sink_ssrc_;
    }
    bool isAudioSinkSSRC(uint32_t ssrc) {
      return ssrc == audio_sink_ssrc_;
    }
    FeedbackSource* getFeedbackSource() {
     std::lock_guard<std::mutex> lock(monitor_mutex_);
        return sink_fb_source_;
    }
    int deliverEvent(MediaEventPtr event) {
      return this->deliverEvent_(event);
    }
    MediaSink() : audio_sink_ssrc_{0}, video_sink_ssrc_{0}, sink_fb_source_{nullptr} {}
    virtual ~MediaSink() {}

    virtual void close() = 0;

 private:
    virtual int deliverAudioData_(packetPtr data_packet) = 0;
    virtual int deliverVideoData_(packetPtr data_packet) = 0;
    virtual int deliverEvent_(MediaEventPtr event) = 0;
};

/**
 * A MediaSource is any class that produces audio or video data.
 */
class MediaSource: public virtual Monitor {
 protected:
    // SSRCs coming from the source
    uint32_t audio_source_ssrc_;
    // for multi stream
    std::vector<uint32_t> video_source_ssrc_list_;
    MediaSink* video_sink_;
    MediaSink* audio_sink_;
    MediaSink* event_sink_;
    // can it accept feedback
    FeedbackSink* source_fb_sink_;

 public:
    void setAudioSink(MediaSink* audio_sink) {
        std::lock_guard<std::mutex> lock(monitor_mutex_);
        this->audio_sink_ = audio_sink;
    }
    void setVideoSink(MediaSink* video_sink) {
        std::lock_guard<std::mutex> lock(monitor_mutex_);
        this->video_sink_ = video_sink;
    }
    void setEventSink(MediaSink* event_sink) {
      std::lock_guard<std::mutex> lock(monitor_mutex_);
      this->event_sink_ = event_sink;
    }

    FeedbackSink* getFeedbackSink() {
        std::lock_guard<std::mutex> lock(monitor_mutex_);
        return source_fb_sink_;
    }
    virtual int sendPLI() = 0;
    uint32_t getVideoSourceSSRC() {
        std::lock_guard<std::mutex> lock(monitor_mutex_);
        return video_source_ssrc_list_[0];
    }
    void setVideoSourceSSRC(uint32_t ssrc) {
        std::lock_guard<std::mutex> lock(monitor_mutex_);
        video_source_ssrc_list_[0] = ssrc;
    }
    std::vector<uint32_t> getVideoSourceSSRCList() {
        std::lock_guard<std::mutex> lock(monitor_mutex_);
        return video_source_ssrc_list_;  //  return by copy to avoid concurrent access
    }
    void setVideoSourceSSRCList(const std::vector<uint32_t>& new_ssrc_list) {
        std::lock_guard<std::mutex> lock(monitor_mutex_);
        video_source_ssrc_list_ = new_ssrc_list;
    }
    uint32_t getAudioSourceSSRC() {
        std::lock_guard<std::mutex> lock(monitor_mutex_);
        return audio_source_ssrc_;
    }
    void setAudioSourceSSRC(uint32_t ssrc) {
        std::lock_guard<std::mutex> lock(monitor_mutex_);
        audio_source_ssrc_ = ssrc;
    }

    bool isVideoSourceSSRC(uint32_t ssrc);
    bool isAudioSourceSSRC(uint32_t ssrc) {
      return audio_source_ssrc_ == ssrc;
    }

    MediaSource() : audio_source_ssrc_{0}, video_source_ssrc_list_{std::vector<uint32_t>(1, 0)},
      video_sink_{nullptr}, audio_sink_{nullptr}, event_sink_{nullptr}, source_fb_sink_{nullptr} {}
    virtual ~MediaSource() {}

    virtual void close() = 0;
};

}  // namespace erizo

#endif  // ERIZO_SRC_ERIZO_MEDIADEFINITIONS_H_
