#include "NodeViews.h"

// E1 (#1530): stubs so the failing-first tests build. E2-E4 implement them.

namespace offband {
namespace gui {

static void blank(char* out, size_t n) {
  if (n) out[0] = '\0';
}

NodeKind nodeKind(uint8_t) { return NodeKind::Other; }
bool passesFilter(const NodeRow&, TypeFilter, bool) { return false; }
bool searchMatches(const NodeRow&, const char*) { return false; }
size_t nodeOrder(const NodeRow*, size_t, TypeFilter, bool, const char*, uint16_t*, size_t) { return 0; }
void nodeCount(size_t, size_t, bool, char* out, size_t n) { blank(out, n); }
void hopsText(uint8_t, char* out, size_t n) { blank(out, n); }
void agoText(bool, uint32_t, char* out, size_t n) { blank(out, n); }
void rowSub(const NodeRow&, char* out, size_t n) { blank(out, n); }
void rowSnr(float, char* out, size_t n) { blank(out, n); }
void badgeText(uint16_t, char* out, size_t n) { blank(out, n); }
void matchLine(size_t, TypeFilter, char* out, size_t n) { blank(out, n); }
const char* emptyTitle(TypeFilter) { return ""; }

void detailKind(NodeKind, uint8_t, bool, float, char* out, size_t n) { blank(out, n); }
size_t statusCells(const uint8_t*, size_t, uint32_t, Cell*, size_t) { return 0; }
size_t emptyStatusCells(Cell*, size_t) { return 0; }
void thousands(uint32_t, char* out, size_t n) { blank(out, n); }
void traceSnrLine(const uint8_t*, size_t, char* out, size_t n) { blank(out, n); }

void LetterWheel::next() {}
void LetterWheel::prev() {}
LetterWheel::Pick LetterWheel::pick() const { return Pick::Done; }
char LetterWheel::ch() const { return 0; }
const char* LetterWheel::label() const { return ""; }
const char* LetterWheel::labelAt(int) const { return ""; }

void PasswordBook::set(const uint8_t*, const char*) {}
const char* PasswordBook::get(const uint8_t*) const { return nullptr; }
void PasswordBook::forget(const uint8_t*) {}
size_t PasswordBook::size() const { return 0; }
bool PasswordBook::save(ByteSink&) const { return false; }
LoadResult PasswordBook::load(ByteSource&) { return LoadResult::BadHeader; }

}  // namespace gui
}  // namespace offband
