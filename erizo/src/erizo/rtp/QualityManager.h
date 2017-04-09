#ifndef ERIZO_SRC_ERIZO_RTP_QUALITYMANAGER_H_
#define ERIZO_SRC_ERIZO_RTP_QUALITYMANAGER_H_

#include "logger.h"
#include "lib/Clock.h"
#include "pipeline/Service.h"

namespace erizo {
class Stats;

class QualityManager: public Service, public std::enable_shared_from_this<QualityManager> {
  DECLARE_LOGGER();

 public:
  static constexpr duration kMinLayerSwitchInterval = std::chrono::seconds(10);
  static constexpr duration kActiveLayerInterval = std::chrono::milliseconds(500);
  static constexpr float kIncreaseLayerBitrateThreshold = 0.1f;

 public:
  explicit QualityManager(std::shared_ptr<Clock> the_clock = std::make_shared<SteadyClock>());
  void enable();
  void disable();

  virtual int getSpatialLayer() const { return spatial_layer_; }
  virtual int getTemporalLayer() const { return temporal_layer_; }
  virtual  bool isSlideShowEnabled() const { return slideshow_mode_active_; }

  void setSpatialLayer(int spatial_layer);
  void setTemporalLayer(int temporal_layer);
  // 强制指定接收Layer
  void forceLayers(int spatial_layer, int temporal_layer);

  void notifyQualityUpdate();

  virtual bool isPaddingEnabled() const { return padding_enabled_; }

 private:
  void calculateMaxActiveLayer();
  void selectLayer(bool try_higher_layers);
  uint64_t getInstantLayerBitrate(int spatial_layer, int temporal_layer);
  bool isInBaseLayer();
  bool isInMaxLayer();
  void setPadding(bool enabled);

 private:
  bool initialized_ = false;
  bool enabled_ = false;
  bool padding_enabled_ = true;
  // layer set by user in forceLayers func
  bool forced_layers_ = false;
  bool slideshow_mode_active_ = false;
  int spatial_layer_ = 0;
  int temporal_layer_ = 0;
  int max_active_spatial_layer_ = 0;
  int max_active_temporal_layer_ = 0;
  uint64_t current_estimated_bitrate_ = 0;

  time_point last_quality_check_;
  time_point last_activity_check_;
  std::shared_ptr<Stats> stats_;
  std::shared_ptr<Clock> clock_;
};
}  // namespace erizo

#endif  // ERIZO_SRC_ERIZO_RTP_QUALITYMANAGER_H_
