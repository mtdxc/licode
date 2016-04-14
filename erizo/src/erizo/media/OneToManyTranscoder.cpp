/*
 * OneToManyTranscoder.cpp
 */

#include "OneToManyTranscoder.h"
#include "../WebRtcConnection.h"
#include "../rtp/RtpHeaders.h"

namespace erizo {
DEFINE_LOGGER(OneToManyTranscoder, "media.OneToManyTranscoder");
OneToManyTranscoder::OneToManyTranscoder() {
	publisher = NULL;
	sentPackets_ = 0;

	MediaInfo m;
	m.processorType = RTP_ONLY;
//	m.videoCodec.bitRate = 2000000;
//	ELOG_DEBUG("m.videoCodec.bitrate %d\n", m.videoCodec.bitRate);
	m.hasVideo = true;
	m.videoCodec.width = 640;
	m.videoCodec.height = 480;
	m.hasAudio = false;

  ELOG_DEBUG("init ip");
  ip_ = new InputProcessor();
  ip_->init(m, this);

	MediaInfo om;
	om.processorType = RTP_ONLY;
	om.videoCodec.bitRate = 2000000;
	om.videoCodec.width = 640;
	om.videoCodec.height = 480;
	om.videoCodec.frameRate = 20;
	om.hasVideo = true;
//	om.url = "file://tmp/test.mp4";

	om.hasAudio = false;

  op_ = new OutputProcessor();
  op_->init(om, this);

}

OneToManyTranscoder::~OneToManyTranscoder() {
	closeAll();
  delete ip_; ip_ = NULL;
  delete op_; op_ = NULL;
}

int OneToManyTranscoder::deliverAudioData_(char* buf, int len) {
	if (subscribers.empty() || len <= 0)
		return 0;

	std::map<std::string, MediaSink*>::iterator it;
	for (it = subscribers.begin(); it != subscribers.end(); it++) {
		memcpy(sendAudioBuffer_, buf, len);
		(*it).second->deliverAudioData(sendAudioBuffer_, len);
	}

	return 0;
}

int OneToManyTranscoder::deliverVideoData_(char* buf, int len) {
	memcpy(sendVideoBuffer_, buf, len);

	RtpHeader* theHead = reinterpret_cast<RtpHeader*>(buf);
//	ELOG_DEBUG("extension %d pt %u", theHead->getExtension(),
//			theHead->getPayloadType());

  if (theHead->getPayloadType() == VP8_90000_PT) {
    // wait for receiveRawData
    ip_->deliverVideoData(sendVideoBuffer_, len);
	} else {
		receiveRtpData((unsigned char*) buf, len);
	}

	sentPackets_++;
	return 0;
}

void OneToManyTranscoder::receiveRawData(RawDataPacket& pkt) {
//	ELOG_DEBUG("Received %d", pkt.length);
  // wait for receiveRtpData
  op_->receiveRawData(pkt);
}

void OneToManyTranscoder::receiveRtpData(unsigned char*rtpdata, int len) {
	ELOG_DEBUG("Received rtp data %d", len);
	memcpy(sendVideoBuffer_, rtpdata, len);

	if (subscribers.empty() || len <= 0)
		return;
	std::map<std::string, MediaSink*>::iterator it;
	for (it = subscribers.begin(); it != subscribers.end(); it++) {
		(*it).second->deliverVideoData(sendVideoBuffer_, len);
	}
	sentPackets_++;
}

void OneToManyTranscoder::setPublisher(MediaSource* webRtcConn) {
	publisher = webRtcConn;
}

void OneToManyTranscoder::addSubscriber(MediaSink* webRtcConn,
		const std::string& peerId) {
	subscribers[peerId] = webRtcConn;
}

void OneToManyTranscoder::removeSubscriber(const std::string& peerId) {
  if (subscribers.find(peerId) != subscribers.end()) {
    delete subscribers[peerId];
    subscribers.erase(peerId);
  }
}

void OneToManyTranscoder::closeAll() {
  ELOG_WARN("OneToManyTranscoder closeAll");
  std::map<std::string, MediaSink*>::iterator it = subscribers.begin();
  while (it != subscribers.end()) {
    delete (*it).second;
    subscribers.erase(it++);
  }
  delete publisher;
}

}/* namespace erizo */

