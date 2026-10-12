/*
 * captive_portal.h — SoftAP captive portal for the FSD dashboard
 *
 * When a phone/laptop joins the ESP32's SoftAP, the OS probes for internet;
 * finding none it shows a "no internet" warning and silently drops the Wi-Fi
 * after a few seconds, so the dashboard is never reached. This module answers
 * every DNS query with the AP IP and handles the OS captive-detection probe
 * URLs so the dashboard page is what the OS surfaces instead.
 *
 * ACTIVE ONLY IN SOFTAP MODE. In STA mode the device is on the user's real
 * network with real internet — begin() is a no-op there so DNS is never
 * hijacked. Redirect only: no CAN interaction and no TX of any kind.
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
void captive_portal_register_routes(WebServer &http);

#endif // CAPTIVE_PORTAL_H
