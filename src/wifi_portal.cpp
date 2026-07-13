#include "wifi_portal.h"

#include <SD.h>
#include <WiFiManager.h>

#include <vector>

#include "assets/project_logo_png.h"
#include "audio_engine.h"
#include "config_manager.h"
#include "firmware_update.h"
#include "firmware_version.h"
#include "globals.h"
#include "pattern_storage.h"
#include "sd_bus.h"
#include "sequencer_ui.h"
#include "sound_sets.h"

namespace {

constexpr const char *AP_NAME = "ESP-DrumMachine";
constexpr const char *AP_PASSWORD = "drummachine";
constexpr uint32_t STA_CONNECT_TIMEOUT_MS = 15000;

WiFiManager wm;
WifiPortalState state = WifiPortalState::Off;
uint32_t connectStartMs = 0;
bool routesRegistered = false;
char statusText[64] = "WiFi off";

File uploadFile;
const char *TRACK_UPLOAD_NAMES[NUM_TRACKS] = {
    "kick.wav", "snare.wav", "hihat.wav", "tom.wav", "clap.wav", "cymbal.wav",
};

// WiFiManager stores the pointers it's given here, not copies - backing
// storage MUST outlive the call, so this is file-scope, not a local. A local
// String/param passed to setCustomMenuHTML()/addParameter() went out of
// scope right after the function returned, leaving WiFiManager holding a
// dangling pointer - that's what crashed with LoadProhibited right after
// connecting (see the postmortem further down).
WiFiManagerParameter *menuPasswordParam = nullptr;

// Themes WiFiManager's own native pages (wifi scan/connect, info, param) -
// same technique as pfsense-status-esp32's config_portal.cpp, generic enough
// to reuse as-is.
const char kPortalHeadElement[] PROGMEM = R"HTML(
<style>
.wm-brand{display:flex;justify-content:center;align-items:center;min-height:64px;margin:6px 0 8px 0}
.wm-brand-logo{display:block;max-width:min(100%,320px);height:auto;margin:0 auto}
body{background:radial-gradient(ellipse 900px 500px at 50% -10%,#0d2338,transparent) #070c13;color:#e7f3fb;font-family:-apple-system,system-ui,'Segoe UI',Roboto,sans-serif}
h1,h2,h3{color:#e7f3fb}
a{color:#4fc3f7}
a:hover{color:#7dd8fb}
input,select{background:#101823;border:1px solid #22303f;color:#e7f3fb;border-radius:10px;margin:8px 0}
button,input[type='button'],input[type='submit']{background:linear-gradient(135deg,#4fc3f7,#0f6fa8);color:#0d1015;font-weight:600;border-radius:999px;margin:6px 0}
button.D{background:#dc3630;color:#fff}
.msg{background:#101823;border:1px solid #22303f;border-left-width:5px;border-radius:10px;color:#a9bcca}
hr{border:none;border-top:1px solid #22303f;margin:18px 0}
</style>
<script>
(function(){
  function ensurePortalLogo(){
    var body=document.body;
    if(!body||body.classList.contains('wm-logo-ready')) return;
    body.classList.add('wm-logo-ready');
    var brand=document.createElement('div');
    brand.className='wm-brand';
    var img=document.createElement('img');
    img.className='wm-brand-logo';
    img.alt='ESP DrumMachine';
    img.src='/project-logo.png';
    img.onerror=function(){this.style.display='none';};
    brand.appendChild(img);
    var heading=body.querySelector('h1, h2, h3');
    if(heading){
      heading.style.display='none';
      heading.parentNode.insertBefore(brand, heading);
      return;
    }
    var wrap=body.querySelector('.wrap');
    if(wrap){ wrap.insertBefore(brand, wrap.firstChild); return; }
    body.insertBefore(brand, body.firstChild);
  }
  if(document.readyState==='loading'){
    document.addEventListener('DOMContentLoaded', ensurePortalLogo);
  } else {
    ensurePortalLogo();
  }
})();
</script>
)HTML";

// Full theme for our own custom-built pages (device settings, firmware
// update, sound sets, patterns) - same cyan/blue circuit-ring theme as
// pfsense-status-esp32's buildFirmwareUpdateStyles(), reused verbatim.
String buildPortalStyles() {
  String css;
  css.reserve(2200);
  css += ":root{--accent:#4fc3f7;--accent-2:#0f6fa8;--spark:#ff8f3d;--bg:#070c13;--bg-grad:#0d2338;--surface:#101823;--border:#22303f;--text:#e7f3fb;--text-mute:#a9bcca;--warn:#f5a93c;--ok:#4ade80;--bad:#dc3630}";
  css += ".c,body{text-align:center;font-family:-apple-system,system-ui,'Segoe UI',Roboto,sans-serif;background:radial-gradient(ellipse 900px 500px at 50% -10%,var(--bg-grad),transparent) var(--bg);color:var(--text)}div,input,select{padding:5px;font-size:1em;margin:5px 0;box-sizing:border-box}";
  css += "input,select{border-radius:10px;width:100%;background:var(--surface);border:1px solid var(--border);color:var(--text);margin:8px 0}";
  css += "button{border-radius:999px;width:100%;cursor:pointer;border:0;background:linear-gradient(135deg,var(--accent),var(--accent-2));color:#0d1015;font-weight:600;line-height:2.4rem;font-size:1.2rem;margin:6px 0}";
  css += "button.D{background:var(--bad);color:#fff}.wrap{text-align:left;display:inline-block;min-width:260px;max-width:500px}";
  css += "hr{border:none;border-top:1px solid var(--border);margin:18px 0}";
  css += ".brand{display:flex;justify-content:center;align-items:center;min-height:64px;margin:6px 0 4px 0}.brand-logo{display:block;max-width:min(100%,320px);height:auto;margin:0 auto}.brand-title{margin:0;line-height:1.1;color:var(--text)}body.haslogo .brand-title{display:none}";
  css += "a{color:var(--accent);font-weight:700;text-decoration:none}a:hover{color:#7dd8fb;text-decoration:underline}";
  css += ".msg{padding:20px;margin:20px 0;border:1px solid var(--border);border-radius:10px;border-left-width:5px;border-left-color:#777;background:var(--surface);color:var(--text-mute)}";
  css += ".msg.S{border-left-color:var(--ok)}.msg.D{border-left-color:var(--bad)}.msg.P{border-left-color:var(--accent)}";
  css += "dt{font-weight:bold;color:var(--text)}dd{margin:0;padding:0 0 .5em 0;min-height:12px}";
  css += "label{display:block;font-weight:bold;margin-top:10px;color:var(--text)}";
  css += ".progress-shell{margin:18px 0 14px 0}.progress-track{width:100%;height:14px;background:var(--surface);border-radius:999px;overflow:hidden;border:1px solid var(--border);padding:0 !important;margin:0 !important}.progress-bar{height:100%;width:0%;background:linear-gradient(90deg,var(--accent),var(--accent-2));border-radius:999px;transition:width .2s ease;padding:0 !important;margin:0 !important;display:block}.progress-shell.fail .progress-bar{background:var(--bad)}.progress-text{font-size:.95em;color:var(--text-mute);padding:0;margin:8px 0 0 0}";
  return css;
}

String escapeHtml(String text) {
  text.replace("&", "&amp;");
  text.replace("<", "&lt;");
  text.replace(">", "&gt;");
  text.replace("\"", "&quot;");
  text.replace("'", "&#39;");
  return text;
}

String escapeJson(String text) {
  text.replace("\\", "\\\\");
  text.replace("\"", "\\\"");
  text.replace("\n", "\\n");
  text.replace("\r", "");
  return text;
}

String formatReleaseBody(String body) {
  body.replace("\r", "");
  body.replace("\n", "<br/>");
  if (body.length() > 900) {
    body = body.substring(0, 900) + "<br/>...";
  }
  return body;
}

String buildPortalHeaderHtml(const String &subtitle) {
  String html;
  html.reserve(280);
  html += "<div class='brand'><img class='brand-logo' src='/project-logo.png' alt='ESP DrumMachine' onload=\"document.body.classList.add('haslogo')\" onerror=\"this.style.display='none'\"><h1 class='brand-title'>ESP DrumMachine</h1></div><h3>";
  html += escapeHtml(subtitle);
  html += "</h3>";
  return html;
}

String htmlPage(const String &title, const String &body) {
  String html = "<!DOCTYPE html><html><head><meta charset='utf-8'>";
  html += "<meta name='viewport' content='width=device-width,initial-scale=1'>";
  html += "<title>";
  html += title;
  html += "</title><style>";
  html += buildPortalStyles();
  html += "</style></head><body><div class='wrap'>";
  html += buildPortalHeaderHtml(title);
  html += body;
  html += "<hr><a class='btn' href='/'>&laquo; Menu</a></div></body></html>";
  return html;
}

// Custom routes bypass WiFiManager's own page templates, so they don't get
// its login gate (and this cached WiFiManager version's handleRequest() is a
// hardcoded no-op anyway - it never actually checks credentials). Applied
// explicitly here instead, straight against the underlying WebServer.
bool checkMenuAuth() {
  const DeviceConfig &cfg = ConfigManager::getInstance().getConfig();
  if (strlen(cfg.web_menu_password) < 8) {
    return true;  // no password configured, menu stays open
  }
  if (wm.server->authenticate("admin", cfg.web_menu_password)) {
    return true;
  }
  wm.server->requestAuthentication(HTTPAuthMethod::BASIC_AUTH);
  return false;
}

void handleLogo() {
  wm.server->sendHeader("Cache-Control", "public, max-age=86400");
  wm.server->setContentLength(kProjectLogoPngLen);
  wm.server->send(200, "image/png", "");
  WiFiClient client = wm.server->client();
  client.write(kProjectLogoPng, kProjectLogoPngLen);
}

void handleSounds() {
  if (!checkMenuAuth()) return;

  char names[8][32];
  beginSdSpi();
  int count = scanSoundSets(names, 8);
  sequencerUiRestoreTouchSpi();

  String body = "<p>Current sets on SD:</p><ul>";
  for (int i = 0; i < count; i++) {
    body += "<li>" + String(names[i]) + "</li>";
  }
  body += "</ul><h3>Upload a sample</h3>";
  body += "<form method='POST' action='/sounds/upload' enctype='multipart/form-data'>";
  body += "Set name: <input name='set' value='Set1'><br>";
  body += "Track: <select name='track'>";
  for (int t = 0; t < NUM_TRACKS; t++) {
    body += "<option value='" + String(t) + "'>" + String(TRACK_UPLOAD_NAMES[t]) + "</option>";
  }
  body += "</select><br>";
  body += "WAV file (8-bit mono PCM): <input type='file' name='file' accept='.wav'><br>";
  body += "<button type='submit'>Upload</button></form>";
  body += "<p>One file at a time. After uploading a full set, open it from the "
          "device's Settings screen, or set it as default below, to load it.</p>";
  wm.server->send(200, "text/html", htmlPage("Sound Sets", body));
}

void handleSoundUploadFile() {
  if (!checkMenuAuth()) return;

  HTTPUpload &upload = wm.server->upload();
  if (upload.status == UPLOAD_FILE_START) {
    beginSdSpi();
    String setName = wm.server->arg("set");
    if (setName.length() == 0) setName = "Set1";
    int trackIndex = wm.server->arg("track").toInt();
    if (trackIndex < 0 || trackIndex >= NUM_TRACKS) trackIndex = 0;

    String dir = "/Sounds/" + setName;
    if (!SD.exists("/Sounds")) SD.mkdir("/Sounds");
    if (!SD.exists(dir)) SD.mkdir(dir);
    String path = dir + "/" + TRACK_UPLOAD_NAMES[trackIndex];
    uploadFile = SD.open(path, FILE_WRITE);
    Serial.printf("wifi upload: writing %s\n", path.c_str());
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (uploadFile) uploadFile.write(upload.buf, upload.currentSize);
  } else if (upload.status == UPLOAD_FILE_END) {
    if (uploadFile) uploadFile.close();
    Serial.printf("wifi upload: done, %u bytes\n", (unsigned)upload.totalSize);
  }
}

void handleSoundUploadDone() {
  if (!checkMenuAuth()) return;
  sequencerUiRestoreTouchSpi();
  wm.server->sendHeader("Location", "/sounds");
  wm.server->send(303);
}

void handlePatterns() {
  if (!checkMenuAuth()) return;

  char names[32][32];
  beginSdSpi();
  int count = scanPatternFiles(names, 32);
  sequencerUiRestoreTouchSpi();

  String body = "<ul>";
  for (int i = 0; i < count; i++) {
    body += "<li>" + String(names[i]) + " - <a href='/patterns/download?name=" + String(names[i]) +
            "'>download</a> - <a href='/patterns/delete?name=" + String(names[i]) +
            "' onclick=\"return confirm('Delete " + String(names[i]) + "?')\">delete</a></li>";
  }
  body += "</ul>";
  wm.server->send(200, "text/html", htmlPage("Saved Patterns", body));
}

void handlePatternDownload() {
  if (!checkMenuAuth()) return;

  String name = wm.server->arg("name");
  String path = String(PATTERN_DIR) + "/" + name;
  beginSdSpi();
  File f = SD.open(path, FILE_READ);
  if (!f) {
    sequencerUiRestoreTouchSpi();
    wm.server->send(404, "text/plain", "Not found");
    return;
  }
  wm.server->streamFile(f, "application/json");
  f.close();
  sequencerUiRestoreTouchSpi();
}

void handlePatternDelete() {
  if (!checkMenuAuth()) return;

  String name = wm.server->arg("name");
  String path = String(PATTERN_DIR) + "/" + name;
  beginSdSpi();
  SD.remove(path);
  sequencerUiRestoreTouchSpi();
  wm.server->sendHeader("Location", "/patterns");
  wm.server->send(303);
}

String buildDeviceSettingsPage(const String &notice = String(), bool noticeSuccess = false) {
  const DeviceConfig &cfg = ConfigManager::getInstance().getConfig();

  char names[8][32];
  beginSdSpi();
  int count = scanSoundSets(names, 8);
  sequencerUiRestoreTouchSpi();

  String body;
  if (notice.length() > 0) {
    body += "<div class='msg ";
    body += noticeSuccess ? "S'" : "D'";
    body += "><strong>";
    body += noticeSuccess ? "Status" : "Notice";
    body += "</strong><br/>";
    body += escapeHtml(notice);
    body += "</div>";
  }

  body += "<p>Defaults applied on boot (and applied to the running sequencer right away when saved here).</p>";
  body += "<form method='POST' action='/device-settings'>";
  body += "<label for='bpm'>Tempo (60-200 BPM)</label><input id='bpm' type='number' min='60' max='200' name='bpm' value='";
  body += String(cfg.default_bpm);
  body += "'>";
  body += "<label for='swing'>Swing (0-100%)</label><input id='swing' type='number' min='0' max='100' name='swing' value='";
  body += String(cfg.default_swing);
  body += "'>";
  body += "<label for='volume'>Master Volume (0-100%)</label><input id='volume' type='number' min='0' max='100' name='volume' value='";
  body += String(cfg.default_volume);
  body += "'>";
  body += "<label for='sound_set'>Default Sound Set</label><select id='sound_set' name='sound_set'>";
  bool haveCurrent = false;
  for (int i = 0; i < count; i++) {
    bool selected = strcmp(names[i], cfg.default_sound_set) == 0;
    if (selected) haveCurrent = true;
    body += "<option value='" + String(names[i]) + "'";
    if (selected) body += " selected";
    body += ">" + String(names[i]) + "</option>";
  }
  if (!haveCurrent && strlen(cfg.default_sound_set) > 0) {
    body += "<option value='" + String(cfg.default_sound_set) + "' selected>" + String(cfg.default_sound_set) + " (not on SD)</option>";
  }
  body += "</select>";
  body += "<button type='submit' style='margin-top:14px'>Save & Apply</button></form>";

  return htmlPage("Device Settings", body);
}

void handleDeviceSettings() {
  if (!checkMenuAuth()) return;
  wm.server->send(200, "text/html", buildDeviceSettingsPage());
}

void handleDeviceSettingsSave() {
  if (!checkMenuAuth()) return;

  ConfigManager &cfg = ConfigManager::getInstance();
  cfg.setDefaultBpm(wm.server->arg("bpm").toInt());
  cfg.setDefaultSwing(wm.server->arg("swing").toInt());
  cfg.setDefaultVolume(wm.server->arg("volume").toInt());
  String setName = wm.server->arg("sound_set");
  if (setName.length() > 0) {
    cfg.setDefaultSoundSet(setName.c_str());
  }
  cfg.saveConfig();  // persists + applies bpm/swing/volume to the live sequencer

  if (setName.length() > 0) {
    beginSdSpi();
    audioEngineLoadSampleSet(setName.c_str());
    sequencerUiRestoreTouchSpi();
  }
  sequencerUiRefreshFromState();

  wm.server->send(200, "text/html", buildDeviceSettingsPage("Settings saved and applied.", true));
}

void appendFirmwareReleaseInfo(String &html, const FirmwareReleaseInfo &info) {
  html += "<h3>Release Information</h3><hr><dl>";
  html += "<dt>Current</dt><dd>";
  html += escapeHtml(info.currentVersion);
  html += "</dd>";
  html += "<dt>Latest</dt><dd>";
  html += escapeHtml(info.latestVersion.length() ? info.latestVersion : String("unknown"));
  html += "</dd>";
  html += "<dt>Release</dt><dd>";
  html += escapeHtml(info.releaseName.length() ? info.releaseName : String("-"));
  html += "</dd>";
  html += "<dt>Asset</dt><dd>";
  html += escapeHtml(info.assetName.length() ? info.assetName : String("-"));
  html += "</dd>";
  html += "<dt>Published</dt><dd>";
  html += escapeHtml(info.publishedAt.length() ? info.publishedAt : String("-"));
  html += "</dd></dl>";
}

String buildFirmwareInstallPageStart(const FirmwareReleaseInfo &info) {
  String html;
  html.reserve(7000);
  html += "<!DOCTYPE html><html lang='en'><head>";
  html += "<meta charset='UTF-8'><meta name='viewport' content='width=device-width,initial-scale=1,user-scalable=no'/>";
  html += "<title>Firmware Update</title><style>";
  html += buildPortalStyles();
  html += "</style></head><body><div class='wrap'>";
  html += buildPortalHeaderHtml("Firmware Update");
  html += "<div id='installProgress' class='progress-shell'><div class='progress-track'><div id='installBar' class='progress-bar'></div></div><p id='installStatus' class='progress-text'>Starting firmware download...</p></div>";
  html += "<div class='msg P'><strong>Current firmware:</strong> ";
  html += escapeHtml(kFirmwareVersion);
  html += "</div>";
  appendFirmwareReleaseInfo(html, info);
  if (info.releaseBody.length() > 0) {
    html += "<div class='msg'><strong>Release Notes</strong><br/>";
    html += formatReleaseBody(escapeHtml(info.releaseBody));
    html += "</div>";
  }
  html += "<script>function setFwProgress(percent,message,failed){var bar=document.getElementById('installBar');var shell=document.getElementById('installProgress');var status=document.getElementById('installStatus');if(bar){bar.style.width=percent+'%';}if(status){status.innerHTML=message;}if(shell){shell.className=failed?'progress-shell fail':'progress-shell';}}</script>";
  return html;
}

String buildFirmwareUpdatePage(const FirmwareReleaseInfo &info, const String &message = String(), bool success = false) {
  String html;
  html.reserve(9500);
  html += "<!DOCTYPE html><html lang='en'><head>";
  html += "<meta charset='UTF-8'><meta name='viewport' content='width=device-width,initial-scale=1,user-scalable=no'/>";
  html += "<title>Firmware Update</title>";
  html += "<style>";
  html += buildPortalStyles();
  html += "#installProgress{display:none}</style></head><body><div class='wrap'>";
  html += buildPortalHeaderHtml("Firmware Update");
  html += "<script>function startFirmwareInstall(){if(!confirm('Download and install this release now?'))return false;var buttons=document.querySelectorAll('.fw-install-btn');for(var i=0;i<buttons.length;i++){buttons[i].disabled=true;}window.location='/firmware-update/install';return false;}</script>";

  if (message.length() > 0) {
    html += "<div class='msg ";
    html += success ? "S'" : "D'";
    html += "><strong>";
    html += success ? "Status" : "Notice";
    html += "</strong><br/>";
    html += escapeHtml(message);
    html += "</div>";
  }

  html += "<div class='msg P'><strong>Current firmware:</strong> ";
  html += escapeHtml(kFirmwareVersion);
  html += "</div>";
  html += "<div id='installProgress' class='progress-shell'><div class='progress-track'><div class='progress-bar'></div></div><p id='installStatus' class='progress-text'>Preparing firmware update...</p></div>";

  appendFirmwareReleaseInfo(html, info);

  html += "<div class='msg ";
  html += info.updateAvailable ? "P'" : "S'";
  html += "><strong>";
  html += info.updateAvailable ? "Update available" : "Already on the latest GitHub release";
  html += "</strong></div>";

  if (info.releaseBody.length() > 0) {
    html += "<div class='msg'><strong>Release Notes</strong><br/>";
    html += formatReleaseBody(escapeHtml(info.releaseBody));
    html += "</div>";
  }

  html += "<form action='";
  html += kFirmwareGitHubReleasesUrl;
  html += "' method='get' target='_blank' style='margin-bottom:14px'><button type='submit'>Open GitHub Releases</button></form>";

  if (WiFi.status() == WL_CONNECTED && info.assetUrl.length() > 0) {
    html += "<form onsubmit='return startFirmwareInstall();'><button class='D fw-install-btn' type='submit'>Download & Flash Latest Release</button></form>";
  } else if (WiFi.status() != WL_CONNECTED) {
    html += "<div class='msg D'><strong>WiFi is not connected.</strong><br/>Firmware updates need network access.</div>";
  }

  html += "<hr><br/><form action='/' method='get'><button type='submit'>Back to Menu</button></form>";
  html += "</div></body></html>";
  return html;
}

void handleFirmwareUpdate() {
  if (!checkMenuAuth()) return;

  FirmwareReleaseInfo info;
  String errorMessage;
  bool ok = fetchLatestFirmwareRelease(info, errorMessage);
  if (!ok) {
    info.currentVersion = kFirmwareVersion;
    info.latestVersion = "unavailable";
    info.releaseName = "GitHub release unavailable";
    info.releaseBody.clear();
    info.releaseUrl = kFirmwareGitHubReleasesUrl;
  }

  wm.server->send(200, "text/html", buildFirmwareUpdatePage(info, ok ? String() : errorMessage, ok));
}

void handleFirmwareUpdateInstall() {
  if (!checkMenuAuth()) return;

  FirmwareReleaseInfo info;
  String errorMessage;
  if (!fetchLatestFirmwareRelease(info, errorMessage)) {
    wm.server->send(200, "text/html", buildFirmwareUpdatePage(info, errorMessage, false));
    return;
  }

  wm.server->setContentLength(CONTENT_LENGTH_UNKNOWN);
  wm.server->send(200, "text/html", "");
  wm.server->sendContent(buildFirmwareInstallPageStart(info));

  // Stream incremental progress into the page while OTA writes flash blocks.
  int lastPercent = -1;
  bool flashed = flashFirmwareAsset(info, errorMessage, [&](size_t writtenBytes, size_t totalBytes) {
    int percent = 0;
    if (totalBytes > 0) {
      percent = static_cast<int>((writtenBytes * 100U) / totalBytes);
    } else if (writtenBytes > 0) {
      percent = 95;
    }
    if (percent > 100) {
      percent = 100;
    }
    if (percent == lastPercent) {
      return;
    }
    lastPercent = percent;
    String script = "<script>setFwProgress(";
    script += percent;
    script += ",\"";
    script += escapeJson(percent >= 100 ? String("Verifying checksum (SHA256)...") : String("Downloading and flashing firmware... ") + percent + "%");
    script += "\",false);</script>";
    wm.server->sendContent(script);
  });

  if (!flashed) {
    String script = "<script>setFwProgress(100,\"";
    script += escapeJson(errorMessage.length() ? errorMessage : String("Firmware update failed."));
    script += "\",true);</script>";
    wm.server->sendContent(script);
    wm.server->sendContent("<hr><br/><form action='/firmware-update' method='get'><button type='submit'>Back to Firmware Update</button></form></div></body></html>");
    return;
  }

  wm.server->sendContent("<script>setFwProgress(100,\"<span style='color:#2e7d32;font-weight:700'>&#10003;</span> Checksum verified. Firmware updated successfully. Device is rebooting now...\",false);</script>");
  wm.server->sendContent("</div></body></html>");
  delay(1600);
  ESP.restart();
}

void handleFactoryErase() {
  if (!checkMenuAuth()) return;

  Serial.println("[wifi] Factory erase requested");
  wm.server->send(200, "text/html", "<html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'><style>body{font-family:-apple-system,system-ui,'Segoe UI',Roboto,sans-serif;text-align:center;padding:18px;background:radial-gradient(ellipse 900px 500px at 50% -10%,#0d2338,transparent) #070c13;color:#e7f3fb}.wm-brand{display:flex;justify-content:center;align-items:center;min-height:64px;margin:4px 0 10px 0}.wm-brand-logo{display:block;max-width:min(100%,320px);height:auto;margin:0 auto}</style></head><body><div class='wm-brand'><img class='wm-brand-logo' src='/project-logo.png' alt='ESP DrumMachine'></div><p><strong>Config erased. Rebooting...</strong></p></body></html>");
  wm.server->client().stop();
  delay(600);
  ConfigManager::getInstance().clearConfig();
  // Clear WiFiManager/SDK STA credentials after app config keys are wiped.
  wm.resetSettings();
  delay(500);
  ESP.restart();
}

void handleLogout() {
  // Basic Auth has no real "logout" - forcing a fresh 401 challenge makes
  // most browsers drop the cached credentials for this realm.
  wm.server->requestAuthentication(HTTPAuthMethod::BASIC_AUTH);
}

void saveParamsCallback() {
  if (!menuPasswordParam) {
    return;
  }
  String pw = String(menuPasswordParam->getValue());
  pw.trim();
  ConfigManager &cfg = ConfigManager::getInstance();
  cfg.setWebMenuPassword(pw.c_str());
  cfg.saveConfig();
}

// Registered once wm.server exists (created lazily by WiFiManager on first
// startConfigPortal()/startWebPortal() call) - safe to call repeatedly,
// guarded so routes are only added the first time.
void registerRoutesOnce() {
  if (routesRegistered || !wm.server) return;
  wm.server->on("/project-logo.png", HTTP_GET, handleLogo);
  wm.server->on("/device-settings", HTTP_GET, handleDeviceSettings);
  wm.server->on("/device-settings", HTTP_POST, handleDeviceSettingsSave);
  wm.server->on("/sounds", HTTP_GET, handleSounds);
  wm.server->on("/sounds/upload", HTTP_POST, handleSoundUploadDone, handleSoundUploadFile);
  wm.server->on("/patterns", HTTP_GET, handlePatterns);
  wm.server->on("/patterns/download", HTTP_GET, handlePatternDownload);
  wm.server->on("/patterns/delete", HTTP_GET, handlePatternDelete);
  wm.server->on("/firmware-update", HTTP_GET, handleFirmwareUpdate);
  wm.server->on("/firmware-update/install", HTTP_GET, handleFirmwareUpdateInstall);
  wm.server->on("/factory-erase", HTTP_POST, handleFactoryErase);
  wm.server->on("/logout", HTTP_GET, handleLogout);
  routesRegistered = true;
}

std::vector<const char *> gFirstRunMenu = {"wifi"};
std::vector<const char *> gConnectedMenu = {"wifi", "param", "info", "custom", "restart", "sep"};
String gConnectedMenuHtml;

// First run / not-yet-connected: only WiFiManager's own "wifi" setup page,
// nothing about sounds/patterns/updates - there's nothing useful to do with
// those before the device is actually online.
void applyFirstRunMenu() {
  wm.setMenu(gFirstRunMenu);
  wm.setCustomMenuHTML("");
}

// Connected: WiFiManager's wifi/param/info pages plus our own custom menu
// buttons and a manual reset ("restart"), instead of WiFiManager's built-in
// file-upload "update" page (superseded by our GitHub-release-based
// /firmware-update).
void applyConnectedMenu() {
  wm.setMenu(gConnectedMenu);
  const DeviceConfig &cfg = ConfigManager::getInstance().getConfig();
  gConnectedMenuHtml = "<form action='/device-settings' method='get'><button>Device Settings</button></form>";
  gConnectedMenuHtml += "<form action='/sounds' method='get'><button>Sound Sets</button></form>";
  gConnectedMenuHtml += "<form action='/patterns' method='get'><button>Saved Patterns</button></form>";
  gConnectedMenuHtml += "<form action='/firmware-update' method='get'><button>Firmware Update</button></form>";
  gConnectedMenuHtml += "<form action='/factory-erase' method='post' onsubmit=\"return confirm('Erase all saved config and reboot?');\">";
  gConnectedMenuHtml += "<button style='background:#b00020;color:#fff'>Config Erase</button></form>";
  if (strlen(cfg.web_menu_password) >= 8) {
    gConnectedMenuHtml += "<form action='/logout' method='get'><button>Logout</button></form>";
  }
  wm.setCustomMenuHTML(gConnectedMenuHtml.c_str());
}

void updateStatusText() {
  switch (state) {
    case WifiPortalState::Off:
      snprintf(statusText, sizeof(statusText), "WiFi off");
      break;
    case WifiPortalState::Portal:
      snprintf(statusText, sizeof(statusText), "Setup: join \"%s\" (pw %s), open 192.168.4.1",
               AP_NAME, AP_PASSWORD);
      break;
    case WifiPortalState::Connecting:
      snprintf(statusText, sizeof(statusText), "Connecting to WiFi...");
      break;
    case WifiPortalState::Connected:
      snprintf(statusText, sizeof(statusText), "Connected: http://%s",
                WiFi.localIP().toString().c_str());
      break;
  }
}

}  // namespace

void wifiPortalInit() {
  // Default debug level (WM_DEBUG_NOTIFY) is too quiet to show whether the
  // AP password was actually applied - the relevant log lines are gated at
  // WM_DEBUG_VERBOSE. Bump temporarily to nail down the "AP unprotected"
  // report with real evidence instead of re-reading source.
  wm.setDebugOutput(true, WM_DEBUG_VERBOSE);
  wm.setConfigPortalBlocking(false);
  wm.setCustomHeadElement(kPortalHeadElement);
  wm.setTitle("ESP DrumMachine");
  wm.setSaveParamsCallback(saveParamsCallback);
  wm.setParamsPage(false);

  ConfigManager::getInstance().loadConfig();
  const DeviceConfig &cfg = ConfigManager::getInstance().getConfig();
  if (!menuPasswordParam) {
    menuPasswordParam = new WiFiManagerParameter(
        "web_menu_password", "Web Menu Password (min 8 chars, blank = no login)",
        cfg.web_menu_password, sizeof(cfg.web_menu_password));
    wm.addParameter(menuPasswordParam);
  }
}

void wifiPortalToggle() {
  if (state == WifiPortalState::Off) {
    // getWiFiIsSaved() reads persisted creds via esp_wifi_get_config(), which
    // needs the WiFi driver initialized first - on a fresh boot (mode still
    // WIFI_MODE_NULL, first-ever toggle) that call fails and the saved SSID
    // reads back empty, so this always fell through to the setup portal even
    // with creds already stored from a previous session. Set STA mode before
    // asking, so the driver is up and the check reflects what's really saved.
    WiFi.mode(WIFI_STA);
    bool wifiIsSaved = wm.getWiFiIsSaved();
    Serial.printf("wifi toggle: state=%d wifiIsSaved=%d WiFi.SSID()='%s'\n", (int)state, wifiIsSaved,
                  WiFi.SSID().c_str());

    if (wifiIsSaved) {
      WiFi.begin();
      connectStartMs = millis();
      state = WifiPortalState::Connecting;
      Serial.println("wifi toggle: have saved creds, connecting");
    } else {
      applyFirstRunMenu();
      wm.startConfigPortal(AP_NAME, AP_PASSWORD);
      registerRoutesOnce();
      state = WifiPortalState::Portal;
      Serial.println("wifi toggle: no saved creds, starting setup portal");
    }
  } else if (state == WifiPortalState::Connected) {
    // Only a fully-connected session can be explicitly turned off from
    // here - tapping again mid-connect/mid-setup just re-opens the status
    // view instead (see wifiClickedCb), it doesn't cancel anything.
    //
    // Deliberately NOT calling wm.stopConfigPortal() here: by this point
    // WiFiManager's own AP config portal was already torn down internally
    // (see the crash postmortem above wifiPortalLoop()), and stopWebPortal()
    // alone correctly guards against acting on an already-inactive portal.
    wm.stopWebPortal();
    WiFi.disconnect(true, false);  // wifioff=true, eraseap=false - keep saved creds
    WiFi.mode(WIFI_OFF);
    state = WifiPortalState::Off;
    Serial.println("wifi toggle: turned off (credentials kept)");
  }
  updateStatusText();
}

// CRASH POSTMORTEM (10.07.2026, fixed): after a successful AP-portal save,
// WiFiManager's own process()/processConfigPortal() already tears the
// config portal down internally (default _disableConfigPortal=true) -
// including resetting its dnsServer/server unique_ptrs - as part of the
// very same process() call that detects the connection. Calling
// wm.stopConfigPortal() again ourselves right after seeing WL_CONNECTED
// raced that internal teardown and dereferenced an already-reset dnsServer
// (LoadProhibited, decoded via addr2line straight to
// WiFiManager::shutdownConfigPortal() -> dnsServer->processNextRequest()).
// Fix: never call wm.stopConfigPortal() ourselves, and wait a short
// settle period after seeing WL_CONNECTED before touching the portal
// again (startWebPortal()) at all, so we're never racing that internal
// cleanup.
uint32_t connectedSettleStartMs = 0;
constexpr uint32_t CONNECTED_SETTLE_MS = 500;

void finalizeConnected() {
  applyConnectedMenu();
  wm.startWebPortal();
  if (!wm.server) {
    // startWebPortal() silently no-ops if WiFiManager still thinks a
    // config portal is active - shouldn't happen given the settle delay
    // above, but if it does we want to see it rather than fail silently.
    Serial.println("wifi: startWebPortal() did not create a server - retrying next loop");
    connectedSettleStartMs = millis();  // try again in another CONNECTED_SETTLE_MS
    return;
  }
  registerRoutesOnce();
  state = WifiPortalState::Connected;
  connectedSettleStartMs = 0;
  Serial.println("wifi: connected, web portal + custom routes live");
}

void wifiPortalLoop() {
  switch (state) {
    case WifiPortalState::Off:
      return;
    case WifiPortalState::Portal:
      wm.process();
      if (WiFi.status() == WL_CONNECTED) {
        if (connectedSettleStartMs == 0) {
          connectedSettleStartMs = millis();
        } else if (millis() - connectedSettleStartMs > CONNECTED_SETTLE_MS) {
          finalizeConnected();
        }
      } else {
        connectedSettleStartMs = 0;
      }
      break;
    case WifiPortalState::Connecting:
      if (WiFi.status() == WL_CONNECTED) {
        finalizeConnected();
      } else if (millis() - connectStartMs > STA_CONNECT_TIMEOUT_MS) {
        applyFirstRunMenu();
        wm.startConfigPortal(AP_NAME, AP_PASSWORD);
        registerRoutesOnce();
        state = WifiPortalState::Portal;
      }
      break;
    case WifiPortalState::Connected:
      wm.process();
      break;
  }
  updateStatusText();
}

WifiPortalState wifiPortalGetState() { return state; }

const char *wifiPortalStatusText() { return statusText; }
