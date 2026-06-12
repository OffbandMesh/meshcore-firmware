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

    // 1. Secrets MUST NOT leak (jwt view shows neither password nor token).
    if (strstr(buf, "p4ssw0rd-with-symbols!@#")) return fail("password value leaked", buf);
    if (strstr(buf, "SECRETPAYLOAD"))            return fail("jwt_token value leaked", buf);

    // 2. Header must NOT use a leading "label: " -- the MeshCore companion
    //    renders "<word>: <text>" as sender:message and mangles it.
    if (strstr(buf, "mqtt.broker.3:")) return fail("header uses a colon (will mangle in app)", buf);

    // 3. All operator-layout fields present (full names).
    if (!strstr(buf, "url=wss://mqtt.meshmapper.net:443/mqtt")) return fail("url missing", buf);
    if (!strstr(buf, "port=443"))                       return fail("port missing", buf);
    if (!strstr(buf, "transport=wss"))                  return fail("transport missing", buf);
    if (!strstr(buf, "auth_type=jwt"))                  return fail("auth_type missing", buf);
    if (!strstr(buf, "username=auto(v1_+pubkey)"))      return fail("username missing", buf);
    if (!strstr(buf, "jwt_audience=mqtt.meshmapper.net")) return fail("jwt_audience missing", buf);
    if (!strstr(buf, "jwt_owner=18315E8B"))             return fail("jwt_owner missing", buf);
    if (!strstr(buf, "jwt_email=strycher@gmail.com"))   return fail("jwt_email missing", buf);
    if (!strstr(buf, "jwt_refresh=3600"))               return fail("jwt_refresh missing", buf);
    if (!strstr(buf, "ca_cert=isrg-x2"))                return fail("ca_cert missing", buf);
    if (!strstr(buf, "iata=(global)"))                  return fail("iata missing", buf);

    // 4. Fields appear in the operator's familiar order.
    {
        const char* order[] = {"url=","port=","transport=","auth_type=",
            "username=","jwt_audience=","jwt_owner=","jwt_email=",
            "jwt_refresh=","ca_cert=","iata="};
        long prev = -1;
        for (int i = 0; i < 11; i++) {
            const char* pos = strstr(buf, order[i]);
            if (!pos) { printf("FAIL: %s missing (order check)\n%s\n", order[i], buf); return 1; }
            long off = (long)(pos - buf);
            if (off < prev) { printf("FAIL: %s out of order\n%s\n", order[i], buf); return 1; }
            prev = off;
        }
    }

    // 5. Line budget -- the _sys queue is 8 deep and drops the OLDEST when full.
    {
        int lines = 0;
        for (const char* q = buf; *q; ++q) if (*q == '\n') lines++;
        if (lines > 7) {
            printf("FAIL: too many lines (%d > 7) -- _sys queue is only 8 deep\n%s\n", lines, buf);
            return 1;
        }
    }

    // 6. Return value invariant + truncation safety.
    if (len == 0 || len != strlen(buf) || len >= sizeof(buf)) return fail("bad return length", buf);
    char tiny[16];
    size_t tlen = formatBrokerConfig(3, cfg, tiny, sizeof(tiny));
    if (tlen >= sizeof(tiny)) return fail("tiny: len not clamped", tiny);
    if (tlen != strlen(tiny)) return fail("tiny: len != strlen", tiny);

    // 7. tcp/anon slot (the slot-0 bug): NO jwt-specific fields or auto-notes.
    BrokerConfig anon;  // default-constructed: tcp / none, all empty
    char abuf[768];
    formatBrokerConfig(0, anon, abuf, sizeof(abuf));
    if (strstr(abuf, "jwt_owner="))     return fail("anon: jwt_owner shown on tcp/none", abuf);
    if (strstr(abuf, "jwt_audience="))  return fail("anon: jwt_audience shown on tcp/none", abuf);
    if (strstr(abuf, "auto("))          return fail("anon: auto note shown on tcp/none", abuf);
    if (!strstr(abuf, "transport=tcp")) return fail("anon: transport=tcp missing", abuf);
    if (!strstr(abuf, "auth_type=none")) return fail("anon: auth_type=none missing", abuf);
    if (strstr(abuf, "mqtt.broker.0:")) return fail("anon: header colon (will mangle)", abuf);

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
