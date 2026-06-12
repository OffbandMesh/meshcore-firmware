#!/usr/bin/env python
"""
scripts/test_observer_broker_format.py

Host-runnable test for crosswire::formatBrokerConfig (#98, `mqtt view <N>`).
Compiles ConfigSchema.cpp against the in-memory Preferences mock and asserts
that the rendered per-slot view:

  - REDACTS secrets (password + jwt_token values never appear verbatim; only
    derived "(set, len=N)" / "(cached, len=N)" markers do), per the CLAUDE.md
    "never echo a secret" rule;
  - SHOWS the non-secret config values an operator needs to verify a slot
    (url, transport, auth, audience, ca_cert, jwt_owner, jwt_email);
  - notes the auto-derived username for JWT slots;
  - truncates safely into a small buffer (NUL-terminated, return == strlen).

Run with:  python scripts/test_observer_broker_format.py

Reuses the compiler-discovery + build dispatch from
test_observer_nvs_round_trip.py (same dir) rather than duplicating it.
"""

from __future__ import annotations

import subprocess
import sys
import tempfile
from pathlib import Path

# Reuse the proven build harness (PREFS_MOCK + MSVC/g++ dispatch). Importing
# the module does not run its main() (guarded by __main__).
sys.path.insert(0, str(Path(__file__).resolve().parent))
from test_observer_nvs_round_trip import (  # noqa: E402
    PREFS_MOCK,
    _find_msvc_vcvars,
    _build_with_msvc,
    _build_with_gcc,
)

HARNESS = r"""
#define ARDUINO 1
#define CROSSWIRE_OBSERVER 1
#include "prefs_mock.h"
#include "src/helpers/wifi_observer/ConfigSchema.cpp"

#include <cstdio>
#include <cstring>

static int fail(const char* why, const char* buf) {
    printf("FAIL: %s\n--- view ---\n%s\n", why, buf);
    return 1;
}

int main() {
    using namespace crosswire;

    BrokerConfig cfg;
    cfg.enabled   = false;
    strcpy(cfg.url, "wss://mqtt.meshmapper.net:443/mqtt");
    cfg.port      = 443;
    cfg.transport = BrokerTransport::Wss;
    cfg.auth_type = BrokerAuthType::Jwt;
    // Secrets -- must be redacted in the view.
    strcpy(cfg.password,  "p4ssw0rd-with-symbols!@#");          // len 24
    strcpy(cfg.jwt_token, "eyJhbGciOiJFZDI1NTE5In0.SECRETPAYLOAD.SIG");
    // Non-secret config the operator must be able to verify.
    strcpy(cfg.jwt_audience, "mqtt.meshmapper.net");
    strcpy(cfg.jwt_owner,    "18315E8B18315E8B18315E8B18315E8B18315E8B18315E8B18315E8B18315E8B");
    strcpy(cfg.jwt_email,    "strycher@gmail.com");
    strcpy(cfg.ca_cert_name, "isrg-x2");
    strcpy(cfg.topic_prefix, "meshcore");
    cfg.jwt_refresh_sec = 3600;
    // username left empty -> jwt auto-derive note expected.

    char buf[768];
    size_t len = formatBrokerConfig(3, cfg, buf, sizeof(buf));

    // 1. Secrets MUST NOT leak verbatim.
    if (strstr(buf, "p4ssw0rd-with-symbols!@#")) return fail("password value leaked", buf);
    if (strstr(buf, "SECRETPAYLOAD"))            return fail("jwt_token value leaked", buf);

    // 2. Redaction markers (derived properties only).
    if (!strstr(buf, "password = (set, len=24)")) return fail("password redaction marker missing", buf);
    if (!strstr(buf, "jwt_token = (cached"))      return fail("jwt_token redaction marker missing", buf);

    // 3. Non-secret config values ARE shown.
    if (!strstr(buf, "url = wss://mqtt.meshmapper.net:443/mqtt")) return fail("url missing", buf);
    if (!strstr(buf, "transport = wss"))                         return fail("transport missing", buf);
    if (!strstr(buf, "auth_type = jwt"))                         return fail("auth_type missing", buf);
    if (!strstr(buf, "jwt_audience = mqtt.meshmapper.net"))      return fail("audience missing", buf);
    if (!strstr(buf, "ca_cert = isrg-x2"))                       return fail("ca_cert missing", buf);
    if (!strstr(buf, "jwt_owner = 18315E8B"))                    return fail("jwt_owner missing", buf);
    if (!strstr(buf, "jwt_email = strycher@gmail.com"))          return fail("jwt_email missing", buf);

    // 4. JWT slot w/ empty username -> auto-derive note.
    if (!strstr(buf, "username = (auto")) return fail("username auto note missing", buf);

    // 5. Return value invariant: len == strlen(buf), non-empty, bounded.
    if (len == 0 || len != strlen(buf) || len >= sizeof(buf))
        return fail("bad return length", buf);

    // 6. Truncation safety: tiny buffer must NUL-terminate, never overrun,
    //    and return the actual (clamped) content length.
    char tiny[16];
    size_t tlen = formatBrokerConfig(3, cfg, tiny, sizeof(tiny));
    if (tlen >= sizeof(tiny))     return fail("tiny: len not clamped", tiny);
    if (tlen != strlen(tiny))     return fail("tiny: len != strlen", tiny);

    // 7. unset/none fallbacks render for an empty slot.
    BrokerConfig empty;  // default-constructed: all empty, tcp/none
    char ebuf[768];
    formatBrokerConfig(0, empty, ebuf, sizeof(ebuf));
    if (!strstr(ebuf, "password = (unset)"))   return fail("empty: password unset missing", ebuf);
    if (!strstr(ebuf, "jwt_token = (none"))    return fail("empty: jwt_token none missing", ebuf);
    if (!strstr(ebuf, "ca_cert = (none)"))     return fail("empty: ca_cert none missing", ebuf);

    puts("OK");
    return 0;
}
"""


def main() -> int:
    project_root = Path.cwd()
    vcvars = _find_msvc_vcvars()
    import shutil
    gcc_path = shutil.which("g++")
    if vcvars is not None:
        print(f"compiler: MSVC ({vcvars})")
    elif gcc_path is not None:
        print(f"compiler: g++ ({gcc_path})")
    else:
        print("FAIL: no C++ host compiler found (MSVC or g++).")
        return 1

    with tempfile.TemporaryDirectory() as td_str:
        td = Path(td_str)
        (td / "prefs_mock.h").write_text(PREFS_MOCK)
        (td / "harness.cpp").write_text(HARNESS)
        (td / "Arduino.h").write_text("#pragma once\n")
        (td / "Preferences.h").write_text("#pragma once\n")

        if vcvars is not None:
            rc, build_msg, exe_path = _build_with_msvc(td, project_root, vcvars)
        else:
            rc, build_msg, exe_path = _build_with_gcc(td, project_root)

        if rc != 0:
            print("FAIL compile:")
            print(build_msg)
            return 1

        r = subprocess.run([str(exe_path)], capture_output=True, text=True)
        out = (r.stdout or "").strip()
        if out.endswith("OK") and r.returncode == 0:
            print("OK: formatBrokerConfig redaction + field-dump tests pass.")
            return 0
        print(f"FAIL (rc={r.returncode}): {out}")
        if r.stderr:
            print(f"stderr:\n{r.stderr}")
        return 1


if __name__ == "__main__":
    sys.exit(main())
