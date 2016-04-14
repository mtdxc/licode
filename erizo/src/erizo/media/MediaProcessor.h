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
/// ԭʼ����Ƶ����
struct RawDataPacket {
	unsigned char* data; ///< ������Ƶ����YUV����Ƶ��PCM 16
	int length; ///< ԭʼ���ݳ���
	DataType type; ///< ��������(��Ƶ/��Ƶ)
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
ý�������봦��.
��MediaSink�н���RTP�������������н����
������ffmpeg�����ԭʼ֡���ڴ���RawDataReceiver�ص�
@note ��ǰ��Ƶֻ֧��VP8����.
*/
class InputProcessor: public MediaSink {
	DECLARE_LOGGER();
public:
	InputProcessor();
	virtual ~InputProcessor();

	/**
	@brief ��ʼ��.
	
	@param info MeidiaSink��ý���ʽ�����ڽ��룩
	@param receiver ԭʼ��������ص�
	@return 
	@retval 
	*/
	int init(const MediaInfo& info, RawDataReceiver* receiver);
	/**
	@brief ��Ƶ���(RTP->VideoFrame).
	
	@param inBuff RTP������
	@param inBuffLen RTP����
	@param outBuff �����Ƶ֡������
	@param gotFrame [out] �Ƿ��ȡ��һ֡����Mark���Ϊ��Ϊtrue
	@return 
	*/
	int unpackageVideo(unsigned char* inBuff, int inBuffLen,
		unsigned char* outBuff, int* gotFrame);
	/**
	@brief ��Ƶ���(RTP->AudioFrame).
	
	@param inBuff RTP������
	@param inBuffLen RTP����
	@param outBuff �����Ƶ֡������
	@return �ɹ��򷵻�1������0
	*/
  int unpackageAudio(unsigned char* inBuff, int inBuffLen,
		unsigned char* outBuff);

  void closeSink();
  void close();

private:
  // �Ƿ��ʼ��
	int audioDecoder;
	int videoDecoder;

  double lastVideoTs_;

	MediaInfo mediaInfo;
  // �Ƿ��ʼ��
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
��������ࣨ�������෴��
����ԭʼ����Ƶ�������룬�ְ���Ȼ���γ�RTP���ص� RTPDataReceiver
@note ��ǰ��ƵҲֻ֧��VP8, ����Ƶ������������
*/
class OutputProcessor: public RawDataReceiver {
	DECLARE_LOGGER();
public:
	OutputProcessor();
	virtual ~OutputProcessor();

	/**
	@brief ��ʼ��.
	
	@param info ����ý����Ϣ
	@param rtpReceiver RTP���ݻص�
	@return 
	*/
	int init(const MediaInfo& info, RTPDataReceiver* rtpReceiver);
  void close();

	// implement for RawDataReceiver(for data input)
	void receiveRawData(RawDataPacket& packet);
  
  /**
  @brief RTP��Ƶ���.
  ����RTPͷ��.
  @param inBuff ��������Ƶ�� 
  @param inBuffLen ��Ƶ������
  @param outBuff [out] RTP������ 
  @param pts [in] ʱ���������Ϊ0
  @return RTP������(����ͷ��)
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
  // @todo �˺���д�ò���.
	int encodeAudio(unsigned char* inBuff, int nSamples,
			AVPacket* pkt);

};
} /* namespace erizo */

#endif /* MEDIAPROCESSOR_H_ */
