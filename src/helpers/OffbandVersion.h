#pragma once
//
// Offband canonical version string -- the single source of the version text that
// the CLI (`ver` / `version`) and the companion device-info field both report, so
// all three agree (owner-agreed 2026-10-07, OffbandMesh/meshcore-firmware#1391).
//
// Format: OB-first `<ob>-<stock>`, e.g. "1.5.0-1.17.0".
//   * <stock> = the example's FIRMWARE_VERSION core (leading 'v' and any -suffix trimmed).
//   * <ob>    = the offband-v* tag core; or, when NO offband-v* tag is reachable at build
//               time (a shallow/tagless CI or beta build), the short git SHA. It is never
//               silently dropped to a bare stock version -- a missing stamp must be loud,
//               not a plausible stockver (#52 acceptance; owner directive #1391).
//   * an optional build tag (#222) suffix lets flag-only variants (e.g. "diag")
//     self-identify; the version cores truncate first if the 20-char field is tight.
//
// Split into a pure core (offbandVersionStringFrom, unit-tested) and a thin wrapper
// that binds the build-injected macros. stockVer is passed in because FIRMWARE_VERSION
// is #defined per example (MyMesh.h), while OFFBAND_* are injected globally by
// scripts/inject_offband_version.py. Returns a static buffer the caller uses
// immediately (the CLI is single-threaded), matching the offbandClientVersion()
// contract this replaces.
//
#include <string.h>
#include <stdio.h>

// Pure core. obVersion/obSha/buildTag may each be NULL (not injected). No globals, no
// macros -- directly unit-testable (test/test_offband_version/).
// NOT re-entrant: returns a pointer to a function-static buffer, so each caller must use
// the result before the next call. Every caller (the CLI handlers, the companion
// device-info reply) runs on the single main/CLI thread and consumes it immediately; do
// not call this from an ISR, and do not hold the returned pointer across another call
// that could preempt it (#1391, Gemini review 2026-10-07).
static inline const char* offbandVersionStringFrom(const char* stockVer,
                                                   const char* obVersion,
                                                   const char* obSha,
                                                   const char* buildTag) {
  static char buf[20];

  // Stock core: strip a leading 'v', then take up to the first '-'.
  const char* mc = stockVer ? stockVer : "";
  if (*mc == 'v' || *mc == 'V') mc++;
  char mc_core[12];
  size_t m = 0;
  while (mc[m] && mc[m] != '-' && m < sizeof(mc_core) - 1) { mc_core[m] = mc[m]; m++; }
  mc_core[m] = '\0';

  // OB core: the offband-v* tag core; else the short SHA (never bare stock, #1391).
  char ob_core[16];
  ob_core[0] = '\0';
  if (obVersion != NULL) {
    const char* ob = strstr(obVersion, "offband-v");
    if (ob != NULL) {
      ob += strlen("offband-v");
      size_t n = 0;
      while (ob[n] && ob[n] != '-' && n < sizeof(ob_core) - 1) { ob_core[n] = ob[n]; n++; }
      ob_core[n] = '\0';
    }
  }
  if (ob_core[0] == '\0' && obSha != NULL && obSha[0] != '\0') {
    size_t n = 0;
    // stop at '-' too, in case a SHA field ever carries a git-describe '-dirty' suffix.
    while (obSha[n] && obSha[n] != '-' && n < sizeof(ob_core) - 1) { ob_core[n] = obSha[n]; n++; }
    ob_core[n] = '\0';
  }

  // No Offband identity at all (pure upstream build): the stock CORE, kept consistent
  // with the OB paths (leading 'v' and any -suffix already trimmed), never a raw string.
  if (ob_core[0] == '\0') {
    snprintf(buf, sizeof(buf), "%s", mc_core);
    return buf;
  }

  // #222: flag-only variants self-identify; reserve room for the tag so it survives,
  // the version cores truncate first if the 20-char field is tight.
  if (buildTag != NULL && buildTag[0] != '\0') {
    char vers[20];
    snprintf(vers, sizeof(vers), "%s-%s", ob_core, mc_core);
    int tagroom = (int)strlen(buildTag) + 1;   // "-<tag>"
    int vmax = (int)sizeof(buf) - 1 - tagroom;
    if (vmax < 0) vmax = 0;
    snprintf(buf, sizeof(buf), "%.*s-%s", vmax, vers, buildTag);
    return buf;
  }

  snprintf(buf, sizeof(buf), "%s-%s", ob_core, mc_core);   // OB-first (#1391)
  return buf;
}

// Firmware wrapper: binds the build-injected identity macros (each absent -> NULL).
static inline const char* offbandVersionString(const char* stockVer) {
  const char* obv = NULL;
  const char* obs = NULL;
  const char* obt = NULL;
#if defined(OFFBAND_VERSION)
  obv = OFFBAND_VERSION;
#endif
#if defined(OFFBAND_GIT_SHA)
  obs = OFFBAND_GIT_SHA;
#endif
#if defined(OFFBAND_BUILD_TAG)
  obt = OFFBAND_BUILD_TAG;
#endif
  return offbandVersionStringFrom(stockVer, obv, obs, obt);
}
