/*
 * IceConnection.cpp
 */

#include <cstdio>
#include <string>
#include <cstring>
#include <vector>

#include "IceConnection.h"

namespace erizo {

DEFINE_LOGGER(IceConnection, "IceConnection")

IceConnection::IceConnection(const IceConfig& ice_config) : ice_state_{INITIAL}, ice_config_{ice_config} {
    for (unsigned int i = 1; i <= ice_config_.ice_components; i++) {
      comp_state_list_[i] = INITIAL;
    }
  }

IceConnection::~IceConnection() {
}

void IceConnection::setIceListener(std::weak_ptr<IceConnectionListener> listener) {
  listener_ = listener;
}

std::weak_ptr<IceConnectionListener> IceConnection::getIceListener() {
  return listener_;
}

const std::string& IceConnection::getLocalUsername() const {
  return ufrag_;
}

const std::string& IceConnection::getLocalPassword() const {
  return upass_;
}

IceState IceConnection::checkIceState() {
  return ice_state_;
}

const char* IceConnection::iceStateToString(IceState state) {
  switch (state) {
    case IceState::INITIAL:             return "initial";
    case IceState::FINISHED:            return "finished";
    case IceState::FAILED:              return "failed";
    case IceState::READY:               return "ready";
    case IceState::CANDIDATES_RECEIVED: return "cand_received";
  }
  return "unknown";
}

void IceConnection::updateIceState(IceState state) {
  if (state <= ice_state_) {
    if (state != IceState::READY)
      Warn("unexpected ice state transition, iceState: %s,  newIceState: %s",
                 iceStateToString(ice_state_), iceStateToString(state));
    return;
  }

  Info("iceState transition, ice_config_.transport_name: %s, iceState: %s, newIceState: %s, this: %p",
             ice_config_.transport_name.c_str(),
             iceStateToString(ice_state_), iceStateToString(state), this);
  this->ice_state_ = state;
  switch (ice_state_) {
    case IceState::FINISHED:
      return;
    case IceState::FAILED:
      Warn("Ice Failed");
      break;

    case IceState::READY:
    case IceState::CANDIDATES_RECEIVED:
      break;
    default:
      break;
  }

  // Important: send this outside our state lock.  Otherwise, serious risk of deadlock.
  if (auto listener = listener_.lock()) {
    listener->updateIceState(state, this);
  }
}

}  // namespace erizo
