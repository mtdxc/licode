#ifndef ERIZO_SRC_ERIZO_RTP_QUALITYFILTERHANDLER_H_
#define ERIZO_SRC_ERIZO_RTP_QUALITYFILTERHANDLER_H_

#include <memory>
#include <string>
#include <random>
#include <map>

#include "logger.h"
#include "lib/Clock.h"
#include "pipeline/Handler.h"
#include "rtp/SequenceNumberTranslator.h"
#include "rtp/QualityManager.h"

namespace erizo {

class WebRtcConnection;

class QualityFilterHandler: public Handler, public std::enable_shared_from_this<QualityFilterHandler> {
  DECLARE_LOGGER();

 public:
  QualityFilterHandler();

  void enable() override;
  void disable() override;

  std::string getName() override {
     return "quality_filter";
  }

  void read(Context *ctx, packetPtr packet) override;
  void write(Context *ctx, packetPtr packet) override;
  void notifyUpdate() override;

 private:
  void sendPLI();
  void checkLayers();
  void handleFeedbackPackets(packetPtr packet);
  bool checkSSRCChange(uint32_t ssrc);
  void changeSpatialLayerOnKeyframeReceived(packetPtr packet);
  void detectVideoScalability(packetPtr packet);

 private:
  std::shared_ptr<QualityManager> quality_manager_;
  SequenceNumberTranslator translator_;
  WebRtcConnection *connection_;
  bool enabled_ = true;
  bool initialized_ = false;
  bool receiving_multiple_ssrc_ = false;
  bool is_scalable_ = false;

  int target_spatial_layer_ = 0;
  // 将切换的空间层id, 空间层必须等到关键帧后才可以切换,切换后此id就置为-1
  int future_spatial_layer_ = -1;
  int target_temporal_layer_ = 0;

  uint32_t video_sink_ssrc_ = 0;
  uint32_t video_source_ssrc_ = 0;
  uint32_t last_ssrc_received_ = 0;
  uint32_t max_video_bw_ = 0;
  uint32_t last_timestamp_sent_ = 0;
  uint32_t timestamp_offset_ = 0;
  time_point time_change_started_;
};
}  // namespace erizo

#endif  // ERIZO_SRC_ERIZO_RTP_QUALITYFILTERHANDLER_H_
