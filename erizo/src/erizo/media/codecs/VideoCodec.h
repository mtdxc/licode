/**
 * VideoCodec.h
 */

#ifndef ERIZO_SRC_ERIZO_MEDIA_CODECS_VIDEOCODEC_H_
#define ERIZO_SRC_ERIZO_MEDIA_CODECS_VIDEOCODEC_H_

#include "Codecs.h"
#include "logger.h"
// Forward Declarations

struct AVCodec;
struct AVCodecContext;
struct AVFrame;

namespace erizo {

class VideoEncoder {
  DECLARE_LOGGER();

 public:
  VideoEncoder();
  virtual ~VideoEncoder();

  int initEncoder(const VideoCodecInfo& info);
  int encodeVideo(unsigned char* inBuffer, int length, unsigned char* outBuffer, int outLength);
  int closeEncoder();

 private:
  AVCodec* vCoder;
  AVCodecContext* vCoderContext;
  AVFrame* cPicture;
};

class VideoDecoder {
  DECLARE_LOGGER();

 public:
  VideoDecoder();
  virtual ~VideoDecoder();

  int initDecoder(const VideoCodecInfo& info);
  int initDecoder(AVCodecContext* context);
  int decodeVideo(unsigned char* inBuff, int inBuffLen,
      unsigned char* outBuff, int outBuffLen, int* gotFrame);
  int closeDecoder();

 private:
  bool initWithContext_;
  AVCodec* vDecoder;
  AVCodecContext* vDecoderContext;
  AVFrame* dPicture;
};

}  // namespace erizo
#endif  // ERIZO_SRC_ERIZO_MEDIA_CODECS_VIDEOCODEC_H_
