/*
 * captive_portal.cpp — see captive_portal.h
 *
 * Standard ESP32 captive-portal pattern: a DNSServer that resolves every
 * hostname to the SoftAP IP, plus HTTP handlers that 302-redirect the OS
 * captive-detection probe URLs (and requests for any other site) to the
 * dashboard root, so the OS pops the dashboard as the captive page.
 *
 * Apple's probe is the exception once its client has loaded the dashboard: it
 * then gets Apple's Success page, which ends the captive state so iOS / macOS
 * stop dropping the Wi-Fi when the captive sheet loses focus (#219). That
 * client's other stray requests then get a small 404 with a link rather than
 * the redirect. The decision logic is in captive_probe.h (host-tested); this
 * is the Arduino glue.
 */
#include "captive_portal.h"
#include "captive_probe.h"

#include <DNSServer.h>
#include <WiFi.h>
#include <Arduino.h>
#include <new>

static DNSServer     *g_dns = nullptr;   // created by begin(), SoftAP only
static bool           g_active = false;
static CaptiveClients g_clients;   // clients that have loaded the dashboard

static bool is_ap_mode() {
    wifi_mode_t mode = WiFi.getMode();
    return (mode == WIFI_MODE_AP || mode == WIFI_MODE_APSTA);
}

// URL of the dashboard root on the SoftAP, e.g. "http://192.168.4.1/".
static String root_url() {
    return String("http://") + WiFi.softAPIP().toString() + "/";
}

// Send the client to the dashboard root so the OS captive sheet shows it.
static void redirect_to_root(WebServer &http) {
    http.sendHeader("Location", root_url(), true);
    http.send(302, "text/plain", "");
}

static uint32_t client_ip(WebServer &http) {
    return (uint32_t)http.client().remoteIP();
}

static void send_reply(WebServer &http, CaptiveReply reply) {
    switch (reply) {
    case CAPTIVE_REPLY_APPLE_SUCCESS:
        http.send(200, "text/html", CAPTIVE_APPLE_SUCCESS_BODY);
        break;
    case CAPTIVE_REPLY_NOT_FOUND:
        http.send(404, "text/plain", "Not found");
        break;
    case CAPTIVE_REPLY_DASHBOARD_LINK: {
        // A plain link and nothing a client follows on its own (no Location,
        // refresh or script): a person gets to the dashboard, background
        // traffic does not pull it.
        String url = root_url();
        http.send(404, "text/html", String("<a href=\"") + url + "\">" + url + "</a>");
        break;
    }
    default:
        redirect_to_root(http);
        break;
    }
}

// Apple's probe from a client that has loaded the dashboard gets Success;
// from anyone else it is redirected as before.
static void answer_apple_probe(WebServer &http) {
    String ua = http.header("User-Agent");
    send_reply(http, captive_probe_reply(g_clients, client_ip(http),
                                         http.uri().c_str(), ua.c_str()));
}

// No route matched: redirect (the captive lure), Apple Success, or a small 404
// with or without a link to the dashboard — see captive_not_found_reply().
static void answer_not_found(WebServer &http) {
    String ua   = http.header("User-Agent");
    String host = http.hostHeader();
    String ap   = WiFi.softAPIP().toString();
    send_reply(http, captive_not_found_reply(g_clients, g_active, client_ip(http),
                                             http.uri().c_str(), ua.c_str(),
                                             host.c_str(), ap.c_str()));
}

void captive_portal_begin() {
    if (g_active) g_dns->stop();   // called again: release the socket first
    g_active = false;
    if (!is_ap_mode()) {
        // STA mode: on the user's real network — never hijack DNS here.
        return;
    }

    // Heap, not a static: classic-ESP32 static DRAM is nearly full (esp32-lilygo).
    if (g_dns == nullptr) g_dns = new (std::nothrow) DNSServer();
    if (g_dns == nullptr) {
        Serial.println("[Captive] DNS server alloc failed, portal off");
        return;
    }

    // Reply with the AP IP for every query (catch-all "*" domain).
    g_dns->setErrorReplyCode(DNSReplyCode::NoError);
    if (g_dns->start(53, "*", WiFi.softAPIP())) {
        g_active = true;
        Serial.printf("[Captive] DNS catch-all → %s\n",
                      WiFi.softAPIP().toString().c_str());
    } else {
        Serial.println("[Captive] DNS server failed to start");
    }
}

void captive_portal_process() {
    if (!g_active) return;
    g_dns->processNextRequest();
}

bool captive_portal_active() {
    return g_active;
}

void captive_portal_note_dashboard(WebServer &http) {
    String ua = http.header("User-Agent");
    g_clients.note(g_active, client_ip(http), ua.c_str());
}

void captive_portal_register_routes(WebServer &http) {
    // WebServer only keeps the request headers it is told to collect. This
    // replaces the list, but Authorization is always kept, so admin auth is
    // unaffected.
    const char *collect[] = {"User-Agent"};
    http.collectHeaders(collect, 1);

    // OS captive-detection probe URLs. Each OS fetches a known URL expecting a
    // specific "you have internet" response; we instead 302 them to the
    // dashboard, which makes the OS open the captive page on the dashboard.
    //
    // Only Apple's probe is ever answered with its "you have internet" page
    // (see answer_apple_probe): that is the platform with the documented drop
    // loop. Android / Windows / Firefox keep the plain redirect — no reports
    // against them, and Android also validates over HTTPS, which we can't answer.
    WebServer *h = &http;
    auto redirect  = [h]() { redirect_to_root(*h); };
    auto apple     = [h]() { answer_apple_probe(*h); };
    auto not_found = [h]() { answer_not_found(*h); };

    // Android
    http.on("/generate_204", HTTP_GET, redirect);
    http.on("/gen_204",      HTTP_GET, redirect);
    // Apple (iOS / macOS)
    http.on("/hotspot-detect.html",           HTTP_GET, apple);
    http.on("/library/test/success.html",     HTTP_GET, apple);
    // Windows
    http.on("/ncsi.txt",     HTTP_GET, redirect);
    http.on("/connecttest.txt", HTTP_GET, redirect);
    http.on("/redirect",     HTTP_GET, redirect);
    // Firefox
    http.on("/success.txt",   HTTP_GET, redirect);
    http.on("/canonical.html", HTTP_GET, redirect);

    // Catch-all: a client that has not loaded the dashboard yet and asks for
    // any other site (the OS often probes an arbitrary domain, which our DNS
    // points here) is driven to the dashboard too. Apple's probe is recognised
    // here by User-Agent. Everything else gets a small 404: a missing path on
    // the portal itself, and (with a link to the dashboard) the stray traffic
    // of a client that has already loaded it — its device now routes
    // everything through us, and a redirect would have it fetch the whole page
    // each time. With the portal off (STA mode) nothing is redirected at all.
    // web_dashboard deliberately does not register its own onNotFound so this
    // is the only one.
    http.onNotFound(not_found);
}
