// src/helpers/wifi_observer/NeighborsReport.cpp -- see NeighborsReport.h (#1399).

#include "NeighborsReport.h"
#include <stdio.h>
#include <string.h>

namespace offband {

namespace {

// The same policy as MqttPayload's escapeJsonString (quote, backslash and the usual
// whitespace escaped, any other control byte replaced by '_'), repeated here because
// MqttPayload.cpp needs the observer role headers and is not in the native build.
void escapeJson(const char* in, char* out, size_t out_size) {
    size_t o = 0;
    for (size_t i = 0; in != nullptr && in[i] != 0 && o + 1 < out_size; i++) {
        const char c = in[i];
        const char* esc = nullptr;
        switch (c) {
            case '"':  esc = "\\\""; break;
            case '\\': esc = "\\\\"; break;
            case '\n': esc = "\\n";  break;
            case '\r': esc = "\\r";  break;
            case '\t': esc = "\\t";  break;
            default: break;
        }
        if (esc != nullptr) {
            const size_t n = strlen(esc);
            if (o + n + 1 > out_size) break;   // never split an escape
            memcpy(out + o, esc, n);
            o += n;
            continue;
        }
        out[o++] = ((unsigned char)c < 0x20) ? '_' : c;
    }
    out[o] = 0;
}

// in, cut at its last comma if longer than out_size - 1, so no name is passed on partly.
void cutToWholeNames(const char* in, char* out, size_t out_size) {
    const size_t n = in != nullptr ? strlen(in) : 0;
    if (n + 1 <= out_size) {
        memcpy(out, in != nullptr ? in : "", n + 1);
        return;
    }
    size_t cut = 0;
    for (size_t i = out_size - 1; i > 0; i--) {
        if (in[i] == ',') { cut = i; break; }
    }
    memcpy(out, in, cut);
    out[cut] = 0;
}

bool ageKnown(const NeighborsReportInput& in, const NeighbourInfo& r) {
    return in.plausible != nullptr && in.plausible(in.now) && in.plausible(r.heard_timestamp) &&
           r.heard_timestamp <= in.now;
}

// True when a belongs before b in the report.
bool before(const NeighborsReportInput& in, const ScopeResult& a, const ScopeResult& b) {
    const bool ka = ageKnown(in, a.row), kb = ageKnown(in, b.row);
    if (ka != kb) return ka;
    if (a.row.heard_timestamp != b.row.heard_timestamp) return a.row.heard_timestamp > b.row.heard_timestamp;
    if (a.row.snr != b.row.snr) return a.row.snr > b.row.snr;
    return memcmp(a.row.pub_key, b.row.pub_key, kNeighborKeyLen) < 0;
}

// One neighbor entry, without a leading comma. Returns its length, or -1.
int formatEntry(char* out, size_t out_size, const NeighborsReportInput& in, const ScopeResult& r) {
    char key[kNeighborKeyLen * 2 + 1];
    for (unsigned i = 0; i < kNeighborKeyLen; i++) snprintf(key + 2 * i, 3, "%02X", r.row.pub_key[i]);
    char rssi[8];
    if (r.row.rssi == 0) snprintf(rssi, sizeof(rssi), "null");
    else snprintf(rssi, sizeof(rssi), "%d", (int)r.row.rssi);
    char age[16];
    if (ageKnown(in, r.row)) snprintf(age, sizeof(age), "%lu", (unsigned long)(in.now - r.row.heard_timestamp));
    else snprintf(age, sizeof(age), "null");
    char scopes[kScopesLen * 2];
    escapeJson(r.status == ScopeStatus::Responded ? r.scopes : "", scopes, sizeof(scopes));
    const int n = snprintf(out, out_size,
                           "{\"pubkey\":\"%s\",\"snr\":%.2f,\"rssi\":%s,\"heard_secs_ago\":%s,"
                           "\"scopes\":\"%s\",\"status\":\"%s\"}",
                           key, (double)r.row.snr / 4.0, rssi, age, scopes, scopeStatusName(r.status));
    return (n < 0 || (size_t)n >= out_size) ? -1 : n;
}

}  // namespace

// Plain arithmetic rather than gmtime_r, which not every toolchain this builds on
// declares: the proleptic Gregorian date for a count of days since 1970-01-01
// (Howard Hinnant's civil_from_days).
int formatReportTimestamp(uint32_t epoch, char* out, size_t out_size) {
    if (out == nullptr || out_size < 21) return -1;
    const uint32_t days = epoch / 86400u, secs = epoch % 86400u;
    const int64_t z = (int64_t)days + 719468;
    const int64_t era = z / 146097;
    const uint32_t doe = (uint32_t)(z - era * 146097);
    const uint32_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const uint32_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const uint32_t mp = (5 * doy + 2) / 153;
    const uint32_t day = doy - (153 * mp + 2) / 5 + 1;
    const uint32_t month = mp < 10 ? mp + 3 : mp - 9;
    const int64_t year = (int64_t)yoe + era * 400 + (month <= 2 ? 1 : 0);
    const int n = snprintf(out, out_size, "%04d-%02u-%02uT%02u:%02u:%02uZ", (int)year,
                           (unsigned)month, (unsigned)day, (unsigned)(secs / 3600),
                           (unsigned)(secs / 60 % 60), (unsigned)(secs % 60));
    return (n < 0 || (size_t)n >= out_size) ? -1 : n;
}

int buildNeighborsJson(char* buf, size_t buf_size, const NeighborsReportInput& in) {
    if (buf == nullptr || buf_size == 0) return -1;

    // Escaping at most doubles a string; each buffer holds its whole field escaped.
    char origin[80], origin_id[80], ts[40], self_raw[kSelfScopesLen], self_scopes[kSelfScopesLen * 2], def_scope[80];
    escapeJson(in.origin, origin, sizeof(origin));
    escapeJson(in.origin_id, origin_id, sizeof(origin_id));
    escapeJson(in.timestamp, ts, sizeof(ts));
    cutToWholeNames(in.self_scopes, self_raw, sizeof(self_raw));
    escapeJson(self_raw, self_scopes, sizeof(self_scopes));
    escapeJson(in.default_scope, def_scope, sizeof(def_scope));

    int pos = snprintf(buf, buf_size,
                       "{\"timestamp\":\"%s\",\"origin\":\"%s\",\"origin_id\":\"%s\","
                       "\"self\":{\"scopes\":\"%s\",\"default_scope\":\"%s\"},\"neighbors\":[",
                       ts, origin, origin_id, self_scopes, def_scope);
    if (pos < 0 || (size_t)pos >= buf_size) return -1;

    // Rows with an outcome, in report order. A walk is at most MAX_NEIGHBOURS (50)
    // rows; any beyond kMaxRows are still counted and make the report truncated.
    constexpr int kMaxRows = 64;
    int order[kMaxRows];
    int n = 0, queried = 0;
    bool truncated = false;
    for (int i = 0; in.rows != nullptr && i < in.n_rows; i++) {
        if (in.rows[i].status == ScopeStatus::Pending) continue;
        queried++;
        if (n == kMaxRows) {
            truncated = true;
            continue;
        }
        int j = n++;
        while (j > 0 && before(in, in.rows[i], in.rows[order[j - 1]])) {
            order[j] = order[j - 1];
            j--;
        }
        order[j] = i;
    }

    // Room the closing needs whatever happens: ],"total_neighbors":<int>,
    // "queried_neighbors":<int>,"truncated":false} plus NUL.
    const size_t kTail = 96;
    char entry[512];
    for (int k = 0; k < n; k++) {
        const int len = formatEntry(entry, sizeof(entry), in, in.rows[order[k]]);
        const size_t need = (size_t)len + (k > 0 ? 1 : 0);
        if (len < 0 || (size_t)pos + need + kTail > buf_size) {
            truncated = true;
            break;
        }
        if (k > 0) buf[pos++] = ',';
        memcpy(buf + pos, entry, (size_t)len);
        pos += len;
        buf[pos] = 0;
    }

    const int t = snprintf(buf + pos, buf_size - (size_t)pos,
                           "],\"total_neighbors\":%d,\"queried_neighbors\":%d,\"truncated\":%s}",
                           in.total_neighbors, queried, truncated ? "true" : "false");
    if (t < 0 || (size_t)(pos + t) >= buf_size) return -1;
    return pos + t;
}

}  // namespace offband
