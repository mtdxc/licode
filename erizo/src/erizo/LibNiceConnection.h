/*
 * LibNiceConnection.h
 */

#ifndef ERIZO_SRC_ERIZO_LIBNICECONNECTION_H_
#define ERIZO_SRC_ERIZO_LIBNICECONNECTION_H_

#include <memory>
#include <string>
#include <vector>
#include <queue>
#include <map>

#include "./IceConnection.h"
#include "./SdpInfo.h"
#include "./logger.h"
#include <condition_variable>
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

class CandidateInfo;
class WebRtcConnection;

class LibNiceConnection : public IceConnection {
  DECLARE_LOGGER();

 public:
  LibNiceConnection(const IceConfig& ice_config);

  virtual ~LibNiceConnection();
  /**
   * Starts Gathering candidates in a new thread.
   */
  void start() override;
  bool setRemoteCandidates(const std::vector<CandidateInfo> &candidates, bool is_bundle) override;
  void gatheringDone(uint32_t stream_id);
  void getCandidate(uint32_t stream_id, uint32_t component_id, const std::string &foundation);
  void setRemoteCredentials(const std::string& username, const std::string& password) override;
  int sendData(unsigned int component_id, const void* buf, int len) override;

  void updateComponentState(unsigned int component_id, IceState state);
  void onData(unsigned int component_id, char* buf, int len) override;
  CandidatePair getSelectedPair() override;
  void setReceivedLastCandidate(bool hasReceived) override;
  void close() override;

  static LibNiceConnection* create(const IceConfig& ice_config);

 private:
  void mainLoop();

 private:
  NiceAgent* agent_;
  GMainContext* context_;
  GMainLoop* loop_;

  unsigned int candsDelivered_;

  std::thread thread_;
  Mutex close_mutex_;
  std::condition_variable cond_;

  bool receivedLastCandidate_;
  std::vector<CandidateInfo> local_candidates;
};

}  // namespace erizo
#endif  // ERIZO_SRC_ERIZO_LIBNICECONNECTION_H_
