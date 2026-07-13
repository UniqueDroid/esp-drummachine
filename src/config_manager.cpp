// Persistent configuration manager backed by ESP32 Preferences (NVS).
#include "config_manager.h"
#include "globals.h"
#include <Preferences.h>

ConfigManager& ConfigManager::getInstance() {
  static ConfigManager instance;
  return instance;
}

namespace {
constexpr int kDefaultBpm = 120;
constexpr int kDefaultSwing = 0;
constexpr int kDefaultVolume = 80;
}  // namespace

ConfigManager::ConfigManager() {
  memset(&config_, 0, sizeof(config_));
  config_.default_bpm = kDefaultBpm;
  config_.default_swing = kDefaultSwing;
  config_.default_volume = kDefaultVolume;
  strcpy(config_.default_sound_set, "Set1");
}

ConfigManager::~ConfigManager() {}

bool ConfigManager::loadConfig() {
  Preferences prefs;
  prefs.begin("drummachine", true);  // Read-only mode

  String web_menu_password = prefs.getString("web_menu_pw", "");
  int default_bpm = prefs.getInt("default_bpm", kDefaultBpm);
  int default_swing = prefs.getInt("default_swing", kDefaultSwing);
  int default_volume = prefs.getInt("default_volume", kDefaultVolume);
  String default_sound_set = prefs.getString("default_set", "Set1");

  prefs.end();

  strlcpy(config_.web_menu_password, web_menu_password.c_str(), sizeof(config_.web_menu_password));
  config_.default_bpm = default_bpm;
  config_.default_swing = default_swing;
  config_.default_volume = default_volume;
  strlcpy(config_.default_sound_set, default_sound_set.c_str(), sizeof(config_.default_sound_set));

  return true;
}

bool ConfigManager::saveConfig() {
  Preferences prefs;
  prefs.begin("drummachine", false);  // Write mode

  prefs.putString("web_menu_pw", config_.web_menu_password);
  prefs.putInt("default_bpm", config_.default_bpm);
  prefs.putInt("default_swing", config_.default_swing);
  prefs.putInt("default_volume", config_.default_volume);
  prefs.putString("default_set", config_.default_sound_set);

  prefs.end();

  // Apply the boot defaults live, same as pfsense-status-esp32's
  // ConfigManager does for its globals - no reboot needed to hear the change.
  bpm = config_.default_bpm;
  swing = config_.default_swing;
  volume = config_.default_volume;

  return true;
}

void ConfigManager::setWebMenuPassword(const char* password) {
  String p(password ? password : "");
  p.trim();
  if (p.length() > 0 && p.length() < 8) {
    return;
  }
  strlcpy(config_.web_menu_password, p.c_str(), sizeof(config_.web_menu_password));
}

void ConfigManager::setDefaultBpm(int bpmValue) {
  if (bpmValue < 60) bpmValue = 60;
  if (bpmValue > 200) bpmValue = 200;
  config_.default_bpm = bpmValue;
}

void ConfigManager::setDefaultSwing(int swingValue) {
  if (swingValue < 0) swingValue = 0;
  if (swingValue > 100) swingValue = 100;
  config_.default_swing = swingValue;
}

void ConfigManager::setDefaultVolume(int volumeValue) {
  if (volumeValue < 0) volumeValue = 0;
  if (volumeValue > 100) volumeValue = 100;
  config_.default_volume = volumeValue;
}

void ConfigManager::setDefaultSoundSet(const char* setName) {
  String s(setName ? setName : "");
  s.trim();
  if (s.length() == 0) {
    return;
  }
  strlcpy(config_.default_sound_set, s.c_str(), sizeof(config_.default_sound_set));
}

void ConfigManager::clearConfig() {
  memset(&config_, 0, sizeof(config_));
  config_.default_bpm = kDefaultBpm;
  config_.default_swing = kDefaultSwing;
  config_.default_volume = kDefaultVolume;
  strcpy(config_.default_sound_set, "Set1");

  Preferences prefs;
  prefs.begin("drummachine", false);
  prefs.clear();
  prefs.end();
}
