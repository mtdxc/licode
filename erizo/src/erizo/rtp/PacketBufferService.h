#ifndef ERIZO_SRC_ERIZO_RTP_PACKETBUFFERSERVICE_H_
#define ERIZO_SRC_ERIZO_RTP_PACKETBUFFERSERVICE_H_

#include "logger.h"
#include "MediaDefinitions.h"
#include "pipeline/Service.h"

static constexpr uint16_t kServicePacketBufferSize = 256;

namespace erizo {

class PacketBufferService: public Service {
 public:
  DECLARE_LOGGER();

  PacketBufferService();
  ~PacketBufferService() {}

  void insertPacket(packetPtr packet);

  packetPtr getVideoPacket(uint16_t seq_num);
  packetPtr getAudioPacket(uint16_t seq_num);

 private:
	PacketBufferService(const PacketBufferService&& service);

  // Ϊ���ܿ���,������ϵ�к��Ƿ���ͬ
  inline uint16_t getIndexInBuffer(uint16_t seq_num) {
    return seq_num % kServicePacketBufferSize;
  }
 private:
  std::vector<packetPtr> audio_;
  std::vector<packetPtr> video_;
};

}  // namespace erizo
#endif  // ERIZO_SRC_ERIZO_RTP_PACKETBUFFERSERVICE_H_
