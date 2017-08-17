/*
 * LibNiceConnection.h
 */

#ifndef ERIZO_SRC_ERIZO_LIBNICECONNECTION_H_
#define ERIZO_SRC_ERIZO_LIBNICECONNECTION_H_

#include <boost/thread.hpp>
#include <string>
#include <vector>
#include <queue>
#include <memory>
#include <map>

#include "./IceConnection.h"
#include "./MediaDefinitions.h"
#include "./SdpInfo.h"
#include "./logger.h"
#include "lib/LibNiceInterface.h"

typedef struct _NiceAgent NiceAgent;
typedef struct _GMainContext GMainContext;
typedef struct _GMainLoop GMainLoop;

typedef unsigned int uint;

namespace erizo {

#define NICE_STREAM_MAX_UFRAG   256 + 1  /* ufrag + NULL */
#define NICE_STREAM_MAX_UNAME   256 * 2 + 1 + 1 /* 2*ufrag + colon + NULL */
#define NICE_STREAM_MAX_PWD     256 + 1  /* pwd + NULL */
#define NICE_STREAM_DEF_UFRAG   4 + 1    /* ufrag + NULL */
#define NICE_STREAM_DEF_PWD     22 + 1   /* pwd + NULL */

// forward declarations
typedef std::shared_ptr<dataPacket> packetPtr;
class CandidateInfo;
class WebRtcConnection;

class LibNiceConnection : public IceConnection {
  DECLARE_LOGGER();

 public:
  LibNiceConnection(std::shared_ptr<LibNiceInterface> libnice, IceConnectionListener* listener,
    const IceConfig& ice_config);

  virtual ~LibNiceConnection();
  /**
   * Starts Gathering candidates in a new thread.
   */
  void start() override;
  bool setRemoteCandidates(const std::vector<CandidateInfo> &candidates, bool is_bundle) override;
  void gatheringDone(uint stream_id);
  void getCandidate(uint stream_id, uint component_id, const std::string &foundation);
  void setRemoteCredentials(const std::string& username, const std::string& password) override;
  int sendData(unsigned int component_id, const void* buf, int len) override;

  void updateComponentState(unsigned int component_id, IceState state);
  void onData(unsigned int component_id, char* buf, int len) override;
  CandidatePair getSelectedPair() override;
  void setReceivedLastCandidate(bool hasReceived) override;
  void close() override;

  static LibNiceConnection* create(IceConnectionListener *listener, const IceConfig& ice_config);

 private:
  void mainLoop();

 private:
  std::shared_ptr<LibNiceInterface> lib_nice_;
  NiceAgent* agent_;
  GMainContext* context_;
  GMainLoop* loop_;

  unsigned int candsDelivered_;

  std::thread m_Thread_;
  std::mutex closeMutex_;
  std::condition_variable cond_;

  bool receivedLastCandidate_;
  std::vector<CandidateInfo> local_candidates;
};

}  // namespace erizo
#endif  // ERIZO_SRC_ERIZO_LIBNICECONNECTION_H_
