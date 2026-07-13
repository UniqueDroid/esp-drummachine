#pragma once

// On-demand WiFi: off by default (no first-run setup forced), toggled via
// a UI icon. First toggle-on with no saved credentials opens a WiFiManager
// AP + captive portal to enter WiFi details; once connected (then or on
// later toggles, using saved credentials), a small web menu becomes
// reachable for uploading sound sets, managing saved patterns, and
// installing firmware updates. Toggling off disconnects and turns the
// radio back off, so normal play doesn't share CPU/timing with WiFi.
void wifiPortalInit();
void wifiPortalToggle();
// Call every main-loop iteration; a cheap no-op while WiFi is off.
void wifiPortalLoop();

enum class WifiPortalState { Off, Portal, Connecting, Connected };
WifiPortalState wifiPortalGetState();

// Human-readable status for the UI (IP address once connected, AP name
// while in portal/setup mode, etc).
const char *wifiPortalStatusText();
