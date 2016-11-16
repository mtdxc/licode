/*
 * NiceConnection.cpp
 */
#include "logger.h"
#include <nice/nice.h>

#include "NiceConnection.h"
#include "SdpInfo.h"
#include "lib/Clock.h"

// If true (and configured properly below) erizo will generate relay candidates for itself
// MOSTLY USEFUL WHEN ERIZO ITSELF IS BEHIND A NAT
#define SERVER_SIDE_TURN 0

namespace erizo {

DEFINE_LOGGER(NiceConnection, "NiceConnection")


void cb_nice_recv(NiceAgent* agent, guint stream_id, guint component_id,
    guint len, gchar* buf, gpointer user_data) {
  if (user_data == NULL || len == 0) {
    return;
  }
  NiceConnection* nicecon = reinterpret_cast<NiceConnection*>(user_data);
  nicecon->onData(component_id, buf, len);
}

void cb_new_candidate(NiceAgent *agent, guint stream_id, guint component_id, gchar *foundation,
    gpointer user_data) {
  NiceConnection *conn = reinterpret_cast<NiceConnection*>(user_data);
  std::string found(foundation);
  conn->gotCandidate(stream_id, component_id, found);
}

void cb_candidate_gathering_done(NiceAgent *agent, guint stream_id, gpointer user_data) {
  NiceConnection *conn = reinterpret_cast<NiceConnection*>(user_data);
  conn->gatheringDone(stream_id);
}

void cb_component_state_changed(NiceAgent *agent, guint stream_id,
    guint component_id, guint state, gpointer user_data) {
  NiceConnection *conn = reinterpret_cast<NiceConnection*>(user_data);
  if (state == NICE_COMPONENT_STATE_CONNECTED) {
  } else if (state == NICE_COMPONENT_STATE_FAILED) {
    conn->updateComponentState(component_id, NICE_FAILED);
  }
}

void cb_new_selected_pair(NiceAgent *agent, guint stream_id, guint component_id,
    gchar *lfoundation, gchar *rfoundation, gpointer user_data) {
  NiceConnection *conn = reinterpret_cast<NiceConnection*>(user_data);
  conn->updateComponentState(component_id, NICE_READY);
}

NiceConnection::NiceConnection(std::shared_ptr<LibNiceInterface> libnice, MediaType med,
    const std::string &transport_name, const std::string& connection_id, NiceConnectionListener* listener,
    unsigned int iceComponents, const IceConfig& iceConfig) :
  mediaType(med), connection_id_(connection_id), lib_nice_(libnice), agent_(NULL), loop_(NULL), listener_(listener),
  candsDelivered_(0), iceState_(NICE_INITIAL), iceComponents_(iceComponents), 
  iceConfig_(iceConfig), receivedLastCandidate_(false) {
    set_log_context("%s.%d", connection_id.c_str(), med);
    transportName = transport_name;
    for (unsigned int i = 1; i <= iceComponents_; i++) {
      comp_state_list_[i] = NICE_INITIAL;
    }
    #if !GLIB_CHECK_VERSION(2, 35, 0)
    g_type_init();
    #endif
  }

NiceConnection::~NiceConnection() {
  Info("destroying");
  this->close();
  Info("destroyed");
}

void NiceConnection::close() {
  std::lock_guard<std::mutex> lock(closeMutex_);
  if (this->getIceState() == NICE_FINISHED) {
    return;
  }
  Info("closing");
  this->updateIceState(NICE_FINISHED);
  if (loop_ != NULL) {
    Info("main loop quit");
    g_main_loop_quit(loop_);
  }
  cond_.notify_one();
  listener_ = NULL;
  Info("m_thread join, this: %p", this);
#if 0
  boost::system_time const timeout = boost::get_system_time() + boost::posix_time::milliseconds(5);
  if (!m_Thread_.timed_join(timeout)) {
    Info("interrupt thread to close, this: %p", this);
    m_Thread_.interrupt();
  }
#else
	m_Thread_.join();
#endif
  if (loop_ != NULL) {
    g_main_loop_unref(loop_);
    loop_ = NULL;
  }
  if (agent_ != NULL) {
    g_object_unref(agent_);
    agent_ = NULL;
  }
  if (context_ != NULL) {
    g_main_context_unref(context_);
    context_ = NULL;
  }
  Info("closed, this: %p", this);
}

void NiceConnection::onData(unsigned int component_id, char* buf, int len) {
  if (this->getIceState() == NICE_READY) {
    packetPtr packet (new dataPacket());
    memcpy(packet->data, buf, len);
    packet->comp = component_id;
    packet->length = len;
    packet->received_time_ms = ClockUtils::msNow();
    listener_->onPacketReceived(packet);
  }
}

int NiceConnection::sendData(unsigned int compId, const void* buf, int len) {
  int val = -1;
  if (this->getIceState() == NICE_READY) {
    val = lib_nice_->NiceAgentSend(agent_, 1, compId, len, reinterpret_cast<const gchar*>(buf));
  }
  if (val != len) {
    Info("Sending less data than expected, sent: %d, to_send: %d", val, len);
  }
  return val;
}

void NiceConnection::start() {
    std::lock_guard<std::mutex> lock(closeMutex_);
    if (this->getIceState() != NICE_INITIAL) {
      return;
    }
    context_ = g_main_context_new();
    Info("creating Nice Agent");
    nice_debug_enable(FALSE);
    // Create a nice agent
    agent_ = lib_nice_->NiceAgentNew(context_);
    loop_ = g_main_loop_new(context_, FALSE);
    m_Thread_ = std::thread(&NiceConnection::mainLoop, this);
    // controlling-mode = false
    GValue controllingMode = { 0 };
    g_value_init(&controllingMode, G_TYPE_BOOLEAN);
    g_value_set_boolean(&controllingMode, false);
    g_object_set_property(G_OBJECT(agent_), "controlling-mode", &controllingMode);
    // max-connectivity-checks = 100
    GValue checks = { 0 };
    g_value_init(&checks, G_TYPE_UINT);
    g_value_set_uint(&checks, 100);
    g_object_set_property(G_OBJECT(agent_), "max-connectivity-checks", &checks);


    if (iceConfig_.stunServer.compare("") != 0 && iceConfig_.stunPort != 0) {
      GValue val = { 0 }, val2 = { 0 };
      g_value_init(&val, G_TYPE_STRING);
      g_value_set_string(&val, iceConfig_.stunServer.c_str());
      g_object_set_property(G_OBJECT(agent_), "stun-server", &val);

      g_value_init(&val2, G_TYPE_UINT);
      g_value_set_uint(&val2, iceConfig_.stunPort);
      g_object_set_property(G_OBJECT(agent_), "stun-server-port", &val2);

      Info("setting stun server %s:%d", iceConfig_.stunServer.c_str(), iceConfig_.stunPort);
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
    Info("adding stream, iceComponents: %d", iceComponents_);
    lib_nice_->NiceAgentAddStream(agent_, iceComponents_);
    gchar *ufrag = NULL, *upass = NULL;
    lib_nice_->NiceAgentGetLocalCredentials(agent_, 1, &ufrag, &upass);
    ufrag_ = std::string(ufrag); //g_free(ufrag);
    upass_ = std::string(upass); //g_free(upass);

    /* Set our remote credentials.  This must be done *after* we add a stream.
    if (username_.compare("") != 0 && password_.compare("") != 0) {
      Log("setting remote credentials in constructor, ufrag:%s, pass:%s",
                 username_.c_str(), password_.c_str());
      this->setRemoteCredentials(username_, password_);
    }*/
    // Set Port Range: If this doesn't work when linking the file libnice.sym has to be modified to include this call
    if (iceConfig_.minPort != 0 && iceConfig_.maxPort != 0) {
      Info("setting port range: %d->%d", iceConfig_.minPort, iceConfig_.maxPort);
      lib_nice_->NiceAgentSetPortRange(agent_, (guint)1, (guint)1, 
        (guint)iceConfig_.minPort, (guint)iceConfig_.maxPort);
    }
    // setting some local addr?
    if (!iceConfig_.network_interface.empty()) {
      const char* public_ip = lib_nice_->NiceInterfacesGetIpForInterface(iceConfig_.network_interface.c_str());
      if (public_ip) {
        lib_nice_->NiceAgentAddLocalAddress(agent_, public_ip);
      }
    }

    if (iceConfig_.turnServer.compare("") != 0 && iceConfig_.turnPort != 0) {
      Info("configuring turnServer %s:%d, %s@%s",
        iceConfig_.turnServer.c_str(), iceConfig_.turnPort, 
        iceConfig_.turnUsername.c_str(), iceConfig_.turnPass.c_str());

      for (unsigned int i = 1; i <= iceComponents_ ; i++) {
        lib_nice_->NiceAgentSetRelayInfo(agent_,
            1, // stream
            i, // comp
            iceConfig_.turnServer.c_str(),     // TURN Server IP
            iceConfig_.turnPort,               // TURN Server PORT
            iceConfig_.turnUsername.c_str(),   // Username
            iceConfig_.turnPass.c_str());       // Pass
      }
    }

    if (agent_) {
      for (unsigned int i = 1; i <= iceComponents_; i++) {
        lib_nice_->NiceAgentAttachRecv(agent_, 1, i, context_, reinterpret_cast<void*>(cb_nice_recv), this);
      }
    }
    Info("gathering, this: %p", this);
    lib_nice_->NiceAgentGatherCandidates(agent_, 1);
}

void NiceConnection::mainLoop() {
  // Start gathering candidates and fire event loop
  Info("starting g_main_loop, this: %p", this);
  if (agent_ == NULL || loop_ == NULL) {
    return;
  }
  g_main_loop_run(loop_);
  Info("finished g_main_loop, this: %p", this);
}

bool NiceConnection::setRemoteCandidates(const std::vector<CandidateInfo> &candidates, bool isBundle) {
  if (agent_ == NULL) {
    close();
    return false;
  }
  Info("setting remote candidates, candidateSize: %lu, mediaType: %d, bundle %d",
             candidates.size(), mediaType, isBundle);
  char host_str[128] = { 0 };
  GSList* candList = NULL;
  for (unsigned int it = 0; it < candidates.size(); it++) {
    NiceCandidateType nice_cand_type;
    CandidateInfo cinfo = candidates[it];
    // If bundle we will add the candidates regardless the mediaType
    if (cinfo.componentId != 1 || (!isBundle && cinfo.mediaType != this->mediaType ))
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
    thecandidate->username = g_strdup(cinfo.username.c_str());
    thecandidate->password = g_strdup(cinfo.password.c_str());
    thecandidate->stream_id = (guint) 1;
    thecandidate->component_id = cinfo.componentId;
    thecandidate->priority = cinfo.priority;
    thecandidate->transport = NICE_CANDIDATE_TRANSPORT_UDP;
    nice_address_set_from_string(&thecandidate->addr, cinfo.hostAddress.c_str());
    nice_address_set_port(&thecandidate->addr, cinfo.hostPort);

    sprintf(host_str, "host %d %s:%d", cinfo.hostType, cinfo.hostAddress.c_str(), cinfo.hostPort);

    if (cinfo.hostType == RELAY || cinfo.hostType == SRFLX) {
      nice_address_set_from_string(&thecandidate->base_addr, cinfo.rAddress.c_str());
      nice_address_set_port(&thecandidate->base_addr, cinfo.rPort);
      Info("adding relay or srflx remote candidate, %s, remote %s:%d", host_str,
                 cinfo.rAddress.c_str(), cinfo.rPort);
    } else {
      Info("adding remote candidate, %s, priority: %d, componentId: %d, ufrag: %s, pass: %s",
          host_str, cinfo.priority, cinfo.componentId, 
          cinfo.username.c_str(), cinfo.password.c_str());
    }
    candList = g_slist_prepend(candList, thecandidate);
  }
  // TODO(pedro): Set Component Id properly, now fixed at 1
  lib_nice_->NiceAgentSetRemoteCandidates(agent_, (guint) 1, 1, candList);
  g_slist_free_full(candList, (GDestroyNotify)&nice_candidate_free);

  return true;
}

void NiceConnection::gatheringDone(uint stream_id) {
  Info("gathering done, stream_id: %u", stream_id);
  this->updateIceState(NICE_CANDIDATES_RECEIVED);
}

void NiceConnection::gotCandidate(uint stream_id, uint component_id, const std::string &foundation) {
  GSList* lcands = lib_nice_->NiceAgentGetLocalCandidates(agent_, stream_id, component_id);
  // We only want to get the new candidates(skip exist)
  if (candsDelivered_ <= g_slist_length(lcands)) {
    lcands = g_slist_nth(lcands, (candsDelivered_));
  }

  char address[NICE_ADDRESS_STRING_LEN], baseAddress[NICE_ADDRESS_STRING_LEN];
  for (GSList* iterator = lcands; iterator; iterator = iterator->next) {
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
    cand_info.mediaType = mediaType;

    /*
     *   NICE_CANDIDATE_TYPE_HOST,
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
        char turnAddres[NICE_ADDRESS_STRING_LEN];
        nice_address_to_string(&cand->turn->server, turnAddres);
        cand_info.hostType = RELAY;
        cand_info.rAddress = std::string(baseAddress);
        cand_info.rPort = nice_address_get_port(&cand->base_addr);
        break;
      default:
        break;
    }
    cand_info.netProtocol = "udp";
    cand_info.transProtocol = transportName;
    cand_info.username = ufrag_;
    cand_info.password = upass_;
    if (this->getNiceListener() != NULL)
      this->getNiceListener()->onCandidate(cand_info, this);
  }
  // for nice_agent_get_local_candidates, the caller owns the returned GSList as well as the candidates
  // contained within it.
  // let's free everything in the list, as well as the list.
  //g_slist_free_full(lcands, (GDestroyNotify)&nice_candidate_free);
}

void NiceConnection::setRemoteCredentials(const std::string& username, const std::string& password) {
  Info("setting remote credentials, ufrag: %s, pass: %s", username.c_str(), password.c_str());
  lib_nice_->NiceAgentSetRemoteCredentials(agent_, (guint) 1, username.c_str(), password.c_str());
}

void NiceConnection::setNiceListener(NiceConnectionListener *listener) {
  this->listener_ = listener;
}

NiceConnectionListener* NiceConnection::getNiceListener() {
  return this->listener_;
}

void NiceConnection::updateComponentState(unsigned int compId, IceState state) {
  Info("update ice ComponentState, newState: %u, transportName: %s, componentId %u, iceComponents: %u",
             state, transportName.c_str(), compId, iceComponents_);
  comp_state_list_[compId] = state;
  if (state == NICE_READY) {
    for (unsigned int i = 1; i <= iceComponents_; i++) {
      if (comp_state_list_[i] != NICE_READY) {
        return;
      }
    }
  } else if (state == NICE_FAILED) {
    if (receivedLastCandidate_) {
      ELOG_WARN("component failed, transportName: %s, componentId: %u",
                transportName.c_str(), compId);
      for (unsigned int i = 1; i <= iceComponents_; i++) {
        if (comp_state_list_[i] != NICE_FAILED) {
          return;
        }
      }
    } else {
      ELOG_WARN("failed and not received all candidates, newComponentState:%u", state);
      return;
    }
  }
  // pass to this state
  this->updateIceState(state);
}

IceState NiceConnection::getIceState() {
  return iceState_;
}

std::string NiceConnection::iceStateToString(IceState state) {
  switch (state) {
    case NICE_INITIAL:             return "initial";
    case NICE_FINISHED:            return "finished";
    case NICE_FAILED:              return "failed";
    case NICE_READY:               return "ready";
    case NICE_CANDIDATES_RECEIVED: return "cand_received";
  }
  return "unknown";
}

void NiceConnection::updateIceState(IceState state) {
  if (state <= iceState_) {
    if (state != NICE_READY) {
      Warn("unexpected ice state transition %s -> %s",
        iceStateToString(iceState_).c_str(), iceStateToString(state).c_str());
    }
    return;
  }

  Info("iceState transition, transportName: %s, iceState: %s->%s, this: %p",
             transportName.c_str(),
             iceStateToString(this->iceState_).c_str(), iceStateToString(state).c_str(), this);
  this->iceState_ = state;
  switch (iceState_) {
    case NICE_FINISHED:
      return;
    case NICE_FAILED:
      Info("Ice Failed");
      break;

    case NICE_READY:
    case NICE_CANDIDATES_RECEIVED:
      break;
    default:
      break;
  }

  // Important: send this outside our state lock.  Otherwise, serious risk of deadlock.
  if (this->listener_ != NULL)
    this->listener_->updateIceState(state, this);
}

std::string CandidateTypeStr(NiceCandidate *candidate) {
  switch (candidate->type) {
    case NICE_CANDIDATE_TYPE_HOST: return "host";
    case NICE_CANDIDATE_TYPE_SERVER_REFLEXIVE: return "serverReflexive";
    case NICE_CANDIDATE_TYPE_PEER_REFLEXIVE: return "peerReflexive";
    case NICE_CANDIDATE_TYPE_RELAYED: return "relayed";
    default: return "unknown";
  }
}

CandidatePair NiceConnection::getSelectedPair() {
  CandidatePair selectedPair;
  NiceCandidate* local, *remote;
  if (lib_nice_ && lib_nice_->NiceAgentGetSelectedPair(agent_, 1, 1, &local, &remote)) {
    char ipaddr[NICE_ADDRESS_STRING_LEN] = {0};
    nice_address_to_string(&local->addr, ipaddr);
    selectedPair.erizoCandidateIp = std::string(ipaddr);
    selectedPair.erizoCandidatePort = nice_address_get_port(&local->addr);
    selectedPair.erizoHostType = CandidateTypeStr(local);
    nice_address_to_string(&remote->addr, ipaddr);
    selectedPair.clientCandidateIp = std::string(ipaddr);
    selectedPair.clientCandidatePort = nice_address_get_port(&remote->addr);
    selectedPair.clientHostType = CandidateTypeStr(local);
    Info("selected pair, local_addr:%s %s:%d -> remote_addr:%s %s:%d",
      selectedPair.erizoHostType.c_str(), selectedPair.erizoCandidateIp.c_str(), selectedPair.erizoCandidatePort,
      selectedPair.clientHostType.c_str(), selectedPair.clientCandidateIp.c_str(), selectedPair.clientCandidatePort);
  }
  return selectedPair;
}

void NiceConnection::setReceivedLastCandidate(bool hasReceived) {
  Info("setReceivedLastCandidate %d", hasReceived);
  this->receivedLastCandidate_ = hasReceived;
}
} /* namespace erizo */
