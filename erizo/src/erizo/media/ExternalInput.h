#ifndef ERIZO_SRC_ERIZO_MEDIA_EXTERNALINPUT_H_
#define ERIZO_SRC_ERIZO_MEDIA_EXTERNALINPUT_H_

#include <thread>
#include <queue>
#include "logger.h"
#include "codecs/VideoCodec.h"
#include "media/MediaProcessor.h"
struct AVFormatContext;

namespace erizo {
class WebRtcConnection;

class ExternalInput : public MediaSource, public RTPDataReceiver {
  DECLARE_LOGGER();

 public:
  explicit ExternalInput(const std::string& inputUrl);
  virtual ~ExternalInput();

  int init();
  void receiveRtpData(unsigned char* rtpdata, int len) override;
  int sendPLI() override;

  void close() override {}

 private:
  std::unique_ptr<OutputProcessor> op_;
  VideoDecoder inCodec_;
  std::unique_ptr<unsigned char> decodedBuffer_;

  std::string url_;
  bool running_;
  bool needTranscoding_;
  std::mutex queueMutex_;
  std::thread thread_, encodeThread_;
  std::queue<RawDataPacket> packetQueue_;
  AVFormatContext* context_;

  int video_stream_index_, video_time_base_;
  int audio_stream_index_, audio_time_base_;
  int bufflen_;

  int64_t lastPts_, lastAudioPts_;
  int64_t startTime_;


  void receiveLoop();
  void encodeLoop();
};
}  // namespace erizo
#endif  // ERIZO_SRC_ERIZO_MEDIA_EXTERNALINPUT_H_
