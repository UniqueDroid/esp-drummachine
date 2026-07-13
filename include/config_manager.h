#pragma once

// Owns load/save/clear operations for persisted device configuration.
// Mirrors pfsense-status-esp32's ConfigManager pattern.
#include <Arduino.h>

struct DeviceConfig {
  char web_menu_password[64];
  int default_bpm;
  int default_swing;
  int default_volume;
  char default_sound_set[32];
};

class ConfigManager {
 public:
  static ConfigManager& getInstance();

  // Load config from Preferences.
  bool loadConfig();

  // Save config to Preferences.
  bool saveConfig();

  // Get current config.
  const DeviceConfig& getConfig() const { return config_; }

  // Update specific fields.
  void setWebMenuPassword(const char* password);
  void setDefaultBpm(int bpm);
  void setDefaultSwing(int swing);
  void setDefaultVolume(int volume);
  void setDefaultSoundSet(const char* setName);

  // Clear all config back to defaults.
  void clearConfig();

 private:
  ConfigManager();
  ~ConfigManager();

  DeviceConfig config_;

  // Prevent copying.
  ConfigManager(const ConfigManager&) = delete;
  ConfigManager& operator=(const ConfigManager&) = delete;
};
