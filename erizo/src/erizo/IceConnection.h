/*
 * LibNiceConnection.h
 */

#ifndef ERIZO_SRC_ERIZO_ICECONNECTION_H_
#define ERIZO_SRC_ERIZO_ICECONNECTION_H_

#include <string>
#include <vector>
#include <queue>
#include <map>

#include "./MediaDefinitions.h"
#include "./SdpInfo.h"
#include "./logger.h"

typedef struct _NiceAgent NiceAgent;
typedef struct _GMainContext GMainContext;
typedef struct _GMainLoop GMainLoop;

namespace erizo {

// forward declarations
typedef packetPtr packetPtr;
class CandidateInfo;
class WebRtcConnection;
class IceConnection;

struct CandidatePair{
  std::string erizoCandidateIp;
  int erizoCandidatePort;
  std::string clientCandidateIp;
  int clientCandidatePort;
  std::string erizoHostType;
  std::string clientHostType;
};

class IceConfig {
 public:
    MediaType media_type;
    std::string transport_name;
    std::string connection_id;
    unsigned int ice_components;
    std::string turn_server, turn_username, turn_pass;
    std::string stun_server, network_interface;
    uint16_t stun_port, turn_port, min_port, max_port;
    bool should_trickle;
    bool use_nicer;
    IceConfig()
      : media_type{MediaType::OTHER},
        ice_components{0},
        stun_port{0},
        turn_port{0},
        min_port{0},
        max_port{0},
        should_trickle{false},
        use_nicer{false} {
    }
};

/**
 * States of ICE
 */
enum IceState {
  INITIAL, CANDIDATES_RECEIVED, READY, FINISHED, FAILED
};

class IceConnectionListener {
 public:
    virtual void onPacketReceived(packetPtr packet) = 0;
    virtual void onCandidate(const CandidateInfo &candidate, IceConnection *conn) = 0;
    virtual void updateIceState(IceState state, IceConnection *conn) = 0;
};

class IceConnection : public LogContext {
  DECLARE_LOGGER();

 public:
  explicit IceConnection(const IceConfig& ice_config);
  virtual ~IceConnection();

  virtual void start() = 0;
  virtual bool setRemoteCandidates(const std::vector<CandidateInfo> &candidates, bool is_bundle) = 0;
  virtual void setRemoteCredentials(const std::string& username, const std::string& password) = 0;
  virtual int sendData(unsigned int component_id, const void* buf, int len) = 0;

  virtual void onData(unsigned int component_id, char* buf, int len) = 0;
  virtual CandidatePair getSelectedPair() = 0;
  virtual void setReceivedLastCandidate(bool hasReceived) = 0;
  virtual void close() = 0;

  virtual void updateIceState(IceState state);
  virtual IceState getIceState();
  virtual void setIceListener(std::weak_ptr<IceConnectionListener> listener);
  virtual std::weak_ptr<IceConnectionListener> getIceListener();

  virtual const std::string& getLocalUsername() const;
  virtual const std::string& getLocalPassword() const;

  static const char* iceStateToString(IceState state);

 protected:
  std::weak_ptr<IceConnectionListener> listener_;
  IceState ice_state_;
  IceConfig ice_config_;

  std::string ufrag_;
  std::string upass_;
  std::map <unsigned int, IceState> comp_state_list_;
};

}  // namespace erizo
#endif  // ERIZO_SRC_ERIZO_ICECONNECTION_H_
