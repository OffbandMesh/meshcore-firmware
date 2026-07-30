// src/helpers/diagnostics/CrashLogStandard.cpp -- see CrashLogStandard.h (#472 / Epic #350)
#include "CrashLogStandard.h"
#include "CrashLog.h"          // board-agnostic core + OFFBAND_CRASHLOG_HOST selector
#include "../../MeshCore.h"    // MainBoard: getResetReason() / getResetReasonString()

namespace offband {

#if !defined(OFFBAND_CRASHLOG_DISABLED) && !defined(OFFBAND_CRASHLOG_HOST)

// The board is held statically so the reset-reason hook can be a plain
// (non-capturing) function pointer -- CrashLog's ResetReasonHook is
// `const char* (*)()`. One board per node, set once in init.
namespace {
mesh::MainBoard* s_board = nullptr;
const char* resetReasonProvider() {
  if (!s_board) return "n/a";
  return s_board->getResetReasonString(s_board->getResetReason());
}
}  // namespace

void crashLogStandardInit(mesh::MainBoard& board, const char* role_tag) {
  s_board = &board;
  crashLogSetResetReasonHook(&resetReasonProvider);
  crashLogBegin();  // dumps a ring that survived the previous reset, if any
  crashLogf("[boot] %s up; reset=%s", role_tag ? role_tag : "node", resetReasonProvider());
}

void crashLogStandardTick(uint32_t now_ms) {
  crashLogTick(now_ms);
}

#else  // host build or explicit per-board opt-out -> no-ops

void crashLogStandardInit(mesh::MainBoard&, const char*) {}
void crashLogStandardTick(uint32_t) {}

#endif

}  // namespace offband
