#pragma once

// #2297: the layout of offband_4MB.csv (#2295), as the running firmware compares
// it. A build sets OFFBAND_4MB_TABLE when it is made for that table; the three
// 4 MB board families set it next to board_build.partitions.
//
// These numbers must match offband_4MB.csv. scripts/check_4mb_partitions.py
// reads both and fails on any difference, so they cannot drift apart silently.

#include "../PartitionFit.h"

namespace partition_fit {

constexpr uint32_t kOffband4MBAppSize = 0x1B0000;
constexpr uint32_t kOffband4MBSpiffsOffset = 0x370000;
constexpr uint32_t kOffband4MBSpiffsSize = 0x80000;

// The layout this build expects. {0,0,0} means no expectation: every board that
// never changed table, which is every board that isn't 4 MB.
inline Layout expectedLayout() {
#ifdef OFFBAND_4MB_TABLE
  return Layout{kOffband4MBAppSize, kOffband4MBSpiffsOffset, kOffband4MBSpiffsSize};
#else
  return Layout{0, 0, 0};
#endif
}

}  // namespace partition_fit
