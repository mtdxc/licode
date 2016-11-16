
/*
 * RtpSource.cpp
 *
 *  Created on: Aug 2, 2012
 *      Author: pedro
 */

#include "rtp/RtpSource.h"
#include "thread/Worker.h"
using boost::asio::ip::udp;

namespace erizo {
DEFINE_LOGGER(RtpSource, "rtp.RtpSource");

RtpSource::RtpSource(Worker* work, const int mediaPort, const std::string& feedbackDir, const std::string& feedbackPort) {
  socket_.reset(new udp::socket(work->io_service(), udp::endpoint(udp::v4(), mediaPort)));
  resolver_.reset(new udp::resolver(work->io_service()));
  fbSocket_.reset(new udp::socket(work->io_service(), udp::endpoint(udp::v4(), 0)));
  query_.reset(new udp::resolver::query(udp::v4(), feedbackDir.c_str(), feedbackPort.c_str()));
  iterator_ = resolver_->resolve(*query_);
  boost::asio::ip::udp::endpoint sender_endpoint;
  socket_->async_receive_from(boost::asio::buffer(buffer_, LENGTH), sender_endpoint,
      boost::bind(&RtpSource::handleReceive, this, boost::asio::placeholders::error,
        boost::asio::placeholders::bytes_transferred));
}

RtpSource::~RtpSource() {
}

int RtpSource::deliverFeedback_(packetPtr fb_packet) {
  fbSocket_->send_to(boost::asio::buffer(fb_packet->data, fb_packet->length), *iterator_);
  return fb_packet->length;
}

void RtpSource::handleReceive(const::boost::system::error_code& error, size_t bytes_recvd) { // NOLINT
  if (bytes_recvd > 0 && this->video_sink_) {
    this->video_sink_->deliverVideoData(std::make_shared<dataPacket>(0, reinterpret_cast<char*>(buffer_),
          static_cast<int>(bytes_recvd), OTHER_PACKET));
  }
}

}  // namespace erizo
