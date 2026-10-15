#include "MsgStore.h"

namespace offband {
namespace gui {

// C1 stubs: test_msg_store pins the behavior; C2 implements it.
bool MsgStore::begin(uint16_t, uint16_t) { return false; }
MsgStore::~MsgStore() {}
int MsgStore::thread(ThreadKind, const uint8_t*, size_t, const char*) { return -1; }
int MsgStore::find(ThreadKind, const uint8_t*, size_t) const { return -1; }
const Thread* MsgStore::threadAt(int) const { return nullptr; }
uint16_t MsgStore::threadCount() const { return 0xFFFF; }
uint32_t MsgStore::addIncoming(int, uint32_t, const char*, const char*, size_t, uint8_t, int8_t) { return 0; }
uint32_t MsgStore::addOwn(int, uint32_t, const char*, size_t, Delivery) { return 0; }
const MsgRecord* MsgStore::msg(uint32_t) const { return nullptr; }
bool MsgStore::setDelivery(uint32_t, Delivery) { return false; }
bool MsgStore::addEcho(uint32_t) { return false; }
size_t MsgStore::threadMessages(int, uint32_t*, size_t) const { return 0xFFFF; }
void MsgStore::markRead(int) {}
uint32_t MsgStore::totalUnread() const { return 0xFFFFFFFF; }
bool MsgStore::saveIndex(ByteSink&) const { return false; }
LoadResult MsgStore::loadIndex(ByteSource&) { return LoadResult::Ok; }
bool MsgStore::saveRing(ByteSink&) const { return false; }
LoadResult MsgStore::loadRing(ByteSource&) { return LoadResult::Ok; }

}  // namespace gui
}  // namespace offband
