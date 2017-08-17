/**
* AudioCodec.h
*/
#ifndef ERIZO_SRC_ERIZO_MEDIA_CODECS_AUDIOCODEC_H_
#define ERIZO_SRC_ERIZO_MEDIA_CODECS_AUDIOCODEC_H_

#include "Codecs.h"
struct AVCodec;
struct AVCodecContext;
struct AVFrame;
struct AVPacket;
namespace erizo {

class AudioEncoder {
  DECLARE_LOGGER();

 public:
  AudioEncoder();
  virtual ~AudioEncoder();
  int initEncoder(const AudioCodecInfo& info);
  int encodeAudio(unsigned char* inBuffer, int nSamples, AVPacket* pkt);
  int closeEncoder();

 private:
  AVCodec* aCoder_;
  AVCodecContext* aCoderContext_;
  AVFrame* aFrame_;
};

class AudioDecoder {
  DECLARE_LOGGER();

 public:
  AudioDecoder();
  virtual ~AudioDecoder();

  int initDecoder(const AudioCodecInfo& info);
  int initDecoder(AVCodecContext* context);
  int decodeAudio(unsigned char* inBuff, int inBuffLen, unsigned char* outBuff, int outBuffLen, int* gotFrame);
  int closeDecoder();

 private:
  AVCodec* aDecoder_;
  AVCodecContext* aDecoderContext_;
  AVFrame* dFrame_;
};

}  // namespace erizo
#endif  // ERIZO_SRC_ERIZO_MEDIA_CODECS_AUDIOCODEC_H_
