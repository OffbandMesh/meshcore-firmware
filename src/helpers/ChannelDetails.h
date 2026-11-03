#pragma once

#include <Arduino.h>
#include <Mesh.h>

struct ChannelDetails {
  mesh::GroupChannel channel;
  char name[32];

  // #1523: the tune this channel's messages play, AS STORED -- not a tune id.
  //
  // The encoding is id + 1, so 0 means "no choice made" and falls back to the global
  // channel tune. Pass it to offband::resolveChannelTune(); never use it as an id, and
  // never assign an id to it without offband::encodeChannelTune(). The rules and the
  // reason for the +1 live in helpers/ui/TuneSlots.h, which this header deliberately does
  // not include -- BaseChatMesh.h pulls ChannelDetails into every role on every variant,
  // and one byte is the whole of the cost that should carry.
  //
  // Initialized here rather than at each construction site on purpose: three places build
  // a ChannelDetails and hand it to setChannel(), which copies the struct whole. A site
  // that forgets this field then stores a stack byte -- a random tune, or an unresolvable
  // one. With the initializer, forgetting means "unset", which is the pre-feature
  // behaviour. (A fresh channel set from the app still has to carry the old tune over
  // explicitly; see MyMesh::cmdSetChannel.)
  uint8_t tune = 0;

  // #606: the per-channel notification mode, AS STORED -- 1-based, so 0 means "no choice
  // made" and falls back to the device-wide notify_scope. Pass it to
  // offband::resolveChannelNotify(); never read it as a mode directly.
  //
  // One pref per channel: the client (0xC5 CHNOTIFY) and the device's own UI both write
  // this byte, and there is no second store. The rules live in helpers/ChannelNotify.h,
  // which this header deliberately does not include -- BaseChatMesh.h pulls
  // ChannelDetails into every role on every variant, and two bytes is the whole of the
  // cost that should carry.
  //
  // Initialized here for the same reason the tune is: three places build a
  // ChannelDetails and hand it to setChannel(), which copies the struct whole, so a site
  // that forgets this field would store a stack byte -- an arbitrary mode, deciding
  // whether a radio sounds. With the initializer, forgetting means "unset", which is the
  // pre-feature behaviour.
  uint8_t notify = 0;
};
