// Epic E (#1429): what the Nodes screens show, pinned before it is built
// (E1, #1530). Design of record: ObScreen.dc.html, the GUI spec, and the
// owner-approved gap mockups (epic-e-gap-mockups.html, 2026-10-16).

#include <gtest/gtest.h>

#include <string.h>

#include <string>
#include <vector>

#include "helpers/gui/NodeViews.h"

using namespace offband::gui;

namespace {

uint8_t keyA[32] = {0x7c, 0x02, 0xd1, 0xe9};
uint8_t keyB[32] = {0x5b, 0x21, 0x07, 0xfe};
uint8_t keyC[32] = {0xa9, 0x1c, 0x3d, 0x02};
uint8_t keyD[32] = {0xe4, 0xb0, 0x1a, 0x6c};
uint8_t keyE[32] = {0x01, 0x02, 0x03, 0x04};

// The design's sample data (ObScreen nRaw), plus a never-heard node.
std::vector<NodeRow> sample() {
  return {
      {"KD9-Ridge RPT", keyA, NodeKind::Repeater, 1, true, 120, 7.5f, 0},
      {"Wren", keyB, NodeKind::Chat, 2, true, 240, 3.0f, 1},
      {"Valley Ops", keyC, NodeKind::Room, 1, true, 660, 5.1f, 0},
      {"Creek Gauge", keyD, NodeKind::Sensor, 2, true, 540, 4.0f, 0},
      {"Marek T-Deck", keyE, NodeKind::Chat, 3, true, 2520, -6.0f, 2},
      {"Antelope", keyE, NodeKind::Repeater, 0xFF, false, 0, 0.0f, 0},
      {"zulu", keyE, NodeKind::Chat, 0xFF, false, 0, 0.0f, 0},     // never heard, lower case
      {"Yankee", keyE, NodeKind::Chat, 0xFF, false, 0, 0.0f, 0},
  };
}

class MemSink : public ByteSink {
public:
  std::vector<uint8_t> bytes;
  bool write(const void* d, size_t n) override {
    const uint8_t* p = static_cast<const uint8_t*>(d);
    bytes.insert(bytes.end(), p, p + n);
    return true;
  }
};
class MemSource : public ByteSource {
public:
  explicit MemSource(std::vector<uint8_t> b) : _b(std::move(b)) {}
  size_t read(void* d, size_t n) override {
    const size_t k = n < _b.size() - _pos ? n : _b.size() - _pos;
    memcpy(d, _b.data() + _pos, k);
    _pos += k;
    return k;
  }

private:
  std::vector<uint8_t> _b;
  size_t _pos = 0;
};

}  // namespace

// --------------------------------------------------------- E2 the list

TEST(Nodes, KindFromAdvertType) {
  EXPECT_EQ(nodeKind(1), NodeKind::Chat);
  EXPECT_EQ(nodeKind(2), NodeKind::Repeater);
  EXPECT_EQ(nodeKind(3), NodeKind::Room);
  EXPECT_EQ(nodeKind(4), NodeKind::Sensor);
  EXPECT_EQ(nodeKind(0), NodeKind::Other);
  EXPECT_EQ(nodeKind(9), NodeKind::Other);
}

TEST(Nodes, TypeAndUnreadFilters) {
  auto s = sample();
  EXPECT_TRUE(passesFilter(s[0], TypeFilter::All, false));
  EXPECT_TRUE(passesFilter(s[0], TypeFilter::Repeater, false));
  EXPECT_FALSE(passesFilter(s[0], TypeFilter::Chat, false));
  EXPECT_TRUE(passesFilter(s[2], TypeFilter::Room, false));
  EXPECT_TRUE(passesFilter(s[3], TypeFilter::Sensor, false));
  // Unread: only nodes with unread DMs (ObScreen nodesunread).
  EXPECT_TRUE(passesFilter(s[1], TypeFilter::Chat, true));
  EXPECT_FALSE(passesFilter(s[0], TypeFilter::All, true));
}

TEST(Nodes, SearchByNamePrefixOrKeyHex) {
  auto s = sample();
  EXPECT_TRUE(searchMatches(s[1], ""));
  EXPECT_TRUE(searchMatches(s[1], "w"));
  EXPECT_TRUE(searchMatches(s[1], "WR"));       // ignores case
  EXPECT_TRUE(searchMatches(s[1], "ren"));      // anywhere in the name (owner, 2026-10-17)
  EXPECT_TRUE(searchMatches(s[0], "ridge"));
  EXPECT_FALSE(searchMatches(s[1], "renx"));
  EXPECT_TRUE(searchMatches(s[0], "7c02"));     // key hex
  EXPECT_TRUE(searchMatches(s[0], "7C02D1"));
  EXPECT_FALSE(searchMatches(s[0], "7c03"));
  EXPECT_TRUE(searchMatches(s[0], "kd9"));
}

TEST(Nodes, OrderIsMostRecentlyHeardThenNeverHeardAtoZ) {
  auto s = sample();
  uint16_t out[10];
  const size_t n = nodeOrder(s.data(), s.size(), TypeFilter::All, false, "", out, 10);
  ASSERT_EQ(n, 8u);
  EXPECT_EQ(out[0], 0);   // 2m
  EXPECT_EQ(out[1], 1);   // 4m
  EXPECT_EQ(out[2], 3);   // 9m
  EXPECT_EQ(out[3], 2);   // 11m
  EXPECT_EQ(out[4], 4);   // 42m
  EXPECT_EQ(out[5], 5);   // never heard, A-Z ignoring case: Antelope,
  EXPECT_EQ(out[6], 7);   // Yankee,
  EXPECT_EQ(out[7], 6);   // zulu
}

// A name that is also hex matches by name or by key; a space matches only
// names that start with one.
TEST(Nodes, SearchEdges) {
  NodeRow cafe = {"cafe", keyA, NodeKind::Chat, 0, true, 0, 0, 0};
  EXPECT_TRUE(searchMatches(cafe, "cafe"));   // by name, though keyA is 7c02...
  EXPECT_TRUE(searchMatches(cafe, "7c"));     // by key
  EXPECT_FALSE(searchMatches(cafe, " "));
}

TEST(Nodes, OrderAppliesFiltersAndSearch) {
  auto s = sample();
  uint16_t out[8];
  EXPECT_EQ(nodeOrder(s.data(), s.size(), TypeFilter::Chat, true, "", out, 8), 2u);   // "2 of 12"
  EXPECT_EQ(out[0], 1);
  EXPECT_EQ(out[1], 4);
  EXPECT_EQ(nodeOrder(s.data(), s.size(), TypeFilter::All, false, "w", out, 8), 1u);
  EXPECT_EQ(out[0], 1);
  EXPECT_EQ(nodeOrder(s.data(), s.size(), TypeFilter::All, false, "", out, 2), 2u);   // stops at max
  EXPECT_EQ(nodeOrder(s.data(), s.size(), TypeFilter::All, false, "zq", out, 8), 0u);
}

TEST(Nodes, HeaderCount) {
  char b[16];
  nodeCount(12, 12, false, b, sizeof b);
  EXPECT_STREQ(b, "12");
  nodeCount(2, 12, true, b, sizeof b);
  EXPECT_STREQ(b, "2 of 12");
  nodeCount(0, 0, false, b, sizeof b);
  EXPECT_STREQ(b, "0");
}

TEST(Nodes, HopsText) {
  char b[16];
  hopsText(0, b, sizeof b);
  EXPECT_STREQ(b, "direct");
  hopsText(1, b, sizeof b);
  EXPECT_STREQ(b, "1 hop");
  hopsText(3, b, sizeof b);
  EXPECT_STREQ(b, "3 hops");
  hopsText(0xFF, b, sizeof b);
  EXPECT_STREQ(b, "flood");
  hopsText(0x40 | 2, b, sizeof b);   // a 2-byte-hash path: the low 6 bits count hops
  EXPECT_STREQ(b, "2 hops");
  hopsText(0x40 | 63, b, sizeof b);  // the most hops a path length can say, not "flood"
  EXPECT_STREQ(b, "63 hops");
}

TEST(Nodes, AgoText) {
  char b[8];
  agoText(false, 0, b, sizeof b);
  EXPECT_STREQ(b, "—");
  agoText(true, 30, b, sizeof b);
  EXPECT_STREQ(b, "now");
  agoText(true, 240, b, sizeof b);
  EXPECT_STREQ(b, "4m");
  agoText(true, 3600, b, sizeof b);
  EXPECT_STREQ(b, "1h");
  agoText(true, 3 * 86400 + 5, b, sizeof b);
  EXPECT_STREQ(b, "3d");
}

TEST(Nodes, RowLines) {
  auto s = sample();
  char b[32];
  rowSub(s[1], b, sizeof b);
  EXPECT_STREQ(b, "2 hops · 4m");
  rowSub(s[5], b, sizeof b);
  EXPECT_STREQ(b, "flood · —");
  rowSnr(7.5f, b, sizeof b);
  EXPECT_STREQ(b, "+7.5");
  rowSnr(-2.5f, b, sizeof b);
  EXPECT_STREQ(b, "-2.5");
  badgeText(3, b, sizeof b);
  EXPECT_STREQ(b, "3");
  badgeText(100, b, sizeof b);
  EXPECT_STREQ(b, "99+");
}

TEST(Nodes, SearchAndEmptyLines) {
  char b[40];
  matchLine(2, TypeFilter::All, b, sizeof b);
  EXPECT_STREQ(b, "2 matches · all types");
  matchLine(1, TypeFilter::Repeater, b, sizeof b);
  EXPECT_STREQ(b, "1 match · repeaters");
  matchLine(0, TypeFilter::Chat, b, sizeof b);
  EXPECT_STREQ(b, "0 matches · chat");
  EXPECT_STREQ(emptyTitle(TypeFilter::All), "No nodes heard yet");
  EXPECT_STREQ(emptyTitle(TypeFilter::Repeater), "No repeaters heard yet");
  EXPECT_STREQ(emptyTitle(TypeFilter::Sensor), "No sensors heard yet");
}

// ----------------------------------------------------------- E4 detail

TEST(Detail, KindLine) {
  char b[48];
  detailKind(NodeKind::Repeater, 1, true, 7.5f, b, sizeof b);
  EXPECT_STREQ(b, "Repeater · 1 hop · +7.5 dB");
  detailKind(NodeKind::Chat, 2, true, 3.0f, b, sizeof b);
  EXPECT_STREQ(b, "Companion · 2 hops · +3.0 dB");
  detailKind(NodeKind::Room, 0xFF, false, 0, b, sizeof b);
  EXPECT_STREQ(b, "Room server · flood");
  detailKind(NodeKind::Sensor, 0, true, -1.25f, b, sizeof b);
  EXPECT_STREQ(b, "Sensor · direct · -1.3 dB");
}

namespace {
// A status reply body: the 6-byte key prefix, then RepeaterStats
// (examples/simple_repeater/MyMesh.h), little-endian.
std::vector<uint8_t> statusBody() {
  std::vector<uint8_t> b(6 + 56, 0);
  auto u16 = [&](size_t o, uint16_t v) { memcpy(&b[6 + o], &v, 2); };
  auto i16 = [&](size_t o, int16_t v) { memcpy(&b[6 + o], &v, 2); };
  auto u32 = [&](size_t o, uint32_t v) { memcpy(&b[6 + o], &v, 4); };
  u16(0, 3940);                        // batt_milli_volts
  i16(4, -118);                        // noise_floor
  u32(16, 43435);                      // total_air_time_secs (transmit; receive is its own field): 4.1% of uptime
  u32(20, 12 * 86400 + 6 * 3600);      // total_up_time_secs
  u32(24, 2000);                       // n_sent_flood
  u32(28, 318);                        // n_sent_direct
  return b;
}
}  // namespace

TEST(Detail, StatusCells) {
  const auto body = statusBody();
  Cell c[8];
  ASSERT_EQ(statusCells(body.data(), body.size(), 1800, c, 8), 6u);
  EXPECT_STREQ(c[0].key, "Battery");
  EXPECT_STREQ(c[0].value, "3.94 V");
  EXPECT_STREQ(c[1].key, "Uptime");
  EXPECT_STREQ(c[1].value, "12d 6h");
  EXPECT_STREQ(c[2].key, "Airtime");
  EXPECT_STREQ(c[2].value, "4.1%");
  EXPECT_STREQ(c[3].key, "Relayed");
  EXPECT_STREQ(c[3].value, "2,318");
  EXPECT_STREQ(c[4].key, "Noise");
  EXPECT_STREQ(c[4].value, "-118");
  EXPECT_STREQ(c[5].key, "RTT");
  EXPECT_STREQ(c[5].value, "1.8 s");
}

TEST(Detail, StatusCellsRejectAShortBody) {
  const auto body = statusBody();
  Cell c[8];
  EXPECT_EQ(statusCells(body.data(), 6 + 20, 0, c, 8), 0u);
  EXPECT_EQ(statusCells(nullptr, 0, 0, c, 8), 0u);
}

TEST(Detail, NoRttBeforeItIsKnown) {
  const auto body = statusBody();
  Cell c[8];
  ASSERT_EQ(statusCells(body.data(), body.size(), 0, c, 8), 6u);
  EXPECT_STREQ(c[5].value, "—");
}

TEST(Detail, EmptyCellsDash) {
  Cell c[8];
  ASSERT_EQ(emptyStatusCells(c, 8), 6u);
  EXPECT_STREQ(c[0].key, "Battery");
  for (int i = 0; i < 6; i++) EXPECT_STREQ(c[i].value, "—");
}

TEST(Detail, Thousands) {
  char b[16];
  thousands(0, b, sizeof b);
  EXPECT_STREQ(b, "0");
  thousands(999, b, sizeof b);
  EXPECT_STREQ(b, "999");
  thousands(2318, b, sizeof b);
  EXPECT_STREQ(b, "2,318");
  thousands(1234567, b, sizeof b);
  EXPECT_STREQ(b, "1,234,567");
}

TEST(Detail, TraceSnrs) {
  // path_len 2 (two 1-byte hashes), flags 0, tag, auth, hashes, SNRs x4, final.
  std::vector<uint8_t> b = {2, 0, 1, 0, 0, 0, 2, 0, 0, 0, 0xAA, 0xBB};
  b.push_back(uint8_t(int8_t(25)));    // +6.25, rounded half away from zero: +6.3
  b.push_back(uint8_t(int8_t(16)));    // +4.0
  b.push_back(uint8_t(int8_t(30)));    // +7.5, the final hop to us
  char out[64];
  traceSnrLine(b.data(), b.size(), out, sizeof out);
  EXPECT_STREQ(out, "snr: +6.3 › +4.0 › +7.5");
  traceSnrLine(b.data(), 5, out, sizeof out);   // cut short
  EXPECT_STREQ(out, "");
}

// 2-byte hashes (flags & 3 == 1): 4 bytes of hashes are 2 hops, so 2 SNRs
// and the final one.
TEST(Detail, TraceSnrsWithWideHashes) {
  std::vector<uint8_t> b = {4, 1, 1, 0, 0, 0, 2, 0, 0, 0, 0xAA, 0xAB, 0xBA, 0xBB};
  b.push_back(uint8_t(int8_t(-8)));    // -2.0
  b.push_back(uint8_t(int8_t(12)));    // +3.0
  b.push_back(uint8_t(int8_t(4)));     // +1.0
  char out[64];
  traceSnrLine(b.data(), b.size(), out, sizeof out);
  EXPECT_STREQ(out, "snr: -2.0 › +3.0 › +1.0");
}

// ------------------------------------------------------ E3 letter wheel

TEST(Wheel, WalksLettersDigitsThenControls) {
  LetterWheel w;
  EXPECT_EQ(w.pick(), LetterWheel::Pick::Char);
  EXPECT_EQ(w.ch(), 'A');
  EXPECT_STREQ(w.label(), "A");
  for (int i = 0; i < 25; i++) w.next();
  EXPECT_EQ(w.ch(), 'Z');
  w.next();
  EXPECT_EQ(w.ch(), '0');
  for (int i = 0; i < 9; i++) w.next();
  EXPECT_EQ(w.ch(), '9');
  w.next();
  EXPECT_EQ(w.ch(), ' ');
  EXPECT_STREQ(w.label(), "space");
  w.next();
  EXPECT_EQ(w.pick(), LetterWheel::Pick::Backspace);
  EXPECT_STREQ(w.label(), "⌫");
  w.next();
  EXPECT_EQ(w.pick(), LetterWheel::Pick::Done);
  EXPECT_STREQ(w.label(), "Done");
  w.next();
  EXPECT_EQ(w.ch(), 'A');   // round again
}

TEST(Wheel, PrevWrapsAndTheStripLooksBothWays) {
  LetterWheel w;
  w.prev();
  EXPECT_EQ(w.pick(), LetterWheel::Pick::Done);
  w.next();
  EXPECT_STREQ(w.labelAt(-1), "Done");
  EXPECT_STREQ(w.labelAt(1), "B");
  EXPECT_STREQ(w.labelAt(-2), "⌫");
}

// ----------------------------------------------------- E4 password book

TEST(Passwords, SetGetForget) {
  PasswordBook p;
  EXPECT_EQ(p.get(keyA), nullptr);
  p.set(keyA, "hunter2");
  ASSERT_NE(p.get(keyA), nullptr);
  EXPECT_STREQ(p.get(keyA), "hunter2");
  p.set(keyA, "changed");
  EXPECT_STREQ(p.get(keyA), "changed");
  EXPECT_EQ(p.size(), 1u);
  p.forget(keyA);
  EXPECT_EQ(p.get(keyA), nullptr);
  EXPECT_EQ(p.size(), 0u);
}

TEST(Passwords, LongPasswordIsCut) {
  PasswordBook p;
  p.set(keyA, "0123456789abcdefXYZ");
  ASSERT_NE(p.get(keyA), nullptr);
  EXPECT_EQ(strlen(p.get(keyA)), kPasswordLen - 1);
}

TEST(Passwords, FullBookDropsTheLeastRecentlySet) {
  PasswordBook p;
  uint8_t k[kPasswordSlots + 1][32] = {};
  for (size_t i = 0; i <= kPasswordSlots; i++) {
    k[i][0] = uint8_t(i + 1);
    p.set(k[i], "pw");
  }
  EXPECT_EQ(p.size(), kPasswordSlots);
  EXPECT_EQ(p.get(k[0]), nullptr);       // the oldest went
  EXPECT_NE(p.get(k[kPasswordSlots]), nullptr);
}

TEST(Passwords, SurviveARoundTrip) {
  PasswordBook p;
  p.set(keyA, "alpha");
  p.set(keyB, "bravo");
  MemSink sink;
  ASSERT_TRUE(p.save(sink));
  PasswordBook q;
  MemSource src(sink.bytes);
  ASSERT_EQ(q.load(src), LoadResult::Ok);
  EXPECT_STREQ(q.get(keyA), "alpha");
  EXPECT_STREQ(q.get(keyB), "bravo");
}

// Two keys that share their first bytes are still two nodes.
TEST(Passwords, KeyedByTheWholeKey) {
  uint8_t twin[32];
  memcpy(twin, keyA, 32);
  twin[31] ^= 1;
  PasswordBook p;
  p.set(keyA, "mine");
  p.set(twin, "theirs");
  EXPECT_EQ(p.size(), 2u);
  EXPECT_STREQ(p.get(keyA), "mine");
}

TEST(Passwords, NewerFileIsNotRead) {
  PasswordBook p;
  p.set(keyA, "alpha");
  MemSink sink;
  ASSERT_TRUE(p.save(sink));
  sink.bytes[4] = 0xFF;   // the version: bytes 4-5 of the header, after the magic
  sink.bytes[5] = 0x7F;
  PasswordBook q;
  MemSource src(sink.bytes);
  EXPECT_EQ(q.load(src), LoadResult::UnknownVersion);
  EXPECT_EQ(q.size(), 0u);
}

TEST(Passwords, BadFileIsNotTrusted) {
  PasswordBook q;
  MemSource empty({});
  EXPECT_EQ(q.load(empty), LoadResult::Empty);
  MemSource junk({1, 2, 3, 4, 5, 6, 7, 8});
  EXPECT_EQ(q.load(junk), LoadResult::BadHeader);
  EXPECT_EQ(q.size(), 0u);
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
