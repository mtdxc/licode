#include <string>
#include "rtp/StatsHandler.h"
#include "MediaDefinitions.h"
#include "WebRtcConnection.h"

namespace erizo {

DEFINE_LOGGER(StatsCalculator, "rtp.StatsCalculator");
DEFINE_LOGGER(IncomingStatsHandler, "rtp.IncomingStatsHandler");
DEFINE_LOGGER(OutgoingStatsHandler, "rtp.OutgoingStatsHandler");

void StatsCalculator::update(WebRtcConnection *connection, std::shared_ptr<Stats> stats) {
  if (!connection_) {
    connection_ = connection;
    stats_ = stats;
    if (!getStatsInfo().hasChild("total")) {
      getStatsInfo()["total"].insertStat("bitrateCalculated", 
        MovingIntervalRateStat(kRateStatIntervalSize, kRateStatIntervals, 8.));
    }
  }
}

void StatsCalculator::processPacket(packetPtr packet) {
  RtcpHeader *chead = reinterpret_cast<RtcpHeader*> (packet->data);
  if (chead->isRtcp()) {
    processRtcpPacket(packet);
  } else {
    processRtpPacket(packet);
  }
}

void StatsCalculator::processRtpPacket(packetPtr packet) {
  char* buf = packet->data;
  int len = packet->length;
  RtpHeader* head = reinterpret_cast<RtpHeader*>(buf);
  uint32_t ssrc = head->getSSRC();
  if (!connection_->isSinkSSRC(ssrc) && !connection_->isSourceSSRC(ssrc)) {
    ELOG_DEBUG("message: Unknown SSRC in processRtpPacket, ssrc: %u, PT: %u", ssrc, head->getPayloadType());
    return;
  }
  StatNode& ssrc_stat = getStatsInfo()[ssrc];
  if (!ssrc_stat.hasChild("bitrateCalculated")) {
    if (connection_->isVideoSourceSSRC(ssrc) || connection_->isVideoSinkSSRC(ssrc)) {
      ssrc_stat.insertStat("type", StringStat("video"));
    } else if (connection_->isAudioSourceSSRC(ssrc) || connection_->isAudioSinkSSRC(ssrc)) {
      ssrc_stat.insertStat("type", StringStat("audio"));
    }
    ssrc_stat.insertStat("bitrateCalculated", MovingIntervalRateStat{kRateStatIntervalSize, kRateStatIntervals, 8.});
  }
  ssrc_stat["bitrateCalculated"] += len;
  getStatsInfo()["total"]["bitrateCalculated"] += len;
  if (packet->type == VIDEO_PACKET && packet->is_keyframe) {
    incrStat(ssrc, "keyFrames");
  }
}

void StatsCalculator::incrStat(uint32_t ssrc, std::string stat) {
  getStatsInfo()[ssrc].addStat<CumulativeStat>(stat, 1);
}

void StatsCalculator::processRtcpPacket(packetPtr packet) {
  char* buf = packet->data;
  int len = packet->length;

  uint32_t ssrc = 0;

  bool is_feedback_on_publisher = false;
  RtcpHeader *chead = reinterpret_cast<RtcpHeader*>(buf);
  if (chead->isFeedback()) {
    ssrc = chead->getSourceSSRC();
    if (!connection_->isSinkSSRC(ssrc)) {
      is_feedback_on_publisher = true;
    }
  } else {
    ssrc = chead->getSSRC();
    if (!connection_->isSourceSSRC(ssrc)) {
      return;
    }
  }

  ELOG_DEBUG("RTCP packet received, type: %u, size: %u, packetLength: %u", chead->getPacketType(),
       chead->getPacketSize(), len);
  StatNode& ssrc_stat = getStatsInfo()[ssrc];
  RtcpAccessor access(buf, len);
  while (chead = access.nextRtcp())
  {
    ELOG_DEBUG("RTCP SubPacket: PT %d, SSRC %u, sourceSSRC %u, block count %d",
        chead->packettype, chead->getSSRC(), chead->getSourceSSRC(), chead->getBlockCount());
    switch (chead->packettype) {
      case RTCP_SDES_PT:
        ELOG_DEBUG("SDES");
        break;
      case RTCP_BYE:
        ELOG_DEBUG("RTCP BYE");
        break;
      case RTCP_Receiver_PT:
        if (is_feedback_on_publisher) {
          break;
        }
        ELOG_DEBUG("RTP RR: Fraction Lost %u, packetsLost %u", chead->getFractionLost(), chead->getLostPackets());
        ssrc_stat.setStat<CumulativeStat>("fractionLost", chead->getFractionLost());
        ssrc_stat.setStat<CumulativeStat>("packetsLost", chead->getLostPackets());
        ssrc_stat.setStat<CumulativeStat>("jitter", chead->getJitter());
        ssrc_stat.setStat<CumulativeStat>("sourceSsrc", ssrc);
        break;
      case RTCP_Sender_PT:
        ELOG_DEBUG("RTP SR: Packets Sent %u, Octets Sent %u", chead->getPacketsSent(), chead->getOctetsSent());
        ssrc_stat.setStat<CumulativeStat>("packetsSent", chead->getPacketsSent());
        ssrc_stat.setStat<CumulativeStat>("bytesSent", chead->getOctetsSent());
        break;
      case RTCP_RTP_Feedback_PT:
        ELOG_DEBUG("RTP FB: Usually NACKs: %u", chead->getBlockCount());
        ELOG_DEBUG("PID %u BLP %u", chead->getNackPid(), chead->getNackBlp());
        incrStat(ssrc, "NACK");
        break;
      case RTCP_PS_Feedback_PT:
        ELOG_DEBUG("RTCP PS FB TYPE: %u", chead->getBlockCount() );
        switch (chead->getBlockCount()) {
          case RTCP_PLI_FMT:
            ELOG_DEBUG("PLI Packet, SSRC %u, sourceSSRC %u", chead->getSSRC(), chead->getSourceSSRC());
            incrStat(ssrc, "PLI");
            break;
          case RTCP_SLI_FMT:
            ELOG_DEBUG("SLI Message");
            incrStat(ssrc, "SLI");
            break;
          case RTCP_FIR_FMT:
            ELOG_DEBUG("FIR Packet, SSRC %u, sourceSSRC %u", chead->getSSRC(), chead->getSourceSSRC());
            incrStat(ssrc, "FIR");
            break;
          case RTCP_AFB:
            {
              if (is_feedback_on_publisher) {
                break;
              }
              ELOG_DEBUG("REMB Packet, SSRC %u, sourceSSRC %u", chead->getSSRC(), chead->getSourceSSRC());
              char *uniqueId = reinterpret_cast<char*>(&chead->report.rembPacket.uniqueid);
              if (!strncmp(uniqueId, "REMB", 4)) {
                uint64_t bitrate = chead->getREMBBitRate();
                // ELOG_DEBUG("REMB Packet numSSRC %u mantissa %u exp %u, tot %lu bps",
                //             chead->getREMBNumSSRC(), chead->getBrMantis(), chead->getBrExp(), bitrate);
                ssrc_stat.setStat<CumulativeStat>("bandwidth", bitrate);
              } else {
                ELOG_DEBUG("Unsupported AFB Packet not REMB")
              }
              break;
            }
          default:
            ELOG_WARN("Unsupported RTCP_PS FB TYPE %u", chead->getBlockCount());
            break;
        }
        break;
      default:
        ELOG_DEBUG("Unknown RTCP Packet, %d", chead->packettype);
        break;
    }
  }
  notifyStats();
}

IncomingStatsHandler::IncomingStatsHandler() : connection_{nullptr} {}

void IncomingStatsHandler::enable() {}

void IncomingStatsHandler::disable() {}

void IncomingStatsHandler::notifyUpdate() {
  if (connection_) {
    return;
  }
  auto pipeline = getContext()->getPipelineShared();
  update(pipeline->getService<WebRtcConnection>().get(), pipeline->getService<Stats>());
}

void IncomingStatsHandler::read(Context *ctx, packetPtr packet) {
  processPacket(packet);
  ctx->fireRead(packet);
}

OutgoingStatsHandler::OutgoingStatsHandler() : connection_{nullptr} {}

void OutgoingStatsHandler::enable() {}

void OutgoingStatsHandler::disable() {}

void OutgoingStatsHandler::notifyUpdate() {
  if (connection_) {
    return;
  }
  auto pipeline = getContext()->getPipelineShared();
  update(pipeline->getService<WebRtcConnection>().get(), pipeline->getService<Stats>());
}

void OutgoingStatsHandler::write(Context *ctx, packetPtr packet) {
  processPacket(packet);
  ctx->fireWrite(packet);
}

}  // namespace erizo
