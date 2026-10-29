// #1738 (Epic V #1893): LVGL's allocator asks PSRAM first and falls back to
// internal RAM. The device code (hw/LvAlloc.cpp) only plugs heap_caps calls
// into this policy, so the policy is what's tested: which heap serves a
// request, that the fallback is used only when the preferred heap fails, and
// that a realloc to size 0 is never retried (it already freed the block).

#include <gtest/gtest.h>

#include "helpers/gui/LvAlloc.h"

using offband::gui::allocPreferring;
using offband::gui::reallocPreferring;

namespace {

char psramBlock[8];
char internalBlock[8];
char movedBlock[8];

struct Calls {
  int prefer = 0;
  int fallback = 0;
};

}  // namespace

TEST(LvAlloc, PsramServesWhenItCan) {
  Calls c;
  void* p = allocPreferring(
      64, [&](size_t) { c.prefer++; return (void*)psramBlock; },
      [&](size_t) { c.fallback++; return (void*)internalBlock; });
  EXPECT_EQ(psramBlock, p);
  EXPECT_EQ(1, c.prefer);
  EXPECT_EQ(0, c.fallback);   // internal RAM untouched
}

TEST(LvAlloc, InternalRamOnlyWhenPsramFails) {
  Calls c;
  void* p = allocPreferring(
      64, [&](size_t) { c.prefer++; return (void*)nullptr; },
      [&](size_t) { c.fallback++; return (void*)internalBlock; });
  EXPECT_EQ(internalBlock, p);
  EXPECT_EQ(1, c.prefer);
  EXPECT_EQ(1, c.fallback);
}

TEST(LvAlloc, BothHeapsFullIsNull) {
  void* p = allocPreferring(
      64, [](size_t) { return (void*)nullptr; }, [](size_t) { return (void*)nullptr; });
  EXPECT_EQ(nullptr, p);
}

TEST(LvAlloc, TheSizeReachesBothHeapsUnchanged) {
  size_t seenPrefer = 0, seenFallback = 0;
  allocPreferring(
      4096, [&](size_t n) { seenPrefer = n; return (void*)nullptr; },
      [&](size_t n) { seenFallback = n; return (void*)internalBlock; });
  EXPECT_EQ(4096u, seenPrefer);
  EXPECT_EQ(4096u, seenFallback);
}

TEST(LvAlloc, ReallocMovesToPsramWhenItCan) {
  Calls c;
  void* p = reallocPreferring(
      internalBlock, 128, [&](void*, size_t) { c.prefer++; return (void*)movedBlock; },
      [&](void*, size_t) { c.fallback++; return (void*)internalBlock; });
  EXPECT_EQ(movedBlock, p);
  EXPECT_EQ(0, c.fallback);
}

TEST(LvAlloc, ReallocFallsBackWithTheSameBlockAndSize) {
  void* seenBlock = nullptr;
  size_t seenSize = 0;
  void* p = reallocPreferring(
      psramBlock, 128, [](void*, size_t) { return (void*)nullptr; },
      [&](void* q, size_t n) { seenBlock = q; seenSize = n; return (void*)internalBlock; });
  EXPECT_EQ(internalBlock, p);
  EXPECT_EQ(psramBlock, seenBlock);   // the original block, still valid after a failed realloc
  EXPECT_EQ(128u, seenSize);
}

TEST(LvAlloc, ReallocToZeroIsNotRetried) {
  Calls c;
  void* p = reallocPreferring(
      psramBlock, 0, [&](void*, size_t) { c.prefer++; return (void*)nullptr; },
      [&](void*, size_t) { c.fallback++; return (void*)internalBlock; });
  EXPECT_EQ(nullptr, p);
  EXPECT_EQ(1, c.prefer);
  EXPECT_EQ(0, c.fallback);   // the block is already freed: a retry would be a double free
}

TEST(LvAlloc, ReallocWithBothHeapsFullIsNull) {
  void* p = reallocPreferring(
      psramBlock, 128, [](void*, size_t) { return (void*)nullptr; },
      [](void*, size_t) { return (void*)nullptr; });
  EXPECT_EQ(nullptr, p);
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
