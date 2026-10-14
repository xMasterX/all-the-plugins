/*
 * captive_probe.h — captive-portal decision logic (no Arduino dependencies)
 *
 * Which clients have loaded the dashboard, which requests are Apple's
 * connectivity probe, and what a request gets back. Plain C++ over uint32_t /
 * const char* so it is unit tested on the host (test/test_captive_probe.cpp);
 * captive_portal.cpp is the Arduino glue around it.
 */
#ifndef CAPTIVE_PROBE_H
#define CAPTIVE_PROBE_H

#include <stdint.h>
#include <string.h>

// SoftAP takes 4 stations; 8 leaves room for address churn.
static constexpr uint8_t CAPTIVE_MAX_CLIENTS = 8;

// Byte-for-byte what http://captive.apple.com/hotspot-detect.html serves
// (trailing LF included). iOS / macOS read anything else as "still captive".
static constexpr char CAPTIVE_APPLE_SUCCESS_BODY[] =
    "<HTML><HEAD><TITLE>Success</TITLE></HEAD><BODY>Success</BODY></HTML>\n";

enum CaptiveReply {
    CAPTIVE_REPLY_REDIRECT,       // 302 to the dashboard
    CAPTIVE_REPLY_APPLE_SUCCESS,  // 200 + CAPTIVE_APPLE_SUCCESS_BODY
    CAPTIVE_REPLY_NOT_FOUND,      // small 404
    CAPTIVE_REPLY_DASHBOARD_LINK, // small 404 with a plain link to the dashboard
};

// CaptiveNetworkSupport is the iOS / macOS connectivity checker, e.g.
// "CaptiveNetworkSupport-355.200.27 wispr". It also probes hosts and paths
// other than the two below, where the UA is the only tell.
inline bool captive_ua_is_apple_probe(const char *ua) {
    return ua && strstr(ua, "CaptiveNetworkSupport") != nullptr;
}

inline bool captive_is_apple_probe(const char *path, const char *ua) {
    if (path && (strcmp(path, "/hotspot-detect.html") == 0 ||
                 strcmp(path, "/library/test/success.html") == 0)) {
        return true;
    }
    return captive_ua_is_apple_probe(ua);
}

// IPv4 addresses that have been served the dashboard since boot. Fixed size,
// RAM only; once full a new client overwrites the oldest. 0 = empty slot.
class CaptiveClients {
    uint32_t ips_[CAPTIVE_MAX_CLIENTS] = {};
    uint8_t next_ = 0;

public:
    bool known(uint32_t ip) const {
        if (ip == 0) return false;
        for (uint8_t i = 0; i < CAPTIVE_MAX_CLIENTS; i++) {
            if (ips_[i] == ip) return true;
        }
        return false;
    }

    // Record a dashboard load. Ignored while the portal is inactive (STA mode)
    // and when the connectivity checker itself fetched the page — that is not
    // the user seeing it.
    void note(bool portal_active, uint32_t ip, const char *ua) {
        if (!portal_active || ip == 0 || captive_ua_is_apple_probe(ua) || known(ip)) return;
        ips_[next_] = ip;
        next_ = (uint8_t)((next_ + 1) % CAPTIVE_MAX_CLIENTS);
    }
};

// Explicit Apple probe routes: Success only for a client that has loaded the
// dashboard, otherwise the redirect that makes the OS show it.
inline CaptiveReply captive_probe_reply(const CaptiveClients &clients, uint32_t ip,
                                        const char *path, const char *ua) {
    if (captive_is_apple_probe(path, ua) && clients.known(ip)) {
        return CAPTIVE_REPLY_APPLE_SUCCESS;
    }
    return CAPTIVE_REPLY_REDIRECT;
}

// True when the request was addressed to the portal itself rather than pulled
// in by the DNS catch-all: Host is the AP address (bare or with :80), or absent.
// Whitespace around the value is skipped (legal HTTP, and WebServer leaves it
// in on GET); hosts compare case-insensitively.
inline bool captive_host_is_ours(const char *host, const char *ap_ip) {
    auto blank = [](char c) { return c == ' ' || c == '\t'; };
    auto lower = [](char c) { return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c; };

    if (!host) return true;
    while (blank(*host)) host++;
    if (!*host) return true;
    if (!ap_ip || !*ap_ip) return false;
    for (; *ap_ip; host++, ap_ip++) {
        if (lower(*host) != lower(*ap_ip)) return false;
    }
    if (strncmp(host, ":80", 3) == 0) host += 3;
    while (blank(*host)) host++;
    return *host == '\0';
}

// Catch-all, for requests no route matched.
//   portal inactive              -> 404 (STA mode: no lure to run)
//   Apple probe, known client    -> Success
//   Host is the portal itself    -> 404 (a missing path on our own host)
//   other Host, known client     -> 404 + link
//   other Host, unknown client   -> redirect (the captive lure, every OS)
// Once a client has loaded the dashboard its device may route everything
// through us. Redirecting that stray traffic would make it fetch the whole
// page each time; a link serves a person who typed some hostname without
// being followed automatically.
inline CaptiveReply captive_not_found_reply(const CaptiveClients &clients, bool portal_active,
                                            uint32_t ip, const char *path, const char *ua,
                                            const char *host, const char *ap_ip) {
    if (!portal_active) return CAPTIVE_REPLY_NOT_FOUND;
    bool known = clients.known(ip);
    if (known && captive_is_apple_probe(path, ua)) return CAPTIVE_REPLY_APPLE_SUCCESS;
    if (captive_host_is_ours(host, ap_ip)) return CAPTIVE_REPLY_NOT_FOUND;
    return known ? CAPTIVE_REPLY_DASHBOARD_LINK : CAPTIVE_REPLY_REDIRECT;
}

#endif // CAPTIVE_PROBE_H
