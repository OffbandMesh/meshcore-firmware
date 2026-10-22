#include "SdDiagLog.h"

#if defined(PIN_SD_CS) && defined(OFFBAND_SD_DIAG_LOG)

#include <stdio.h>
#include <sys/stat.h>

#include <MeshLog.h>

namespace {
constexpr const char* kLog = "/sd/offband-diag.log";
constexpr const char* kOld = "/sd/offband-diag.old.log";
constexpr long kMaxBytes = 4L * 1024 * 1024;
constexpr uint32_t kEveryMs = 1000;
// A pass writes at most this much, so a backlog can't hold the loop.
constexpr int kChunks = 8;
uint8_t s_buf[512];
}  // namespace

void SdDiagLog::tick(uint32_t now_ms) {
  if (_started && now_ms - _last < kEveryMs) return;
  _last = now_ms;

  struct stat st;
  if (stat(kLog, &st) == 0 && st.st_size > kMaxBytes) {
    remove(kOld);
    rename(kLog, kOld);
  }
  FILE* f = fopen(kLog, "a");
  if (!f) return;
  if (!_started) {
    _started = true;
    fprintf(f, "\n===== boot, card mounted at %lu ms =====\n", (unsigned long)now_ms);
  }
  for (int i = 0; i < kChunks; i++) {
    const uint64_t before = _cursor;
    uint64_t lost = 0;
    const size_t n = meshLogReadFrom(&_cursor, s_buf, sizeof s_buf, &lost);
    if (lost) fprintf(f, "[... %llu bytes of log lost before this ...]\n", (unsigned long long)lost);
    if (!n) break;
    if (fwrite(s_buf, 1, n, f) != n) {
      // The card is full or went away mid-write: read these lines again next
      // pass (unless the ring has moved past them by then).
      _cursor = before + lost;
      break;
    }
  }
  fclose(f);   // closed every pass: a card pulled now loses at most a second
}

#else

void SdDiagLog::tick(uint32_t now_ms) { (void)now_ms; }

#endif
