// src/helpers/wifi_observer/ObserverCaplogForward.cpp -- see the header (#1194).
#include "ObserverCaplogForward.h"

#if defined(OFFBAND_OBSERVER) && defined(OFFBAND_CAPLOG_FORWARD)

#include <string.h>

#include "../../MeshLog.h"
#include "../CaplogForwardCli.h"
#include "../CaplogUdpSink.h"
#include "ConfigSchema.h"
#include "WifiBootstrap.h"

namespace offband {

static CaplogUdpSink s_sink;
// Cursor mode: meshLogReadFrom() copies and removes nothing. The tag is set
// once the identity is loaded; until then lines carry "caplog-unknown".
static CaplogForward s_fwd(nullptr, meshLogReadFrom, s_sink);
static char          s_host[kSyslogHostMax + 1] = {0};
static uint16_t      s_port = kDefaultSyslogPort;

CaplogForward& caplogForwarder() {
    return s_fwd;
}

bool caplogForwardLinkUp() {
    return wifiBootstrap().isStaConnected();
}

void observerCaplogForwardBegin(uint32_t now_ms) {
    if (!readSyslogHost(s_host, sizeof(s_host))) s_host[0] = '\0';
    s_port = readSyslogPort();
    if (readCaplogForwardUntilOff()) s_fwd.armUntilOff(now_ms);
}

void observerCaplogForwardSetIdentity(const uint8_t* pub_key, const char* device_id) {
    char tag[17];
    caplogPubKeyTag(pub_key, tag, sizeof(tag));
    s_fwd.setTag(tag);
    s_fwd.setIdentity(device_id);
}

void observerCaplogForwardService(uint32_t now_ms) {
    s_fwd.service(s_host, s_port, caplogForwardLinkUp(), now_ms);
}

void observerCaplogForwardSetSink(const char* host, uint16_t port) {
    if (host != nullptr) {
        strncpy(s_host, host, sizeof(s_host) - 1);
        s_host[sizeof(s_host) - 1] = '\0';
    }
    if (port != 0) s_port = port;
}

const char* observerCaplogForwardHost() {
    return s_host;
}

uint16_t observerCaplogForwardPort() {
    return s_port;
}

}  // namespace offband

#endif  // OFFBAND_OBSERVER && OFFBAND_CAPLOG_FORWARD
