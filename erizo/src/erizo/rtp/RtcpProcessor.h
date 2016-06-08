#ifndef RTCPPROCESSOR_H_
#define RTCPPROCESSOR_H_

#include <map>
#include <list>
#include <boost/shared_ptr.hpp>

#include "logger.h"
#include "MediaDefinitions.h"
#include "SdpInfo.h"
#include "rtp/RtpHeaders.h"

namespace erizo {
	inline uint64_t msTime(struct timeval& now){
		return (now.tv_sec * 1000) + (now.tv_usec / 1000);
	}
	inline int64_t msDelta(struct timeval& tv1, struct timeval& tv2){
		return (tv1.tv_sec - tv2.tv_sec) * 1000 + (tv1.tv_usec - tv2.tv_usec) / 1000;
	}
  // Forward declaration

  class SrData {
    public:
      uint32_t srNtp;
      struct timeval timestamp;

      SrData() {
        srNtp = 0;
        timestamp = {0, 0} ;
      };
      SrData(uint32_t srNTP, struct timeval theTimestamp){
        this->srNtp = srNTP;
        this->timestamp = theTimestamp;
      }
  };
  /// 对应一个ssrc一个结构
  class RtcpData {
  // lost packets - list and length
  public:
    // current values - tracks packet lost for fraction calculation
    uint16_t rrsReceivedInPeriod;

    uint32_t ssrc;
    uint32_t totalPacketsLost;
    uint32_t ratioLost:8;
    uint16_t highestSeqNumReceived;
    uint16_t seqNumCycles;
    uint32_t lastSr;
    uint64_t reportedBandwidth;
    uint32_t maxBandwidth;
    uint32_t delaySinceLastSr;

    uint32_t nextPacketInMs;
    
    uint32_t lastDelay;

    uint32_t jitter;
    // last SR field
    uint32_t lastSrTimestamp;
    // required to properly calculate DLSR
    uint16_t nackSeqnum;
    uint16_t nackBlp;

    // time based data flow limits
    struct timeval lastSrUpdated, lastREMBSent;
    struct timeval lastSrReception, lastRrWasScheduled;
    // to prevent sending too many reports, track time of last
    struct timeval lastRrSent;
    
    bool shouldSendPli;
    bool shouldSendREMB;
    bool shouldSendNACK;
    // flag to send receiver report
    bool requestRr;
    bool shouldReset;

    MediaType mediaType;
		// 保留最后20个sr
    std::list<boost::shared_ptr<SrData>> senderReports;

    void reset(uint32_t bandwidth);

    RtcpData(){
      nextPacketInMs = 0;
      rrsReceivedInPeriod = 0;
      totalPacketsLost = 0;
      ratioLost = 0;
      highestSeqNumReceived = 0;
      seqNumCycles = 0;
      lastSr = 0;
      reportedBandwidth = 0;
      delaySinceLastSr = 0;
      jitter = 0;
      lastSrTimestamp = 0;
      requestRr = false;
      lastDelay = 0;
     
      shouldSendPli = false;
      shouldSendREMB = false;
      shouldSendNACK = false;
      shouldReset = false;
      nackSeqnum = 0;
      nackBlp = 0;
      lastRrSent = {0, 0};
      lastREMBSent = {0, 0};
      lastSrReception = {0, 0};
      lastRrWasScheduled = {0, 0};
    }

    // lock for any blocking data change
    boost::mutex dataLock;
};
typedef boost::shared_ptr<RtcpData> RtcpDataRefPtr;

class RtcpProcessor{
	DECLARE_LOGGER();
  
  public:
    RtcpProcessor(MediaSink* msink, MediaSource* msource, uint32_t maxVideoBw = 300000);
    virtual ~RtcpProcessor(){
    };
		RtcpDataRefPtr addSourceSsrc(uint32_t ssrc);
    void setMaxVideoBW(uint32_t bandwidth);
    void setPublisherBW(uint32_t bandwidth);
    // 分析RR包
    void analyzeSr(RtcpHeader* chead);
    void analyzeFeedback(char* buf, int len);

    // 根据定时器和条件，触发RTCP包的生成和发送
    void checkRtcpFb();

    // append REMB to the buf and return fill size
    int addREMB(char* buf, uint32_t bitrate);
    int addNACK(char* buf, uint16_t seqNum, uint16_t blp, uint32_t sourceSsrc, uint32_t sinkSsrc);

  private:
    static const int RR_AUDIO_PERIOD = 2000;
    static const int RR_VIDEO_BASE = 800; 
    static const int REMB_TIMEOUT = 1000;
    static const uint64_t NTPTOMSCONV = 4294967296;
		// ssrc->RtcpData
		std::map<uint32_t, RtcpDataRefPtr> rtcpData_;
    boost::mutex mapLock_;

    MediaSink* rtcpSink_;  // The sink to send RRs
    MediaSource* rtcpSource_; // The source of SRs
    uint32_t maxVideoBw_, defaultVideoBw_;
    uint8_t packet_[128];

};

} /* namespace erizo */

#endif /* RTCPPROCESSOR_H_ */
