#ifndef MEDIAPROCESSOR_H_
#define MEDIAPROCESSOR_H_

#include <boost/cstdint.hpp>
#ifdef _WIN32
#include <winsock2.h>
#else
#include <sys/time.h>
#include <arpa/inet.h>
#endif
#include <string>

#include "rtp/RtpVP8Parser.h"
#include "../MediaDefinitions.h"
#include "codecs/Codecs.h"
#include "codecs/VideoCodec.h"
#include "logger.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}

namespace erizo {


struct RTPInfo {
	enum AVCodecID codec;
	unsigned int ssrc;
	unsigned int PT; ///< payload type
};

enum ProcessorType {
	RTP_ONLY, AVF, PACKAGE_ONLY
};

enum DataType {
	VIDEO, AUDIO
};
/// 原始音视频数据
struct RawDataPacket {
	unsigned char* data; ///< 对于视频就是YUV，音频是PCM 16
	int length; ///< 原始数据长度
	DataType type; ///< 数据类型(音频/视频)
};

struct MediaInfo {
	std::string url;
	bool hasVideo;
	bool hasAudio;
	ProcessorType processorType;
	RTPInfo rtpVideoInfo;
	RTPInfo rtpAudioInfo;
	VideoCodecInfo videoCodec;
	AudioCodecInfo audioCodec;
};

#define UNPACKAGED_BUFFER_SIZE 150000
#define PACKAGED_BUFFER_SIZE 2000
/* Usage example
class MediaProcessor{
  MediaProcessor(){
    input = new InputProcessor();
    output = new OutputProcessor();
    MediaInfo mi;
    // init with mi;
    input->init(mi, output);
    RTPDataReceiver* rtpRecv = NULL;
    // set mi;
    output->init(mi, rtpRecv);
  }
  void FillData(){
    // fill rtp data
    input->deliverAudioData(char* buf, int len);
    input->deliverVideoData(char* buf, int len);
    //got rtcp data from 
    RTPDataReceiver::receiveRtpData(unsigned char* rtpdata, int len);
  }
  virtual ~Mediaprocessor(){
    delete input; input = NULL;
    delete output; output = NULL;
  }
private:
	InputProcessor* input;
	OutputProcessor* output;
};
*/

class RawDataReceiver {
public:
	virtual void receiveRawData(RawDataPacket& packet) = 0;
	virtual ~RawDataReceiver() {}
};

class RTPDataReceiver {
public:
	virtual void receiveRtpData(unsigned char* rtpdata, int len) = 0;
	virtual ~RTPDataReceiver() {}
};

class RTPSink;
/*
媒体流输入处理.
从MediaSink中接受RTP数据流，并进行解包，
最用用ffmpeg解码成原始帧后，在传给RawDataReceiver回调
@note 当前视频只支持VP8解码.
*/
class InputProcessor: public MediaSink {
	DECLARE_LOGGER();
public:
	InputProcessor();
	virtual ~InputProcessor();

	/**
	@brief 初始化.
	
	@param info MeidiaSink的媒体格式（用于解码）
	@param receiver 原始数据输出回调
	@return 
	@retval 
	*/
	int init(const MediaInfo& info, RawDataReceiver* receiver);
	/**
	@brief 视频解包(RTP->VideoFrame).
	
	@param inBuff RTP缓冲区
	@param inBuffLen RTP长度
	@param outBuff 输出视频帧缓冲区
	@param gotFrame [out] 是否获取到一帧，置Mark标记为则为true
	@return 
	*/
	int unpackageVideo(unsigned char* inBuff, int inBuffLen,
		unsigned char* outBuff, int* gotFrame);
	/**
	@brief 音频解包(RTP->AudioFrame).
	
	@param inBuff RTP缓冲区
	@param inBuffLen RTP长度
	@param outBuff 输出视频帧缓冲区
	@return 成功则返回1，否则0
	*/
  int unpackageAudio(unsigned char* inBuff, int inBuffLen,
		unsigned char* outBuff);

  void closeSink();
  void close();

private:
  // 是否初始化
	int audioDecoder;
	int videoDecoder;

  double lastVideoTs_;

	MediaInfo mediaInfo;
  // 是否初始化
	int audioUnpackager;
	int videoUnpackager;

	int gotUnpackagedFrame_;
	int upackagedSize_;

	unsigned char* decodedBuffer_;
	unsigned char* unpackagedBuffer_;
  unsigned char* unpackagedBufferPtr_;

	unsigned char* decodedAudioBuffer_;
	unsigned char* unpackagedAudioBuffer_;

	AVCodec* aDecoder;
	AVCodecContext* aDecoderContext;

  VideoDecoder vDecoder;

	RTPInfo* vRTPInfo;
	RawDataReceiver* rawReceiver_;

	erizo::RtpVP8Parser pars;

	bool initAudioDecoder();

	bool initAudioUnpackager();
	bool initVideoUnpackager();
	int deliverAudioData_(char* buf, int len);
	int deliverVideoData_(char* buf, int len);

	int decodeAudio(unsigned char* inBuff, int inBuffLen,
			unsigned char* outBuff);

};
/*
输出处理类（与上类相反）
接受原始音视频流，编码，分包，然后形成RTP包回调 RTPDataReceiver
@note 当前视频也只支持VP8, 且音频处理仍有问题
*/
class OutputProcessor: public RawDataReceiver {
	DECLARE_LOGGER();
public:
	OutputProcessor();
	virtual ~OutputProcessor();

	/**
	@brief 初始化.
	
	@param info 编码媒体信息
	@param rtpReceiver RTP数据回调
	@return 
	*/
	int init(const MediaInfo& info, RTPDataReceiver* rtpReceiver);
  void close();

	// implement for RawDataReceiver(for data input)
	void receiveRawData(RawDataPacket& packet);
  
  /**
  @brief RTP音频封包.
  增加RTP头部.
  @param inBuff 编码后的音频抱 
  @param inBuffLen 音频包长度
  @param outBuff [out] RTP缓冲区 
  @param pts [in] 时间戳，可以为0
  @return RTP包长度(包含头部)
  */
  int packageAudio(unsigned char* inBuff, int inBuffLen,
			unsigned char* outBuff, long int pts = 0);

	int packageVideo(unsigned char* inBuff, int buffSize, 
		unsigned char* outBuff, long int pts = 0);

private:

	int audioCoder;
	int videoCoder;

	int audioPackager;
	int videoPackager;

	unsigned int videoSeqnum_;
  unsigned int audioSeqnum_;

	unsigned long timestamp_;

	unsigned char* encodedBuffer_;
	unsigned char* packagedBuffer_;
	unsigned char* rtpBuffer_;

	unsigned char* encodedAudioBuffer_;
	unsigned char* packagedAudioBuffer_;

	MediaInfo mediaInfo;

	RTPDataReceiver* rtpReceiver_;

	AVCodec* aCoder;
	AVCodecContext* aCoderContext;

  VideoEncoder vCoder;

	RTPInfo* vRTPInfo_;
	RTPSink* sink_;

  RtpVP8Parser pars;

	bool initAudioCoder();

	bool initAudioPackager();
	bool initVideoPackager();
  // @todo 此函数写得不对.
	int encodeAudio(unsigned char* inBuff, int nSamples,
			AVPacket* pkt);

};
} /* namespace erizo */

#endif /* MEDIAPROCESSOR_H_ */
