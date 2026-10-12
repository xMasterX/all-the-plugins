/*
 * captive_portal.cpp — see captive_portal.h
 *
 * Standard ESP32 captive-portal pattern: a DNSServer that resolves every
 * hostname to the SoftAP IP, plus HTTP handlers that 302-redirect the OS
 * captive-detection probe URLs (and any unknown path) to the dashboard root.
 * This makes iOS/macOS/Android/Windows keep the Wi-Fi connected and pop the
 * dashboard as the captive page.
 */
#include "captive_portal.h"

#include <DNSServer.h>
#include <WiFi.h>
#include <Arduino.h>

static DNSServer g_dns;
static bool      g_active = false;

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

void captive_portal_begin() {
    g_active = false;
    if (!is_ap_mode()) {
        // STA mode: on the user's real network — never hijack DNS here.
        return;
    }

    // Reply with the AP IP for every query (catch-all "*" domain).
    g_dns.setErrorReplyCode(DNSReplyCode::NoError);
    if (g_dns.start(53, "*", WiFi.softAPIP())) {
        g_active = true;
        Serial.printf("[Captive] DNS catch-all → %s\n",
                      WiFi.softAPIP().toString().c_str());
    } else {
        Serial.println("[Captive] DNS server failed to start");
    }
}

void captive_portal_process() {
    if (!g_active) return;
    g_dns.processNextRequest();
}

bool captive_portal_active() {
    return g_active;
}

void captive_portal_register_routes(WebServer &http) {
    // OS captive-detection probe URLs. Each OS fetches a known URL expecting a
    // specific "you have internet" response; we instead 302 them to the
    // dashboard, which makes the OS open the captive page on the dashboard.
    WebServer *h = &http;
    auto redirect = [h]() { redirect_to_root(*h); };

    // Android
    http.on("/generate_204", HTTP_GET, redirect);
    http.on("/gen_204",      HTTP_GET, redirect);
    // Apple (iOS / macOS)
    http.on("/hotspot-detect.html",           HTTP_GET, redirect);
    http.on("/library/test/success.html",     HTTP_GET, redirect);
    // Windows
    http.on("/ncsi.txt",     HTTP_GET, redirect);
    http.on("/connecttest.txt", HTTP_GET, redirect);
    http.on("/redirect",     HTTP_GET, redirect);
    // Firefox
    http.on("/success.txt",   HTTP_GET, redirect);
    http.on("/canonical.html", HTTP_GET, redirect);

    // Catch-all: any other host/path (the OS often probes an arbitrary domain,
    // which our DNS points here) is driven to the dashboard too. web_dashboard
    // deliberately does not register its own onNotFound so this is the only one.
    http.onNotFound(redirect);
}
