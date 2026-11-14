/*
 * captive_portal.h — SoftAP captive portal for the FSD dashboard
 *
 * When a phone/laptop joins the ESP32's SoftAP, the OS probes for internet;
 * finding none it shows a "no internet" warning and silently drops the Wi-Fi
 * after a few seconds, so the dashboard is never reached. This module answers
 * every DNS query with the AP IP and redirects the OS captive-detection probe
 * URLs to the dashboard, so the dashboard is what the OS surfaces instead.
 *
 * The redirect alone does not keep iOS / macOS connected: while their probe
 * keeps failing the network stays "captive", and the captive sheet drops the
 * Wi-Fi as soon as it loses focus (auto-join brings it straight back, so it
 * loops). Once a client has been served the dashboard, Apple's probe from
 * that client is therefore answered with Apple's own Success page — the portal
 * "login" is complete and the device keeps the network. Such a device then
 * sends all its traffic our way, so from that point its requests for other
 * sites get a small 404 with a link to the dashboard instead of the redirect.
 *
 * ACTIVE ONLY IN SOFTAP MODE. In STA mode the device is on the user's real
 * network with real internet — begin() is a no-op there so DNS is never
 * hijacked, and an unmatched request is a plain 404, not a redirect.
 * HTTP/DNS only: no CAN interaction and no TX of any kind.
 */
#ifndef CAPTIVE_PORTAL_H
#define CAPTIVE_PORTAL_H

#include <WebServer.h>

// Start the DNS catch-all if (and only if) the Wi-Fi is in AP/AP+STA mode.
void captive_portal_begin();

// Service one pending DNS request. Cheap no-op unless the portal is active.
void captive_portal_process();

// True once begin() has armed the portal (i.e. we are in SoftAP mode).
bool captive_portal_active();

// Register the OS captive-detection probe routes + the catch-all onNotFound
// handler on the given HTTP server. web_dashboard owns the server and calls
// this; it does NOT register its own onNotFound, so there is no double-bind.
// Also makes the server collect User-Agent. collectHeaders() replaces the whole
// list (WebServer always keeps Authorization), so a later call on this server
// must name User-Agent too, or Apple's probe is only recognised by path.
void captive_portal_register_routes(WebServer &http);

// The request being handled is a dashboard page load: remember its client so
// Apple's probe from it gets Success from now on and its stray requests stop
// being redirected. Call from the root handler. No-op unless the portal is
// active.
void captive_portal_note_dashboard(WebServer &http);

#endif // CAPTIVE_PORTAL_H
