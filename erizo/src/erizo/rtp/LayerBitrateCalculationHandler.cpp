#include <vector>
#include "rtp/LayerBitrateCalculationHandler.h"
#include "rtp/QualityManager.h"
#include "WebRtcConnection.h"
#include "Stats.h"

namespace erizo {

DEFINE_LOGGER(LayerBitrateCalculationHandler, "rtp.LayerBitrateCalculationHandler");

LayerBitrateCalculationHandler::LayerBitrateCalculationHandler() : enabled_{true},
  initialized_{false} {}

void LayerBitrateCalculationHandler::enable() {
  enabled_ = true;
}

void LayerBitrateCalculationHandler::disable() {
  enabled_ = false;
}

void LayerBitrateCalculationHandler::write(Context *ctx, packetPtr packet) {
  if (!enabled_ || !initialized_ || packet->type!=VIDEO_PACKET || isRtcpPacket(packet)) {
    ctx->fireWrite(packet);
    return;
  }
  // 统计发出去的各层包的流量...
  for (auto s : packet->compatible_spatial_layers){
    std::string spatial_layer_name = std::to_string(s);
    for (auto t: packet->compatible_temporal_layers){
      std::string temporal_layer_name = std::to_string(t);
      StatNode& sNode = stats_->getNode()[kQualityLayersStatsKey][spatial_layer_name];
      if (!sNode.hasChild(temporal_layer_name)) {
        sNode.insertStat(temporal_layer_name, 
          MovingIntervalRateStat(kLayerRateStatIntervalSize, kLayerRateStatIntervals, 8. ));
      }
      else {
        sNode[temporal_layer_name] += packet->length;
      }
    }
  }
  quality_manager_->notifyQualityUpdate();
  ctx->fireWrite(packet);
}


void LayerBitrateCalculationHandler::notifyUpdate() {
  if (initialized_) {
    return;
  }

  auto pipeline = getContext()->getPipelineShared();
  if (!pipeline) {
    return;
  }
  stats_ = pipeline->getService<Stats>();
  if (!stats_) {
    return;
  }
  quality_manager_ = pipeline->getService<QualityManager>();
  if (!quality_manager_) {
    return;
  }
  initialized_ = true;
}
}  // namespace erizo
