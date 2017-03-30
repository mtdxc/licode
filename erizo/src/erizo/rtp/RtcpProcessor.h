#ifndef ERIZO_SRC_ERIZO_RTP_RTCPPROCESSOR_H_
#define ERIZO_SRC_ERIZO_RTP_RTCPPROCESSOR_H_

#include <list>
#include <set>
#include <mutex>
#include <memory>

#include "MediaDefinitions.h"
#include "SdpInfo.h"
#include "pipeline/Service.h"

namespace erizo {
class RtcpHeader;
class SrDelayData {
 public:
  uint32_t sr_ntp;
  uint64_t sr_send_time;

  SrDelayData() : sr_ntp{0}, sr_send_time{0} {}

  SrDelayData(uint32_t ntp, uint64_t send_time) : sr_ntp{ntp},
    sr_send_time{send_time} {}
};
typedef std::shared_ptr<SrDelayData> SrDataPtr;

class RtcpData {
// lost packets - list and length
 public:
  // current values - tracks packet lost for fraction calculation
  uint16_t rrsReceivedInPeriod = 0;

  uint32_t ssrc = 0;
  uint32_t totalPacketsLost = 0;
  uint32_t prevTotalPacketsLost = 0;
  uint8_t ratioLost = 0;
  uint16_t highestSeqNumReceived = 0;
  uint16_t seqNumCycles = 0;
  uint32_t extendedSeqNo = 0;
  uint32_t prevExtendedSeqNo = 0;
  uint32_t lastSr = 0;
  uint64_t reportedBandwidth = 0;
  uint32_t maxBandwidth = 0;
  uint32_t delaySinceLastSr = 0;

  uint32_t nextPacketInMs = 0;

  uint32_t lastDelay = 0;

  uint32_t jitter = 0;
  // last SR field
  uint32_t lastSrTimestamp = 0;
  // required to properly calculate DLSR
  uint16_t nackSeqnum = 0;
  uint16_t nackBlp = 0;

  // time based data flow limits
  uint64_t last_sr_updated = 0;
  uint64_t last_remb_sent = 0;
  uint64_t last_sr_reception = 0;
  uint64_t last_rr_was_scheduled = 0;
  // to prevent sending too many reports, track time of last
  uint64_t last_rr_sent = 0;

  bool shouldSendPli = false;
  bool shouldSendREMB = false;
  bool shouldSendNACK = false;
  // flag to send receiver report
  bool requestRr = false;
  bool shouldReset = false;

  MediaType mediaType;

  std::list<SrDataPtr> senderReports;
  std::set<uint32_t> nackedPackets_;

  RtcpData() {
  }

  // lock for any blocking data change
  std::mutex dataLock;
};
typedef std::shared_ptr<RtcpData> RtcpDataPtr;

class RtcpProcessor : public Service {
 public:
  RtcpProcessor(MediaSink* msink, MediaSource* msource, uint32_t max_video_bw = 300000):
    rtcpSink_(msink), rtcpSource_(msource), max_video_bw_{max_video_bw} {}
  virtual ~RtcpProcessor() {}
  virtual void addSourceSsrc(uint32_t ssrc) = 0;
  virtual void setPublisherBW(uint32_t bandwidth) = 0;
  virtual void analyzeSr(RtcpHeader* chead) = 0;
  virtual int analyzeFeedback(char* buf, int len) = 0;
  virtual void checkRtcpFb() = 0;

  virtual void setMaxVideoBW(uint32_t bandwidth) { max_video_bw_ = bandwidth; }
  virtual uint32_t getMaxVideoBW() { return max_video_bw_; }

 protected:
  MediaSink* rtcpSink_;  // The sink to send RRs
  MediaSource* rtcpSource_;  // The source of SRs
  uint32_t max_video_bw_;
};

}  // namespace erizo

#endif  // ERIZO_SRC_ERIZO_RTP_RTCPPROCESSOR_H_
