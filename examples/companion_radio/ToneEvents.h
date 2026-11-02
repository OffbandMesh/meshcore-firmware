#pragma once

// #1419: the UI's event enum, in the tune table's terms. `none` and anything unmapped has
// no tone event, so it stays silent rather than falling onto some other event's tune.
// #1957: shared, because every UI that plays through the tune engine needs the same map.

#ifdef PIN_BUZZER

#include <helpers/ui/Tunes.h>
#include "AbstractUITask.h"

namespace offband {

inline bool toneEventFor(UIEventType t, ToneEvent& out) {
  switch (t) {
    case UIEventType::contactMessage:    out = ToneEvent::ContactMessage; return true;
    case UIEventType::channelMessage:    out = ToneEvent::ChannelMessage; return true;
    case UIEventType::roomMessage:       out = ToneEvent::RoomMessage;    return true;
    case UIEventType::newContactMessage: out = ToneEvent::NewContact;     return true;
    case UIEventType::ack:               out = ToneEvent::Ack;            return true;
    case UIEventType::sent:              out = ToneEvent::Sent;           return true;
    case UIEventType::none:
    default:
      return false;
  }
}

}  // namespace offband

#endif  // PIN_BUZZER
