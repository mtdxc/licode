/*
 * TargetVideoBWDistributor.cpp
 */

#include <algorithm>

#include "TargetVideoBWDistributor.h"
#include "MediaStream.h"
#include "Transport.h"
#include "rtp/RtpUtils.h"

namespace erizo {

void TargetVideoBWDistributor::distribute(uint32_t remb, uint32_t ssrc,
                                  std::vector<std::shared_ptr<MediaStream>> streams, Transport *transport) {
  std::vector<MediaStreamInfo> stream_infos;
  for (auto stream : streams) {
    stream_infos.push_back({stream,
                            stream->isSimulcast(),
                            stream->isSlideShowModeEnabled(),
                            stream->getVideoBitrate(),
                            stream->getMaxVideoBW(),
                            stream->getBitrateFromMaxQualityLayer()});
  }

  std::sort(stream_infos.begin(), stream_infos.end(),
    [this](const MediaStreamInfo &i, const MediaStreamInfo &j) {
      return getTargetVideoBW(i) < getTargetVideoBW(j);
    });
  uint8_t remaining_streams = streams.size();
  uint32_t remaining_bitrate = remb;
  for(MediaStreamInfo stream : stream_infos) {
    uint32_t remaining_avg_bitrate = remaining_bitrate / remaining_streams;
    uint32_t target_bitrate = getTargetVideoBW(stream);
    uint32_t bitrate = std::min(target_bitrate, remaining_avg_bitrate);
    uint32_t remb = std::min(stream.max_video_bw, remaining_avg_bitrate);

    auto generated_remb = RtpUtils::createREMB(ssrc, {stream.stream->getVideoSinkSSRC()}, remb);
    stream.stream->onTransportData(generated_remb, transport);

    remaining_bitrate -= bitrate;
    remaining_streams--;
  }
}

uint32_t TargetVideoBWDistributor::getTargetVideoBW(const MediaStreamInfo &stream) {
  uint32_t max_bitrate = stream.max_video_bw;
  uint32_t target_bitrate = max_bitrate;
  if (stream.is_simulcast) {
    target_bitrate = std::min(stream.bitrate_from_max_quality_layer, max_bitrate);
  }
  if (stream.is_slideshow) {
    target_bitrate = std::min(stream.bitrate_sent, max_bitrate);
  }
  return target_bitrate;
}

}  // namespace erizo
