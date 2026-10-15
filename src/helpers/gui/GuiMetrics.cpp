#include "GuiMetrics.h"

namespace offband {
namespace gui {

// B1 stubs: the tests in test_gui_data pin the behavior; B4 implements it.
void SnrMedian::add(float) {}
float SnrMedian::median() const { return -99.0f; }

void AirtimeWindow::advance(uint32_t) {}
void AirtimeWindow::addTx(uint32_t, uint32_t) {}
uint32_t AirtimeWindow::lastHourMs(uint32_t) { return 0xFFFFFFFF; }
uint16_t AirtimeWindow::dutyPermille(uint32_t) { return 0xFFFF; }

void HeardCounts::add(uint8_t) {}
uint32_t HeardCounts::count(uint8_t) const { return 0xFFFFFFFF; }
uint32_t HeardCounts::total() const { return 0xFFFFFFFF; }

bool ContactStats::begin(size_t) { return false; }
ContactStat* ContactStats::slot(const uint8_t*, bool) { return nullptr; }
void ContactStats::onHeard(const uint8_t*, float, int16_t) {}
void ContactStats::onDirectMessage(const uint8_t*) {}
void ContactStats::markRead(const uint8_t*) {}
void ContactStats::forget(const uint8_t*) {}
const ContactStat* ContactStats::find(const uint8_t*) const { return nullptr; }
uint32_t ContactStats::totalUnread() const { return 0xFFFFFFFF; }
ContactStats::~ContactStats() {}

}  // namespace gui
}  // namespace offband
