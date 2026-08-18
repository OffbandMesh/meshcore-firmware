#pragma once

#include "DisplayDriver.h"
#include <stddef.h>

// #822 / #705: THE splash. One implementation, drawn by every role.
//
// WHY THIS EXISTS
//   There were SIX independent splash implementations -- ui-new, ui-orig, ui-tiny,
//   simple_repeater, simple_room_server, simple_sensor -- and FIVE of them drew
//   MeshCore artwork unconditionally. Offband branding reached exactly one of them.
//   Anyone running a repeater, room server or sensor saw MeshCore's logo on boot.
//
//   Colour art made it worse: it was gated on a per-VARIANT -D flag, silent when
//   omitted, so colour boards other than the RC32 quietly fell back to mono and
//   nothing failed. Boards were missed that way twice.
//
//   So: one component, and the artwork decision belongs to the DRIVER
//   (DisplayDriver::colourSplashArt), never to a build flag someone has to remember.
//
// MeshCore attribution is preserved as TEXT (#153, MIT obligation). Dropping their
// artwork is a branding change; dropping the credit would be a licence problem.

namespace offband {

struct SplashInfo {
  const char* offband_ver;    // nullptr -> offbandShortVersion()
  const char* meshcore_ver;   // upstream version, rendered as "on MeshCore <v>"
  const char* build_date;
  const char* role_line;      // "< Repeater >" etc; nullptr -> build_date on line 3

  // Explicit ctor rather than a default member initializer: the firmware envs
  // build as gnu++11, where an NSDMI makes this a non-aggregate and every
  // brace-init call site fails to compile. The native test env uses a newer
  // standard and does NOT catch that -- all 23 tests passed while all seven
  // firmware envs failed to build.
  SplashInfo(const char* ver, const char* mc, const char* date,
             const char* role = nullptr)
      : offband_ver(ver), meshcore_ver(mc), build_date(date), role_line(role) {}
};

// Highest logical y any splash line occupies, across every arrangement. Callers and
// tests use it to prove nothing is placed off the 128x64 logical canvas -- the
// clipping trap #758 hit when a taller font pushed the last line past the panel.
int splashLastLineY();

// The compact Offband version shown on the splash -- "1.5.0", "1.5.0+3*", plus any
// OFFBAND_BUILD_TAG. Derived from OFFBAND_VERSION (git describe) per #222. It lived
// in ui-new, which is why only the companion splash ever showed it correctly;
// moved here so all six roles report identically.
const char* offbandShortVersion();

// The pure half of that derivation, split out so it is testable without a build
// macro. `describe` is a git-describe string ("offband-v0.14.0-rc1-4-g369714e");
// `build_tag` may be nullptr. Spec + fixtures: test/test_splash.
//
// The commits-since field is found by git's "-g<sha>" marker, NOT the first dash:
// splitting on the first dash mis-reads a pre-release tag's "-rcN" as the commits
// field and shows a spurious "+0" (#33).
void shortVersionInto(char* out, size_t out_len, const char* describe,
                      const char* build_tag);

// Draws into the CURRENT frame; the caller owns startFrame()/endFrame().
void drawSplash(DisplayDriver& display, const SplashInfo& info);

// Just the brand lockup, no version text -- for screens that are branded but are
// not the splash (the repeater's "Turning OFF"). Returns the y below the artwork
// so the caller can place its own text without re-deriving the art height.
int drawBrandLockup(DisplayDriver& display, int y = 1);

}  // namespace offband
