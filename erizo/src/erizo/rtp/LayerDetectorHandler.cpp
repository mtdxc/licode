#include <vector>
#include "rtp/LayerDetectorHandler.h"
#include "WebRtcConnection.h"

namespace erizo {

DEFINE_LOGGER(LayerDetectorHandler, "rtp.LayerDetectorHandler");

LayerDetectorHandler::LayerDetectorHandler() : connection_{nullptr}, enabled_{true}, initialized_{false} {}

void LayerDetectorHandler::enable() {
  enabled_ = true;
}

void LayerDetectorHandler::disable() {
  enabled_ = false;
}

void LayerDetectorHandler::read(Context *ctx, packetPtr packet) {
  if (enabled_ && isVideoRtp(packet)) {
    RtpHeader *rtp_header = reinterpret_cast<RtpHeader*>(packet->data);
    RtpMap *codec = connection_->getRemoteSdpInfo().getCodecByExternalPayloadType(rtp_header->getPayloadType());
    if (codec && codec->encoding_name == "VP8") {
      parseLayerInfoFromVP8(packet);
    } else if (codec && codec->encoding_name == "VP9") {
      parseLayerInfoFromVP9(packet);
    } else if (codec && codec->encoding_name == "H264") {
      parseLayerInfoFromH264(packet);
    }
  }
  ctx->fireRead(packet);
}

int LayerDetectorHandler::getSsrcPosition(uint32_t ssrc) {
  for (size_t i = 0; i<video_ssrc_list_.size() ;i++){
    if (video_ssrc_list_[i] == ssrc)
      return i;
  }
  return -1;
}

void LayerDetectorHandler::parseLayerInfoFromVP8(packetPtr packet) {
  RtpHeader *rtp_header = reinterpret_cast<RtpHeader*>(packet->data);
  RTPPayloadVP8* payload = vp8_parser_.parseVP8(
	  rtp_header->getPayloadPtr(), 
	  packet->length - rtp_header->getHeaderLength());

  packet->compatible_temporal_layers = {};
  switch (payload->tID) {
    case 0:
      packet->compatible_temporal_layers.push_back(0);
    case 2:
      packet->compatible_temporal_layers.push_back(1);
    case 1:
      packet->compatible_temporal_layers.push_back(2);
    // case 3 and beyond are not handled because Chrome only
    // supports 3 temporal scalability today (03/15/17)
      break;
    default:
      packet->compatible_temporal_layers.push_back(0);
      break;
  }

  int position = getSsrcPosition(rtp_header->getSSRC());
  packet->compatible_spatial_layers = {position};
  if (!payload->frameType) {
    packet->is_keyframe = true;
  } else {
    packet->is_keyframe = false;
  }
  delete payload;
}

void LayerDetectorHandler::parseLayerInfoFromVP9(packetPtr packet) {
  RtpHeader *rtp_header = reinterpret_cast<RtpHeader*>(packet->data);
  RTPPayloadVP9* payload = vp9_parser_.parseVP9(
    rtp_header->getPayloadPtr(),
    packet->length - rtp_header->getHeaderLength());

  // why?
  int spatial_layer = payload->spatialID;
  packet->compatible_spatial_layers.clear();
  for (int i = 5; i >= spatial_layer; i--) {
    packet->compatible_spatial_layers.push_back(i);
  }

  packet->compatible_temporal_layers = {};
  switch (payload->temporalID) {
    case 0:
      packet->compatible_temporal_layers.push_back(0);
    case 2:
      packet->compatible_temporal_layers.push_back(1);
    case 1:
      packet->compatible_temporal_layers.push_back(2);
    case 3:
      packet->compatible_temporal_layers.push_back(3);
      break;
    default:
      packet->compatible_temporal_layers.push_back(0);
      break;
  }

  if (!payload->frameType) {
    packet->is_keyframe = true;
  } else {
    packet->is_keyframe = false;
  }

  packet->ending_of_layer_frame = payload->endingOfLayerFrame;
  delete payload;
}

void LayerDetectorHandler::parseLayerInfoFromH264(packetPtr packet)
{
  RtpHeader * rtp_header = (RtpHeader *)packet->data;
  uint8_t* p = rtp_header->getPayloadPtr();
  uint8_t nal_type = p[0] & 0x1f;
  // only support 2 temporal layer
  packet->compatible_temporal_layers = { p[0] & 0x60 ? 0 : 1 };
  if (nal_type == 28 || nal_type == 29) {//stap a and b
    nal_type = p[1] & 0x1f;
    packet->ending_of_layer_frame = p[1] & 0x40;
  }
  else if (nal_type == 25 || nal_type == 26) {
    nal_type = p[3] & 0x1f;
    packet->ending_of_layer_frame = true;
  }
  else {
    packet->ending_of_layer_frame = true;
  }
  packet->is_keyframe = (nal_type == 5 || nal_type == 7);
  packet->compatible_spatial_layers = { getSsrcPosition(rtp_header->getSSRC()) };
}

void LayerDetectorHandler::notifyUpdate() {
  if (initialized_) {
    return;
  }

  auto pipeline = getContext()->getPipelineShared();
  if (!pipeline) {
    return;
  }

  connection_ = pipeline->getService<WebRtcConnection>().get();
  if (!connection_) {
    return;
  }

  video_ssrc_list_ = connection_->getVideoSourceSSRCList();
}

}  // namespace erizo
