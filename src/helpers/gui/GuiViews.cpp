#include "GuiViews.h"

// D1 (#1495): stubs so the failing-first tests build. D2-D5 implement them.

namespace offband {
namespace gui {

static void blank(char* out, size_t n) {
  if (n) out[0] = '\0';
}

Tone batteryTone(uint8_t) { return Tone::Dim; }
BleIcon bleIcon(bool, bool) { return BleIcon::Hidden; }
GpsIcon gpsIcon(bool, FixType, uint32_t) { return GpsIcon::Hidden; }
bool gpsBlinkLit(uint32_t) { return false; }

const char* signalWord(uint8_t) { return ""; }
void loraTitle(size_t, char* out, size_t n) { blank(out, n); }
void formatSnr(float, char* out, size_t n) { blank(out, n); }
void formatDbm(int16_t, char* out, size_t n) { blank(out, n); }
void formatMargin(float, uint8_t, char* out, size_t n) { blank(out, n); }

void formatFreq(float, char* out, size_t n) { blank(out, n); }
void radioSub(uint8_t, float, uint8_t, char* out, size_t n) { blank(out, n); }
void heardSub(uint16_t, uint16_t, uint16_t, char* out, size_t n) { blank(out, n); }
const char* gpsValue(bool, FixType) { return ""; }
void gpsSub(uint8_t, bool, char* out, size_t n) { blank(out, n); }
void formatUptime(uint32_t, char* out, size_t n) { blank(out, n); }
void batterySub(uint16_t, uint32_t, char* out, size_t n) { blank(out, n); }
void formatAirtime(uint16_t, char* out, size_t n) { blank(out, n); }
void unreadSub(const MsgStore&, char* out, size_t n) { blank(out, n); }

bool AdvertButton::press(uint32_t) { return false; }
void AdvertButton::result(bool, uint32_t) {}
AdvertButton::State AdvertButton::state(uint32_t) const { return State::Idle; }

void formatClock(int, uint32_t, char* out, size_t n) { blank(out, n); }
void formatDate(int, uint32_t, char* out, size_t n) { blank(out, n); }
size_t lockItems(const MsgStore&, uint32_t*, size_t, uint32_t* more) {
  if (more) *more = 0;
  return 0;
}

size_t chatOrder(const MsgStore&, uint16_t*, size_t) { return 0; }
void chatPreview(const MsgStore&, int, char* out, size_t n) { blank(out, n); }
void deliveryMeta(const MsgRecord&, bool, uint8_t, uint8_t, char* out, size_t n) { blank(out, n); }
void metaLine(const char*, const char*, const char*, char* out, size_t n) { blank(out, n); }

void ComposeBuffer::start(size_t) {}
bool ComposeBuffer::insert(const char*, size_t) { return false; }
bool ComposeBuffer::backspace() { return false; }
void ComposeBuffer::counter(char* out, size_t n) const { blank(out, n); }
Tone ComposeBuffer::counterTone() const { return Tone::Bad; }

void ShiftState::tap(uint32_t) {}
void ShiftState::typedLetter() {}
void ShiftState::autoCap(const char*, size_t) {}

void PageState::typed(char) {}

size_t lettersLayout(int16_t, int16_t, KeyRect*, size_t) { return 0; }
int nearestKey(const KeyRect*, size_t, int16_t, int16_t) { return -1; }

}  // namespace gui
}  // namespace offband
