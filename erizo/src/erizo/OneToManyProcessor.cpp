/*
 * OneToManyProcessor.cpp
 */

#include "OneToManyProcessor.h"

#include <map>
#include <string>

#include "WebRtcConnection.h"
#include "rtp/RtpHeaders.h"

namespace erizo {
  DEFINE_LOGGER(OneToManyProcessor, "OneToManyProcessor");
  OneToManyProcessor::OneToManyProcessor() : feedbackSink_{nullptr} {
    ELOG_DEBUG("OneToManyProcessor constructor");
  }

  OneToManyProcessor::~OneToManyProcessor() {
  }

  int OneToManyProcessor::deliverAudioData_(packetPtr audio_packet) {
    if (audio_packet->length <= 0)
      return 0;

    std::unique_lock<std::mutex> lock(monitor_mutex_);
    if (subscribers.empty())
      return 0;

    std::map<std::string, sink_ptr>::iterator it;
    for (it = subscribers.begin(); it != subscribers.end(); ++it) {
      if ((*it).second != nullptr) {
        (*it).second->deliverAudioData(audio_packet);
      }
    }

    return 0;
  }

  int OneToManyProcessor::deliverVideoData_(packetPtr video_packet) {
    if (video_packet->length <= 0)
      return 0;
    RtcpHeader* head = reinterpret_cast<RtcpHeader*>(video_packet->data);
    if (head->isFeedback()) {
      ELOG_WARN("Receiving Feedback in wrong path: %d", head->packettype);
      if (feedbackSink_ != nullptr) {
        head->ssrc = htonl(publisher->getVideoSourceSSRC());
        feedbackSink_->deliverFeedback(video_packet);
      }
      return 0;
    }
    std::unique_lock<std::mutex> lock(monitor_mutex_);
    if (subscribers.empty())
      return 0;
    std::map<std::string, sink_ptr>::iterator it;
    for (it = subscribers.begin(); it != subscribers.end(); ++it) {
      if ((*it).second != nullptr) {
        (*it).second->deliverVideoData(video_packet);
      }
    }
    return 0;
  }

  void OneToManyProcessor::setPublisher(std::shared_ptr<MediaSource> webRtcConn) {
    std::unique_lock<std::mutex> lock(monitor_mutex_);
    this->publisher = webRtcConn;
    feedbackSink_ = publisher->getFeedbackSink();
  }

  int OneToManyProcessor::deliverFeedback_(packetPtr fb_packet) {
    if (feedbackSink_ != nullptr) {
      feedbackSink_->deliverFeedback(fb_packet);
    }
    return 0;
  }

  void OneToManyProcessor::addSubscriber(std::shared_ptr<MediaSink> webRtcConn,
      const std::string& peerId) {
    std::unique_lock<std::mutex> lock(monitor_mutex_);
    if (!publisher) {
      ELOG_WARN("without publisher can't addSubscriber %s, call setPublisher first", peerId.c_str());
      return;
    }
    ELOG_DEBUG("Adding subscriber %s", peerId.c_str());
    webRtcConn->setAudioSinkSSRC(publisher->getAudioSourceSSRC());
    webRtcConn->setVideoSinkSSRC(publisher->getVideoSourceSSRC());
    ELOG_DEBUG("Subscribers ssrcs: Audio %u, video, %u from %u, %u ",
               webRtcConn->getAudioSinkSSRC(), webRtcConn->getVideoSinkSSRC(),
               publisher->getAudioSourceSSRC() , publisher->getVideoSourceSSRC());

    FeedbackSource* fbsource = webRtcConn->getFeedbackSource();
    if (fbsource != nullptr) {
      ELOG_DEBUG("adding fbsource");
      fbsource->setFeedbackSink(this);
    }
    if (subscribers.find(peerId) != subscribers.end()) {
        ELOG_WARN("This OTM already has a subscriber with peerId %s, substituting it", peerId.c_str());
        subscribers.erase(peerId);
    }
    subscribers[peerId] = webRtcConn;
  }

  void OneToManyProcessor::removeSubscriber(const std::string& peerId) {
    ELOG_DEBUG("Remove subscriber %s", peerId.c_str());
	std::unique_lock<std::mutex> lock(monitor_mutex_);
    if (this->subscribers.find(peerId) != subscribers.end()) {
      deleteAsync(std::dynamic_pointer_cast<WebRtcConnection>(subscribers.find(peerId)->second));
      this->subscribers.erase(peerId);
    }
  }

  std::future<void> OneToManyProcessor::deleteAsync(std::shared_ptr<WebRtcConnection> connection) {
    auto promise = std::make_shared<std::promise<void>>();
    if (connection) {
      connection->getWorker()->task([promise, connection] {
        connection->close();
        promise->set_value();
      });
    } else {
      promise->set_value();
    }
    return promise->get_future();
  }

  void OneToManyProcessor::close() {
    closeAll();
  }

  void OneToManyProcessor::closeAll() {
    ELOG_DEBUG("OneToManyProcessor closeAll");
    feedbackSink_ = nullptr;
    if (publisher.get()) {
      std::future<void> future = deleteAsync(std::dynamic_pointer_cast<WebRtcConnection>(publisher));
      future.wait();
    }
    publisher.reset();
    std::unique_lock<std::mutex> lock(monitor_mutex_);
    std::map<std::string, sink_ptr>::iterator it = subscribers.begin();
    while (it != subscribers.end()) {
      if ((*it).second != nullptr) {
        std::future<void> future = deleteAsync(std::dynamic_pointer_cast<WebRtcConnection>((*it).second));
        future.wait();
      }
      subscribers.erase(it++);
    }
    subscribers.clear();
    ELOG_DEBUG("ClosedAll media in this OneToMany");
  }

}  // namespace erizo
