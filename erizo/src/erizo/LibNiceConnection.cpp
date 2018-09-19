/*
 * LibNiceConnection.cpp
 */

#include <nice/nice.h>
#include <cstdio>
#include <string>
#include <cstring>
#include <vector>
#include <sstream>
#include "LibNiceConnection.h"
#include "SdpInfo.h"
#include "lib/Clock.h"

using std::memcpy;

// If true (and configured properly below) erizo will generate relay candidates for itself
// MOSTLY USEFUL WHEN ERIZO ITSELF IS BEHIND A NAT
#define SERVER_SIDE_TURN 0

namespace erizo {

DEFINE_LOGGER(LibNiceConnection, "LibNiceConnection")

void cb_nice_recv(NiceAgent* agent, guint stream_id, guint component_id,
    guint len, gchar* buf, gpointer user_data) {
  if (user_data == NULL || len == 0) {
    return;
  }
  LibNiceConnection* nicecon = reinterpret_cast<LibNiceConnection*>(user_data);
  nicecon->onData(component_id, reinterpret_cast<char*> (buf), static_cast<unsigned int> (len));
}

void cb_new_candidate(NiceAgent *agent, guint stream_id, guint component_id, gchar *foundation,
    gpointer user_data) {
  LibNiceConnection *conn = reinterpret_cast<LibNiceConnection*>(user_data);
  std::string found(foundation);
  conn->getCandidate(stream_id, component_id, found);
}

void cb_candidate_gathering_done(NiceAgent *agent, guint stream_id, gpointer user_data) {
  LibNiceConnection *conn = reinterpret_cast<LibNiceConnection*>(user_data);
  conn->gatheringDone(stream_id);
}

void cb_component_state_changed(NiceAgent *agent, guint stream_id,
    guint component_id, guint state, gpointer user_data) {
  if (state == NICE_COMPONENT_STATE_CONNECTED) {
  } else if (state == NICE_COMPONENT_STATE_FAILED) {
    LibNiceConnection *conn = reinterpret_cast<LibNiceConnection*>(user_data);
    conn->updateComponentState(component_id, IceState::FAILED);
  }
}

void cb_new_selected_pair(NiceAgent *agent, guint stream_id, guint component_id,
    gchar *lfoundation, gchar *rfoundation, gpointer user_data) {
  LibNiceConnection *conn = reinterpret_cast<LibNiceConnection*>(user_data);
  conn->updateComponentState(component_id, IceState::READY);
}

LibNiceConnection::LibNiceConnection(const IceConfig& ice_config)
  : IceConnection{ice_config}, 
	agent_{NULL}, loop_{NULL}, candsDelivered_{0}, receivedLastCandidate_{false} {
  #if !GLIB_CHECK_VERSION(2, 35, 0)
  g_type_init();
  #endif
}

LibNiceConnection::~LibNiceConnection() {
  this->close();
}

void LibNiceConnection::close() {
  AutoLock lock(close_mutex_);
  if (this->checkIceState() == IceState::FINISHED) {
    return;
  }
  Debug("closing");
  this->updateIceState(IceState::FINISHED);
  if (loop_ != NULL) {
    Debug("main loop quit");
    g_main_loop_quit(loop_);
  }
  cond_.notify_one();
  listener_.reset();
#ifdef WIN32
  if (thread_.joinable()) {
	  thread_.join();
  }
#else
  boost::system_time const timeout = boost::get_system_time() + boost::posix_time::milliseconds(5);
  Debug("m_thread join, this: %p", this);
  if (!thread_.timed_join(timeout)) {
	  Debug("interrupt thread to close, this: %p", this);
	  thread_.interrupt();
  }
#endif // WIN32

  if (loop_ != NULL) {
    Debug("Unrefing loop");
    g_main_loop_unref(loop_);
    loop_ = NULL;
  }
  if (agent_ != NULL) {
    Debug("unrefing agent");
    g_object_unref(agent_);
    agent_ = NULL;
  }
  if (context_ != NULL) {
    Debug("Unrefing context");
    g_main_context_unref(context_);
    context_ = NULL;
  }
  Debug("closed, this: %p", this);
}

void LibNiceConnection::onData(unsigned int component_id, char* buf, int len) {
  IceState state;
  {
    AutoLock lock(close_mutex_);
    state = this->checkIceState();
  }
  if (state == IceState::READY) {
    packetPtr packet (new DataPacket());
    memcpy(packet->data, buf, len);
    packet->comp = component_id;
    packet->length = len;
    packet->received_time_ms = ClockUtils::timePointToMs(clock::now());
    if (auto listener = getIceListener().lock()) {
      listener->onPacketReceived(packet);
    }
  }
}

int LibNiceConnection::sendData(unsigned int component_id, const void* buf, int len) {
  int val = -1;
  if (this->checkIceState() == IceState::READY) {
    val = nice_agent_send(agent_, 1, component_id, len, reinterpret_cast<const gchar*>(buf));
  }
  if (val != len) {
    Debug("Sending less data than expected, sent: %d, to_send: %d", val, len);
  }
  return val;
}

void LibNiceConnection::start() {
	AutoLock lock(close_mutex_);
    if (this->checkIceState() != INITIAL) {
      return;
    }
    context_ = g_main_context_new();
    Debug("creating Nice Agent");
    nice_debug_enable(FALSE);
    // Create a nice agent
    agent_ = nice_agent_new(context_, NICE_COMPATIBILITY_RFC5245);
    loop_ = g_main_loop_new(context_, FALSE);
    thread_ = std::thread(&LibNiceConnection::mainLoop, this);
    GValue controllingMode = { 0 };
    g_value_init(&controllingMode, G_TYPE_BOOLEAN);
    g_value_set_boolean(&controllingMode, false);
    g_object_set_property(G_OBJECT(agent_), "controlling-mode", &controllingMode);

    GValue checks = { 0 };
    g_value_init(&checks, G_TYPE_UINT);
    g_value_set_uint(&checks, 100);
    g_object_set_property(G_OBJECT(agent_), "max-connectivity-checks", &checks);


    if (ice_config_.stun_server.length() != 0 && ice_config_.stun_port != 0) {
      GValue val = { 0 }, val2 = { 0 };
      g_value_init(&val, G_TYPE_STRING);
      g_value_set_string(&val, ice_config_.stun_server.c_str());
      g_object_set_property(G_OBJECT(agent_), "stun-server", &val);

      g_value_init(&val2, G_TYPE_UINT);
      g_value_set_uint(&val2, ice_config_.stun_port);
      g_object_set_property(G_OBJECT(agent_), "stun-server-port", &val2);

      Debug("setting stun, stun_server: %s, stun_port: %d",
                 ice_config_.stun_server.c_str(), ice_config_.stun_port);
    }

    // Connect the signals
    g_signal_connect(G_OBJECT(agent_), "candidate-gathering-done",
        G_CALLBACK(cb_candidate_gathering_done), this);
    g_signal_connect(G_OBJECT(agent_), "component-state-changed",
        G_CALLBACK(cb_component_state_changed), this);
    g_signal_connect(G_OBJECT(agent_), "new-selected-pair",
        G_CALLBACK(cb_new_selected_pair), this);
    g_signal_connect(G_OBJECT(agent_), "new-candidate",
        G_CALLBACK(cb_new_candidate), this);

    // Create a new stream and start gathering candidates
    Debug("adding stream, iceComponents: %d", ice_config_.ice_components);
    nice_agent_add_stream(agent_, ice_config_.ice_components);
    gchar *ufrag = NULL, *upass = NULL;
    nice_agent_get_local_credentials(agent_, 1, &ufrag, &upass);
    ufrag_ = std::string(ufrag); g_free(ufrag);
    upass_ = std::string(upass); g_free(upass);

    // Set Port Range: If this doesn't work when linking the file libnice.sym has to be modified to include this call
    if (ice_config_.min_port != 0 && ice_config_.max_port != 0) {
      Debug("setting port range, min_port: %d, max_port: %d",
                 ice_config_.min_port, ice_config_.max_port);
      nice_agent_set_port_range(agent_, (guint)1, (guint)1, (guint)ice_config_.min_port,
          (guint)ice_config_.max_port);
    }

    if (!ice_config_.network_interface.empty()) {
      const char* public_ip = nice_interfaces_get_ip_for_interface((gchar*)ice_config_.network_interface.c_str());
      if (public_ip) {
				NiceAddress addr;
				nice_address_init(&addr);
				nice_address_set_from_string(&addr, public_ip);
				nice_agent_add_local_address(agent_, &addr);
      }
    }

    if (ice_config_.turn_server.length() != 0 && ice_config_.turn_port != 0) {
      Debug("configuring TURN, turn_server: %s , turn_port: %d, turn_username: %s, turn_pass: %s",
                 ice_config_.turn_server.c_str(),
          ice_config_.turn_port, ice_config_.turn_username.c_str(), ice_config_.turn_pass.c_str());

      for (unsigned int i = 1; i <= ice_config_.ice_components ; i++) {
				nice_agent_set_relay_info(agent_,
            1,
            i,
            ice_config_.turn_server.c_str(),     // TURN Server IP
            ice_config_.turn_port,               // TURN Server PORT
            ice_config_.turn_username.c_str(),   // Username
            ice_config_.turn_pass.c_str(),       // Pass
					  NICE_RELAY_TYPE_TURN_UDP); 
      }
    }

    if (agent_) {
      for (unsigned int i = 1; i <= ice_config_.ice_components; i++) {
        nice_agent_attach_recv(agent_, 1, i, context_, reinterpret_cast<NiceAgentRecvFunc>(cb_nice_recv), this);
      }
    }
    Debug("gathering, this: %p", this);
    nice_agent_gather_candidates(agent_, 1);
}

void LibNiceConnection::mainLoop() {
  // Start gathering candidates and fire event loop
  Debug("starting g_main_loop, this: %p", this);
  if (agent_ == NULL || loop_ == NULL) {
    return;
  }
  g_main_loop_run(loop_);
  Debug("finished g_main_loop, this: %p", this);
}

bool LibNiceConnection::setRemoteCandidates(const std::vector<CandidateInfo> &candidates, bool is_bundle) {
  if (agent_ == NULL) {
    this->close();
    return false;
  }
  GSList* candList = NULL;
  Debug("setting remote candidates, candidateSize: %lu, mediaType: %d", candidates.size(), ice_config_.media_type);

  for (unsigned int it = 0; it < candidates.size(); it++) {
    NiceCandidateType nice_cand_type;
    CandidateInfo cinfo = candidates[it];
    // If bundle we will add the candidates regardless the mediaType
    if (cinfo.componentId != 1 || (!is_bundle && cinfo.mediaType != ice_config_.media_type ))
      continue;

    switch (cinfo.hostType) {
      case HOST:
        nice_cand_type = NICE_CANDIDATE_TYPE_HOST;
        break;
      case SRFLX:
        nice_cand_type = NICE_CANDIDATE_TYPE_SERVER_REFLEXIVE;
        break;
      case PRFLX:
        nice_cand_type = NICE_CANDIDATE_TYPE_PEER_REFLEXIVE;
        break;
      case RELAY:
        nice_cand_type = NICE_CANDIDATE_TYPE_RELAYED;
        break;
      default:
        nice_cand_type = NICE_CANDIDATE_TYPE_HOST;
        break;
    }
    if (cinfo.hostPort == 0) {
      continue;
    }
    NiceCandidate* thecandidate = nice_candidate_new(nice_cand_type);
    thecandidate->username = strdup(cinfo.username.c_str());
    thecandidate->password = strdup(cinfo.password.c_str());
    thecandidate->stream_id = (guint) 1;
    thecandidate->component_id = cinfo.componentId;
    thecandidate->priority = cinfo.priority;
    thecandidate->transport = NICE_CANDIDATE_TRANSPORT_UDP;
    nice_address_set_from_string(&thecandidate->addr, cinfo.hostAddress.c_str());
    nice_address_set_port(&thecandidate->addr, cinfo.hostPort);

    std::ostringstream host_info;
    host_info << "hostType: " << cinfo.hostType
         << ", hostAddress: " << cinfo.hostAddress
         << ", hostPort: " << cinfo.hostPort;

    if (cinfo.hostType == RELAY || cinfo.hostType == SRFLX) {
      nice_address_set_from_string(&thecandidate->base_addr, cinfo.rAddress.c_str());
      nice_address_set_port(&thecandidate->base_addr, cinfo.rPort);
      Debug("adding relay or srflx remote candidate, %s, rAddress: %s, rPort: %d",
                 host_info.str().c_str(), cinfo.rAddress.c_str(), cinfo.rPort);
    } else {
      Debug("adding remote candidate, %s, priority: %d, componentId: %d, ufrag: %s, pass: %s",
          host_info.str().c_str(), cinfo.priority, cinfo.componentId, 
          cinfo.username.c_str(), cinfo.password.c_str());
    }
    candList = g_slist_prepend(candList, thecandidate);
  }
  // TODO(pedro): Set Component Id properly, now fixed at 1
  nice_agent_set_remote_candidates(agent_, (guint) 1, 1, candList);
  g_slist_free_full(candList, (GDestroyNotify)&nice_candidate_free);

  return true;
}

void LibNiceConnection::gatheringDone(uint32_t stream_id) {
  Debug("gathering done, stream_id: %u", stream_id);
  updateIceState(IceState::CANDIDATES_RECEIVED);
}

void LibNiceConnection::getCandidate(uint32_t stream_id, uint32_t component_id, const std::string &foundation) {
  GSList* lcands = nice_agent_get_local_candidates(agent_, stream_id, component_id);
  // We only want to get the new candidates
  if (candsDelivered_ <= g_slist_length(lcands)) {
    lcands = g_slist_nth(lcands, (candsDelivered_));
  }
  for (GSList* iterator = lcands; iterator; iterator = iterator->next) {
    char address[NICE_ADDRESS_STRING_LEN], baseAddress[NICE_ADDRESS_STRING_LEN];
    NiceCandidate *cand = reinterpret_cast<NiceCandidate*>(iterator->data);
    nice_address_to_string(&cand->addr, address);
    nice_address_to_string(&cand->base_addr, baseAddress);
    candsDelivered_++;
    if (strstr(address, ":") != NULL) {  // We ignore IPv6 candidates at this point
      continue;
    }
    CandidateInfo cand_info;
    cand_info.componentId = cand->component_id;
    cand_info.foundation = cand->foundation;
    cand_info.priority = cand->priority;
    cand_info.hostAddress = std::string(address);
    cand_info.hostPort = nice_address_get_port(&cand->addr);
    if (cand_info.hostPort == 0) {
      continue;
    }
    cand_info.mediaType = ice_config_.media_type;

    /*
     *    NICE_CANDIDATE_TYPE_HOST,
     *    NICE_CANDIDATE_TYPE_SERVER_REFLEXIVE,
     *    NICE_CANDIDATE_TYPE_PEER_REFLEXIVE,
     *    NICE_CANDIDATE_TYPE_RELAYED,
     */
    switch (cand->type) {
      case NICE_CANDIDATE_TYPE_HOST:
        cand_info.hostType = HOST;
        break;
      case NICE_CANDIDATE_TYPE_SERVER_REFLEXIVE:
        cand_info.hostType = SRFLX;
        cand_info.rAddress = std::string(baseAddress);
        cand_info.rPort = nice_address_get_port(&cand->base_addr);
        break;
      case NICE_CANDIDATE_TYPE_PEER_REFLEXIVE:
        cand_info.hostType = PRFLX;
        break;
      case NICE_CANDIDATE_TYPE_RELAYED:
        //char turnAddres[NICE_ADDRESS_STRING_LEN];
        //nice_address_to_string(&cand->turn->server, turnAddres);
        cand_info.hostType = RELAY;
        cand_info.rAddress = std::string(baseAddress);
        cand_info.rPort = nice_address_get_port(&cand->base_addr);
        break;
      default:
        break;
    }
    cand_info.netProtocol = "udp";
    cand_info.transProtocol = ice_config_.transport_name;
    cand_info.username = ufrag_;
    cand_info.password = upass_;
    // localCandidates->push_back(cand_info);
    if (auto listener = this->getIceListener().lock()) {
      listener->onCandidate(cand_info, this);
    }
  }
  // for nice_agent_get_local_candidates, the caller owns the returned GSList as well as the candidates
  // contained within it.
  // let's free everything in the list, as well as the list.
  g_slist_free_full(lcands, (GDestroyNotify)&nice_candidate_free);
}

void LibNiceConnection::setRemoteCredentials(const std::string& username, const std::string& password) {
  Debug("setting remote credentials, ufrag: %s, pass: %s", username.c_str(), password.c_str());
  if(agent_)
  	nice_agent_set_remote_credentials(agent_, (guint) 1, username.c_str(), password.c_str());
}

void LibNiceConnection::updateComponentState(unsigned int component_id, IceState state) {
  Debug("new ice component state, newState: %u, transportName: %s, componentId %u, iceComponents: %u",
             state, ice_config_.transport_name.c_str(), component_id, ice_config_.ice_components);
  comp_state_list_[component_id] = state;
  if (state == IceState::READY) {
    for (unsigned int i = 1; i <= ice_config_.ice_components; i++) {
      if (comp_state_list_[i] != IceState::READY) {
        return;
      }
    }
  } else if (state == IceState::FAILED) {
    if (receivedLastCandidate_) {
      Warn("component failed, ice_config_.transport_name: %s, componentId: %u",
                ice_config_.transport_name.c_str(), component_id);
      for (unsigned int i = 1; i <= ice_config_.ice_components; i++) {
        if (comp_state_list_[i] != IceState::FAILED) {
          return;
        }
      }
    } else {
      Warn("failed and not received all candidates, newComponentState:%u", state);
      return;
    }
  }
  this->updateIceState(state);
}

std::string getHostTypeFromCandidate(NiceCandidate *candidate) {
  switch (candidate->type) {
    case NICE_CANDIDATE_TYPE_HOST: return "host";
    case NICE_CANDIDATE_TYPE_SERVER_REFLEXIVE: return "serverReflexive";
    case NICE_CANDIDATE_TYPE_PEER_REFLEXIVE: return "peerReflexive";
    case NICE_CANDIDATE_TYPE_RELAYED: return "relayed";
    default: return "unknown";
  }
}

CandidatePair LibNiceConnection::getSelectedPair() {
  char ipaddr[NICE_ADDRESS_STRING_LEN];
  CandidatePair selectedPair;
  NiceCandidate* local, *remote;
  nice_agent_get_selected_pair(agent_, 1, 1, &local, &remote);
  nice_address_to_string(&local->addr, ipaddr);
  selectedPair.erizoCandidateIp = std::string(ipaddr);
  selectedPair.erizoCandidatePort = nice_address_get_port(&local->addr);
  selectedPair.erizoHostType = getHostTypeFromCandidate(local);
  Debug("selected pair, local_addr: %s, local_port: %d, local_type: %s",
              ipaddr, nice_address_get_port(&local->addr), selectedPair.erizoHostType.c_str());
  nice_address_to_string(&remote->addr, ipaddr);
  selectedPair.clientCandidateIp = std::string(ipaddr);
  selectedPair.clientCandidatePort = nice_address_get_port(&remote->addr);
  selectedPair.clientHostType = getHostTypeFromCandidate(local);
  Info("selected pair, remote_addr: %s, remote_port: %d, remote_type: %s",
             ipaddr, nice_address_get_port(&remote->addr), selectedPair.clientHostType.c_str());
  return selectedPair;
}

void LibNiceConnection::setReceivedLastCandidate(bool hasReceived) {
  Debug("setting hasReceivedLastCandidate, hasReceived: %u", hasReceived);
  this->receivedLastCandidate_ = hasReceived;
}

LibNiceConnection* LibNiceConnection::create(const IceConfig& ice_config) {
  return new LibNiceConnection(ice_config);
}
} /* namespace erizo */
