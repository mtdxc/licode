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

IceConnection::IceConnection(IceConnectionListener* listener, const IceConfig& ice_config) :
  listener_{listener}, ice_state_{INITIAL}, ice_config_{ice_config} {
    for (unsigned int i = 1; i <= ice_config_.ice_components; i++) {
      comp_state_list_[i] = INITIAL;
    }
  }

IceConnection::~IceConnection() {
  this->listener_ = nullptr;
}

void IceConnection::setIceListener(IceConnectionListener *listener) {
  listener_ = listener;
}

IceConnectionListener* IceConnection::getIceListener() {
  return listener_;
}

std::string IceConnection::getLocalUsername() {
  return ufrag_;
}

std::string IceConnection::getLocalPassword() {
  return upass_;
}


IceState IceConnection::checkIceState() {
  return ice_state_;
}

std::string IceConnection::iceStateToString(IceState state) const {
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
      Info("unexpected ice state transition, iceState: %s,  newIceState: %s",
                iceStateToString(ice_state_).c_str(), iceStateToString(state).c_str());
    return;
  }

  Info("iceState transition, ice_config_.transport_name: %s, iceState: %s, newIceState: %s, this: %p",
             ice_config_.transport_name.c_str(),
             iceStateToString(ice_state_).c_str(), iceStateToString(state).c_str(), this);
  this->ice_state_ = state;
  switch (ice_state_) {
    case IceState::FINISHED:
      return;
    case IceState::FAILED:
      Info("Ice Failed");
      break;

    case IceState::READY:
    case IceState::CANDIDATES_RECEIVED:
      break;
    default:
      break;
  }

  // Important: send this outside our state lock.  Otherwise, serious risk of deadlock.
  if (this->listener_ != NULL)
    this->listener_->updateIceState(state, this);
}

}  // namespace erizo
