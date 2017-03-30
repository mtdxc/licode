#ifndef ERIZO_SRC_ERIZO_RTP_RTPAUDIOMUTEHANDLER_H_
#define ERIZO_SRC_ERIZO_RTP_RTPAUDIOMUTEHANDLER_H_

#include "logger.h"
#include "pipeline/Handler.h"

#include <mutex>  // NOLINT

namespace erizo {

class WebRtcConnection;

class RtpAudioMuteHandler: public Handler {
  DECLARE_LOGGER();

 public:
  RtpAudioMuteHandler();
  void muteAudio(bool active);

  void enable() override;
  void disable() override;

  std::string getName() override {
    return "audio-mute";
  }

  void read(Context *ctx, packetPtr packet) override;
  void write(Context *ctx, packetPtr packet) override;
  void notifyUpdate() override;

 private:
  // 因静音导致出现的系列号偏移，需要进行修正
  uint16_t seq_num_offset_ = 0;
  uint16_t last_sent_seq_num_;
  int32_t  last_original_seq_num_ = -1;

  bool mute_is_active_ = false;

  WebRtcConnection* connection_;

  inline void setPacketSeqNumber(packetPtr packet, uint16_t seq_number);
};

}  // namespace erizo

#endif  // ERIZO_SRC_ERIZO_RTP_RTPAUDIOMUTEHANDLER_H_
