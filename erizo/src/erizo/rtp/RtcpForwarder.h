#ifndef ERIZO_SRC_ERIZO_RTP_RTCPFORWARDER_H_
#define ERIZO_SRC_ERIZO_RTP_RTCPFORWARDER_H_

#include <map>
#include <list>
#include <set>

#include "logger.h"
#include "MediaDefinitions.h"
#include "SdpInfo.h"
#include "rtp/RtcpProcessor.h"

namespace erizo {

class RtcpForwarder: public RtcpProcessor{
  DECLARE_LOGGER();

 public:
  RtcpForwarder(MediaSink* msink, MediaSource* msource, uint32_t max_video_bw = 300000);
  virtual ~RtcpForwarder() {}

  void addSourceSsrc(uint32_t ssrc);
  void setPublisherBW(uint32_t bandwidth);
  void analyzeSr(RtcpHeader* chead);
  int analyzeFeedback(char* buf, int len);
  void checkRtcpFb();
  RtcpDataPtr getRtcpData(int ssrc);
 private:
  static const int RR_AUDIO_PERIOD = 2000;
  static const int RR_VIDEO_BASE = 800;
  static const int REMB_TIMEOUT = 1000;
  std::map<uint32_t, RtcpDataPtr> rtcpData_;
  std::mutex mapLock_;
  int addREMB(char* buf, uint32_t bitrate);
  int addNACK(char* buf, uint16_t seqNum, uint16_t blp, uint32_t sourceSsrc, uint32_t sinkSsrc);
};

}  // namespace erizo

#endif  // ERIZO_SRC_ERIZO_RTP_RTCPFORWARDER_H_
