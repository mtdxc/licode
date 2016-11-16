#ifndef ERIZO_SRC_TEST_UTILS_TOOLS_H_
#define ERIZO_SRC_TEST_UTILS_TOOLS_H_

#include <rtp/RtpHeaders.h>
#include <MediaDefinitions.h>
#include <Stats.h>

#include <queue>
#include <string>
#include <vector>

namespace erizo {

static constexpr uint16_t kVideoSsrc = 1;
static constexpr uint16_t kAudioSsrc = 2;
static constexpr uint16_t kArbitrarySeqNumber = 12;
static constexpr uint16_t kFirstSequenceNumber = 0;
static constexpr uint16_t kLastSequenceNumber = 65535;

inline unsigned char change_bit(unsigned char val, int num, bool bitval) {
  return (val & ~(1 << num)) | (bitval << num);
}

class PacketTools {
 public:
  static packetPtr createDataPacket(uint16_t seq_number, packetType type) {
    packetPtr p = std::make_shared<DataPacket>(0, nullptr, sizeof(erizo::RtpHeader), type);
    erizo::RtpHeader *header = (erizo::RtpHeader*)p->data;
    header->setSeqNumber(seq_number);

    if (type == AUDIO_PACKET) {
      header->setSSRC(kAudioSsrc);
    } else {
      header->setSSRC(kVideoSsrc);
    }

    return p;
  }

  static packetPtr createNack(uint32_t ssrc, uint32_t source_ssrc, uint16_t seq_number,
                                                packetType type, int additional_packets = 0) {
    packetPtr p = std::make_shared<DataPacket>(0, nullptr, 0, type);
    erizo::RtcpHeader *nack = (erizo::RtcpHeader*)p->data;
    nack->setPacketType(RTCP_RTP_Feedback_PT);
    nack->setBlockCount(1);
    nack->setSSRC(ssrc);
    nack->setSourceSSRC(source_ssrc);
    nack->setNackPid(seq_number);
    nack->setNackBlp(additional_packets);
    nack->setLength(3);
    p->length = nack->getSize();
    return p;
  }

  static packetPtr createReceiverReport(uint32_t ssrc, uint32_t source_ssrc,
                                                          uint16_t highest_seq_num, packetType type,
                                                          uint32_t last_sender_report = 0, uint32_t fraction_lost = 0) {
    packetPtr p = std::make_shared<DataPacket>(0, nullptr, 0, type);
    erizo::RtcpHeader *receiver_report = (erizo::RtcpHeader*)p->data;
    receiver_report->setPacketType(RTCP_Receiver_PT);
    receiver_report->setBlockCount(1);
    receiver_report->setSSRC(ssrc);
    receiver_report->setSourceSSRC(source_ssrc);
    receiver_report->setHighestSeqnum(highest_seq_num);
    receiver_report->setLastSr(last_sender_report);
    receiver_report->setFractionLost(fraction_lost);
    receiver_report->setLength(7);
    p->length = receiver_report->getSize();
    return p;
  }

  static packetPtr createSenderReport(uint32_t ssrc, packetType type,
      uint32_t packets_sent = 0, uint32_t octets_sent = 0, uint64_t ntp_timestamp = 0) {
    packetPtr p = std::make_shared<DataPacket>(0, nullptr, 0, type);
    erizo::RtcpHeader *sender_report = (erizo::RtcpHeader*)p->data;
    sender_report->setPacketType(RTCP_Sender_PT);
    sender_report->setBlockCount(1);
    sender_report->setSSRC(ssrc);
    sender_report->setLength(6);
    sender_report->setNtpTimestamp(ntp_timestamp);
    sender_report->setPacketsSent(packets_sent);
    sender_report->setOctetsSent(octets_sent);
    p->length = sender_report->getSize();
    return p;
  }

  static packetPtr createVP8Packet(uint16_t seq_number, bool is_keyframe, bool is_marker) {
    auto packet = std::make_shared<DataPacket>(0, nullptr, 200, VIDEO_PACKET);
    packet->is_keyframe = is_keyframe;
    erizo::RtpHeader *header = (erizo::RtpHeader*)packet->data;
    header->setPayloadType(96);
    header->setSeqNumber(seq_number);
    header->setSSRC(kVideoSsrc);
    header->setMarker(is_marker);

    char* parsing_pointer = packet->data + header->getHeaderLength();
    *parsing_pointer = 0x10;
    parsing_pointer++;
    *parsing_pointer = is_keyframe? 0x00: 0x01;
    return packet;
  }

  static packetPtr createVP8Packet(uint16_t seq_number, uint32_t timestamp,
      bool is_keyframe, bool is_marker) {
    auto packet = std::make_shared<DataPacket>(0, nullptr, 200, VIDEO_PACKET);
    packet->is_keyframe = is_keyframe;
    erizo::RtpHeader *header = (erizo::RtpHeader*)packet->data;
    header->setPayloadType(96);
    header->setSeqNumber(seq_number);
    header->setSSRC(kVideoSsrc);
    header->setTimestamp(timestamp);
    header->setMarker(is_marker);

    char* parsing_pointer = packet->data + header->getHeaderLength();
    *parsing_pointer = 0x10;
    parsing_pointer++;
    *parsing_pointer = is_keyframe? 0x00: 0x01;
    return packet;
  }

  static packetPtr createH264SingleNalPacket(uint16_t seq_number, uint32_t timestamp,
      bool is_keyframe) {
    auto packet = std::make_shared<DataPacket>(0, nullptr, 200, VIDEO_PACKET);
    packet->is_keyframe = is_keyframe;
    erizo::RtpHeader *header = (erizo::RtpHeader*)packet->data;
    header->setPayloadType(97);
    header->setSeqNumber(seq_number);
    header->setSSRC(kVideoSsrc);
    header->setTimestamp(timestamp);

    char* parsing_pointer = packet->data + header->getHeaderLength();
    *parsing_pointer = is_keyframe ? 0x5 : 0x1;
    return packet;
  }

  static packetPtr createH264AggregatedPacket(uint16_t seq_number, uint32_t timestamp,
      uint8_t nal_1_length, uint8_t nal_2_length) {
    int packet_length = nal_1_length + nal_2_length + 17;  // 17 = 12 rtp header + 1 stap header +
                                                           // 2 nalu_1 header + 2 nalu_2 header
    auto packet = std::make_shared<DataPacket>(0, nullptr, packet_length, VIDEO_PACKET);
    erizo::RtpHeader *header = (erizo::RtpHeader*)packet->data;
    header->setPayloadType(97);
    header->setSeqNumber(seq_number);
    header->setSSRC(kVideoSsrc);
    header->setTimestamp(timestamp);

    unsigned char* ptr = (unsigned char*)packet->data + header->getHeaderLength();

    const int nal_1_len = nal_1_length;
    const int nal_2_len = nal_2_length;

    *ptr = 24;
    ++ptr;  // step out stap header
    ++ptr;
    *ptr = nal_1_length;
    ++ptr;  // step out nalu size field
    ptr += nal_1_len;
    ++ptr;
    *ptr = nal_2_length;
    ++ptr;  // step out nalu size field
    ptr += nal_2_len;

    return packet;
  }

  static packetPtr createH264FragmentedPacket(uint16_t seq_number, uint32_t timestamp,
      bool is_start, bool is_end, bool is_keyframe) {
    auto packet = std::make_shared<DataPacket>(0, nullptr, 200, VIDEO_PACKET);
    packet->is_keyframe = is_keyframe;
    erizo::RtpHeader *header = (erizo::RtpHeader*)packet->data;
    header->setPayloadType(97);
    header->setSeqNumber(seq_number);
    header->setSSRC(kVideoSsrc);
    header->setTimestamp(timestamp);
    unsigned char* ptr = (unsigned char*)packet->data + header->getHeaderLength();
    *ptr = 28;

    ++ptr; // reach fu_header
    *ptr = is_keyframe ? 0x5 : 0x1;
    *ptr = change_bit(*ptr, 7, is_start);
    *ptr = change_bit(*ptr, 6, is_end);
    return packet;
  }

  static packetPtr createVP9Packet(uint16_t seq_number, bool is_keyframe, bool is_marker) {
    auto packet = std::make_shared<DataPacket>(0, nullptr, 200, VIDEO_PACKET);
    packet->is_keyframe = is_keyframe;
    erizo::RtpHeader *header = (erizo::RtpHeader*)packet->data;
    header->setPayloadType(98);
    header->setSeqNumber(seq_number);
    header->setSSRC(kVideoSsrc);
    header->setMarker(is_marker);
    char* parsing_pointer = packet->data + header->getHeaderLength();
    *parsing_pointer = is_keyframe? 0x00: 0x40;
    return packet;
  }

  static std::shared_ptr<erizo::DataPacket> createRembPacket(uint32_t bitrate) {
    auto packet = std::make_shared<DataPacket>(0, nullptr, 0, erizo::OTHER_PACKET);
    erizo::RtcpHeader *remb_packet = (erizo::RtcpHeader*) packet->data;
    remb_packet->setPacketType(RTCP_PS_Feedback_PT);
    remb_packet->setBlockCount(RTCP_AFB);
    memcpy(&remb_packet->report.rembPacket.uniqueid, "REMB", 4);

    remb_packet->setSSRC(2);
    remb_packet->setSourceSSRC(1);
    remb_packet->setLength(5);
    remb_packet->setREMBBitRate(bitrate);
    remb_packet->setREMBNumSSRC(1);
    remb_packet->setREMBFeedSSRC(55554);
    packet->length = remb_packet->getSize();
    return packet;
  }

  static std::shared_ptr<erizo::DataPacket> createPLI() {
    auto packet = std::make_shared<DataPacket>(0, nullptr, 0, erizo::OTHER_PACKET);
    erizo::RtcpHeader *pli = (erizo::RtcpHeader*) packet->data;
    pli->setPacketType(RTCP_PS_Feedback_PT);
    pli->setBlockCount(1);
    pli->setSSRC(55554);
    pli->setSourceSSRC(1);
    pli->setLength(2);
    packet->length = pli->getSize();
    return packet;
  }
};

using ::testing::_;

class BaseHandlerTest  {
 public:
  BaseHandlerTest() {}

  virtual void setHandler() = 0;
  virtual void afterPipelineSetup() {}

  virtual void internalSetUp() {
    simulated_clock = std::make_shared<erizo::SimulatedClock>();
    //simulated_worker = std::make_shared<erizo::SimulatedWorker>(simulated_clock);
    //simulated_worker->start();
    io_worker = std::make_shared<erizo::IOWorker>();
    io_worker->start();
    //connection = std::make_shared<erizo::MockWebRtcConnection>(simulated_worker, io_worker, ice_config, rtp_maps);
    media_stream = std::make_shared<erizo::MockMediaStream>(connection, "", rtp_maps);
    processor = std::make_shared<erizo::MockRtcpProcessor>();
    quality_manager = std::make_shared<erizo::MockQualityManager>();
    packet_buffer_service = std::make_shared<erizo::PacketBufferService>();
    stats = std::make_shared<erizo::Stats>();
    media_stream->setVideoSinkSSRC(erizo::kVideoSsrc);
    media_stream->setAudioSinkSSRC(erizo::kAudioSsrc);
    media_stream->setVideoSourceSSRC(erizo::kVideoSsrc);
    media_stream->setAudioSourceSSRC(erizo::kAudioSsrc);

    pipeline = Pipeline::create();
    reader = std::make_shared<erizo::Reader>();
    writer = std::make_shared<erizo::Writer>();

    EXPECT_CALL(*reader, notifyUpdate()).Times(testing::AtLeast(1));
    EXPECT_CALL(*writer, notifyUpdate()).Times(testing::AtLeast(1));

    EXPECT_CALL(*reader, read(_, _)).Times(testing::AtLeast(0));
    EXPECT_CALL(*writer, write(_, _)).Times(testing::AtLeast(0));

    std::shared_ptr<erizo::WebRtcConnection> connection_ptr = std::dynamic_pointer_cast<WebRtcConnection>(connection);
    std::shared_ptr<erizo::MediaStream> stream_ptr = std::dynamic_pointer_cast<MediaStream>(media_stream);
    pipeline->addService(stream_ptr);
    pipeline->addService(std::dynamic_pointer_cast<RtcpProcessor>(processor));
    pipeline->addService(std::dynamic_pointer_cast<QualityManager>(quality_manager));
    pipeline->addService(packet_buffer_service);
    pipeline->addService(stats);

    pipeline->addBack(writer);
    setHandler();
    pipeline->addBack(reader);
    pipeline->finalize();
    afterPipelineSetup();
  }

  virtual void executeTasksInNextMs(int time) {
    for (int step = 0; step < time + 1; step++) {
      //simulated_worker->executePastScheduledTasks();
      simulated_clock->advanceTime(std::chrono::milliseconds(1));
    }
  }

  virtual void internalTearDown() {
  }

  IceConfig ice_config;
  std::vector<RtpMap> rtp_maps;
  std::shared_ptr<erizo::Stats> stats;
  std::shared_ptr<erizo::MockWebRtcConnection> connection;
  std::shared_ptr<erizo::MockMediaStream> media_stream;
  std::shared_ptr<erizo::MockRtcpProcessor> processor;
  std::shared_ptr<erizo::MockQualityManager> quality_manager;
  Pipeline::Ptr pipeline;
  std::shared_ptr<erizo::Reader> reader;
  std::shared_ptr<erizo::Writer> writer;
  std::shared_ptr<erizo::SimulatedClock> simulated_clock;
  //std::shared_ptr<erizo::SimulatedWorker> simulated_worker;
  std::shared_ptr<erizo::IOWorker> io_worker;
  std::shared_ptr<erizo::PacketBufferService> packet_buffer_service;
  std::queue<packetPtr> packet_queue;
};

class HandlerTest : public ::testing::Test, public BaseHandlerTest {
 public:
  HandlerTest() {}

  virtual void setHandler() = 0;

 protected:
  virtual void SetUp() {
    internalSetUp();
  }

  virtual void TearDown() {
    internalTearDown();
  }
};

}  // namespace erizo

#endif  // ERIZO_SRC_TEST_UTILS_TOOLS_H_
