#include <Arduino.h>

#include "audio_engine.h"
#include "config_manager.h"
#include "globals.h"
#include "pattern_storage.h"
#include "sd_test.h"
#include "sequencer_ui.h"
#include "wifi_portal.h"

void setup() {
  Serial.begin(115200);
  sequencerUiInit();
  sdTestInit();
  audioEngineInit();

  ConfigManager::getInstance().loadConfig();
  const DeviceConfig &cfg = ConfigManager::getInstance().getConfig();
  if (strcmp(cfg.default_sound_set, audioEngineCurrentSampleSet()) != 0) {
    audioEngineLoadSampleSet(cfg.default_sound_set);  // still on the SD pins here
  }

  bool hadSavedPattern = loadLatestPattern();  // still on the SD pins here, no extra bus switch needed
  if (!hadSavedPattern) {
    // Nothing to restore from SD (fresh card) - fall back to the configured
    // boot defaults instead of the hardcoded globals.h initializers.
    bpm = cfg.default_bpm;
    swing = cfg.default_swing;
    volume = cfg.default_volume;
  }
  sequencerUiRestoreTouchSpi();
  sequencerUiRefreshFromState();
  wifiPortalInit();  // WiFi stays off until toggled on via the UI icon
}

void loop() {
  sequencerUiPollTouch();
  sequencerUiUpdatePlayhead();
  wifiPortalLoop();
  delay(1);  // yield so the idle task can feed the watchdog
}
