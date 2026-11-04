#pragma once

// The 0xC5 0x1C capability advert, assembled (Offband #1994).
//
// Contract: docs/offband_info_frame_contract.md. The registry of tags is
// OFFBAND_INFO_TAG_* in examples/companion_radio/OffbandConfigProtocol.h; this header
// deliberately does not include it, because that file is companion-only and pulls Arduino,
// and the whole of this assembly must be unit-testable on the host. The caller passes tag
// values in; nothing here knows what a tag means.
//
// WHAT THIS REPLACES, so the shape is not re-litigated: Offband used to announce a
// capability by appending a byte to UPSTREAM's device-info frame and incrementing
// UPSTREAM's FIRMWARE_VER_CODE. Three features independently computed "the next free byte"
// as offset 87 and each was right in isolation, because that frame is built by
// out_frame[i++] with no declared layout. A position collision there is a plausible wrong
// number read with no error on either side. Tags cannot collide on position; they can only
// collide on a tag id, which is a visible clash in a merged header.
//
// A TAG'S PRESENCE IS THE CAPABILITY. There is no bitmap: the tags already answer "is this
// supported", and a bitmap beside them would be a second source of truth for one question.
// len 0 is the ordinary case. A value is carried only where it is one or two bytes, fixed
// for the build, and has no sub-code of its own to read it from.

#include <stdint.h>
#include <stddef.h>

namespace offband {

// The record overhead: one tag byte plus one length byte.
constexpr size_t kInfoRecordHeader = 2;

// A tag of 0 is not valid. Reserved so a zeroed buffer cannot read as a record.
constexpr uint8_t kInfoTagInvalid = 0;

// The smallest record area that can carry anything at all: one zero-length record, or the
// CONTINUE record that announces a truncation.
//
// THE CALLER MUST CHECK THIS. Below it the writer can neither send a record nor say that it
// could not, so an advert built in less space is an empty frame that a client would read as
// "this device has no capabilities" -- a wrong answer rather than a refusal. A caller whose
// transport offers less must reply with an error instead of an advert. 20 bytes is the
// worst case any real link gives us (the MIN_ATT_MTU floor), so this is a guard against a
// mistake, not a case that occurs in the field.
constexpr size_t kInfoMinCapacity = kInfoRecordHeader + 1;

// Assembles records into a caller-owned buffer, refusing any record it cannot write whole.
//
// SIZED BY THE CALLER, NEVER BY A CONSTANT. The ceiling comes from
// BaseSerialInterface::maxFramePayload(), which reports the MIN_ATT_MTU floor -- 20 bytes --
// until BLE negotiates and roughly 244 after. A fixed cap here would be wrong on both ends:
// too small to carry the advert on a negotiated link, too large to fit an unnegotiated one.
class InfoAdvert {
public:
  // `buf` holds the record area only -- the caller has already written [0xC5][0x1C] and
  // passes the space that remains.
  InfoAdvert(uint8_t* buf, size_t cap) : _buf(buf), _cap(buf == nullptr ? 0 : cap) {}

  // Appends one record. Returns false if it does not fit, and writes NOTHING in that case:
  // a half-written record would be read as a different record, which is the failure mode
  // this whole design exists to remove.
  //
  // ASCENDING TAG ORDER IS ENFORCED, not merely documented. Out-of-order append is a
  // programming error at the call site, and it matters because the resume cursor names a
  // tag: a client told "resume from 0x05" can only act on it if everything below 0x05 has
  // already been sent. An unordered advert would make the cursor silently lose records.
  bool add(uint8_t tag, const uint8_t* value, uint8_t len) {
    if (_buf == nullptr) return false;
    if (tag == kInfoTagInvalid) return false;
    if (tag <= _last_tag && _count > 0) return false;    // not ascending
    if (len > 0 && value == nullptr) return false;
    if (_truncated) return false;                        // nothing follows a stop

    const size_t need = kInfoRecordHeader + (size_t)len;
    // ROOM FOR A CONTINUE RECORD IS ALWAYS RESERVED, and never lent out.
    //
    // Without the reserve the advert can fill the buffer exactly, leaving finish() no space
    // to say it was cut -- and a truncated advert that cannot announce itself is read as a
    // complete one. That is the silent-wrong-answer failure this whole design exists to
    // remove, so it is not worth three bytes.
    //
    // An earlier draft lent the reserve to "the last record", which cannot work: add() does
    // not know whether a record is the last. Lending it and then refusing a later record
    // leaves exactly the un-announceable truncation described above.
    //
    // The cost is that an advert may stop one record early and send a CONTINUE for a record
    // that would have fitted. The client re-requests and gets it. Wasteful, correct, and
    // invisible in practice: today's advert is a few bytes against a negotiated ~244.
    const size_t reserve = kInfoRecordHeader + 1;
    if (_len + need + reserve > _cap) { _truncated = true; _next_tag = tag; return false; }

    _buf[_len++] = tag;
    _buf[_len++] = len;
    for (uint8_t i = 0; i < len; i++) _buf[_len++] = value[i];
    _last_tag = tag;
    _count++;
    return true;
  }

  // Convenience for the ordinary case: a capability with nothing to report.
  bool addFlag(uint8_t tag) { return add(tag, nullptr, 0); }

  // Convenience for a one-byte value.
  bool addByte(uint8_t tag, uint8_t v) { return add(tag, &v, 1); }

  // Writes the CONTINUE record if the advert stopped early. Call once, after the last add().
  // Returns the total record-area length to put on the wire.
  //
  // `continue_tag` is the tag the client should resume FROM -- the first one that did not
  // fit, not the last one that did.
  // A CURSOR IS ONLY MEANINGFUL IF PROGRESS WAS MADE. If nothing fit, a CONTINUE would
  // name a tag the device will refuse again at the same capacity, and the client -- doing
  // exactly what the contract tells it -- re-requests from that cursor forever.
  //
  // Raising a minimum capacity does not fix this, which is why the condition is `_count`
  // and not a size: records vary in length, so any fixed floor that admits a 2-byte flag
  // can still stall on a 3-byte record. The only honest signal is "I sent nothing", and
  // the caller must then return an error instead of an advert. See stalled().
  size_t finish(uint8_t continue_tag_value) {
    if (_truncated && _count > 0 && _len + kInfoRecordHeader + 1 <= _cap) {
      _buf[_len++] = kInfoTagContinue;
      _buf[_len++] = 1;
      _buf[_len++] = continue_tag_value;
    }
    return _len;
  }

  // Overload for the normal call: resume from whatever was refused.
  size_t finish() { return finish(_next_tag); }

  bool complete() const { return !_truncated; }

  // Nothing fit at all: the advert cannot make progress at this capacity, so there is no
  // cursor worth sending. THE CALLER MUST RETURN AN ERROR rather than an advert -- an
  // empty advert reads as "this device has no capabilities", and a cursor that cannot
  // advance loops the client forever. Both are confident wrong answers.
  bool stalled() const { return _truncated && _count == 0; }
  uint8_t nextTag() const { return _next_tag; }     // meaningful only when !complete()
  size_t length() const { return _len; }
  uint8_t count() const { return _count; }

  // Mirrors OFFBAND_INFO_TAG_CONTINUE. Mirrored rather than included for the same reason
  // the tag values are passed in: this header stays free of the companion-only protocol
  // header. MyMesh.cpp static_asserts the two against each other, so a drift breaks the
  // build rather than the behaviour.
  static constexpr uint8_t kInfoTagContinue = 0xFF;

private:
  uint8_t* _buf;
  size_t _cap;
  size_t _len = 0;
  uint8_t _last_tag = 0;
  uint8_t _next_tag = 0;
  uint8_t _count = 0;
  bool _truncated = false;
};

// Reads the record area back. Firmware does not need this; the tests do, and a parser
// written against the same header as the writer is what keeps the two honest about the
// format. A client implements the same walk in its own language.
//
// Returns false when the area is malformed -- a truncated record, or a zero tag.
inline bool infoAdvertWalk(const uint8_t* buf, size_t len,
                           void (*on_record)(uint8_t tag, const uint8_t* value, uint8_t len, void* ctx),
                           void* ctx) {
  if (buf == nullptr) return len == 0;
  size_t i = 0;
  while (i < len) {
    if (i + kInfoRecordHeader > len) return false;     // header does not fit
    const uint8_t tag = buf[i];
    const uint8_t vlen = buf[i + 1];
    if (tag == kInfoTagInvalid) return false;
    if (i + kInfoRecordHeader + (size_t)vlen > len) return false;   // value does not fit
    if (on_record != nullptr) on_record(tag, vlen > 0 ? &buf[i + 2] : nullptr, vlen, ctx);
    i += kInfoRecordHeader + (size_t)vlen;
  }
  return true;
}

}  // namespace offband
