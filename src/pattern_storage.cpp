#include "pattern_storage.h"

#include <ArduinoJson.h>
#include <SD.h>

#include <cstdlib>
#include <cstring>

#include "globals.h"

namespace {

// Expects a bare filename like "pattern3.json"; returns 3, or -1 if the
// name doesn't match that shape.
int patternNumberFromName(const char *name) {
  if (strncmp(name, "pattern", 7) != 0) return -1;
  int n = atoi(name + 7);
  return n > 0 ? n : -1;
}

}  // namespace

void savePattern(const char *path) {
  JsonDocument doc;
  doc["bpm"] = bpm;
  doc["volume"] = volume;
  doc["swing"] = swing;
  JsonArray pattern = doc["pattern"].to<JsonArray>();
  for (int t = 0; t < NUM_TRACKS; t++) {
    JsonArray row = pattern.add<JsonArray>();
    for (int s = 0; s < NUM_STEPS; s++) {
      row.add((bool)trackMatrix[t][s]);
    }
  }

  // /patterns/ must exist before a file can be created inside it.
  if (!SD.exists("/patterns")) {
    SD.mkdir("/patterns");
  }

  File f = SD.open(path, FILE_WRITE);
  if (!f) {
    Serial.printf("savePattern: could not open %s for writing\n", path);
    return;
  }
  serializeJsonPretty(doc, f);
  f.close();
  Serial.printf("savePattern: wrote %s\n", path);
}

bool loadPattern(const char *path) {
  File f = SD.open(path, FILE_READ);
  if (!f) {
    Serial.printf("loadPattern: %s not found\n", path);
    return false;
  }

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, f);
  f.close();
  if (err) {
    Serial.printf("loadPattern: %s parse error: %s\n", path, err.c_str());
    return false;
  }

  if (doc["bpm"].is<int>()) bpm = doc["bpm"].as<int>();
  if (doc["volume"].is<int>()) volume = doc["volume"].as<int>();
  if (doc["swing"].is<int>()) swing = doc["swing"].as<int>();

  JsonArray pattern = doc["pattern"];
  if (pattern.isNull()) {
    Serial.printf("loadPattern: %s missing \"pattern\" array\n", path);
    return false;
  }
  int t = 0;
  for (JsonArray row : pattern) {
    if (t >= NUM_TRACKS) break;
    int s = 0;
    for (JsonVariant v : row) {
      if (s >= NUM_STEPS) break;
      trackMatrix[t][s] = v.as<bool>();
      s++;
    }
    t++;
  }

  Serial.printf("loadPattern: loaded %s\n", path);
  return true;
}

int scanPatternFiles(char names[][32], int maxNames) {
  if (!SD.exists(PATTERN_DIR)) return 0;
  File dir = SD.open(PATTERN_DIR);
  if (!dir || !dir.isDirectory()) return 0;

  int count = 0;
  File entry = dir.openNextFile();
  while (entry && count < maxNames) {
    if (!entry.isDirectory() && patternNumberFromName(entry.name()) > 0) {
      strncpy(names[count], entry.name(), 31);
      names[count][31] = '\0';
      count++;
    }
    entry = dir.openNextFile();
  }

  // Insertion sort by pattern number, ascending - count stays small (a
  // handful of saved patterns), so this is plenty fast.
  for (int i = 1; i < count; i++) {
    char tmp[32];
    strcpy(tmp, names[i]);
    int tmpNum = patternNumberFromName(tmp);
    int j = i - 1;
    while (j >= 0 && patternNumberFromName(names[j]) > tmpNum) {
      strcpy(names[j + 1], names[j]);
      j--;
    }
    strcpy(names[j + 1], tmp);
  }
  return count;
}

int saveNextPattern(char *outName, size_t outNameLen) {
  if (!SD.exists(PATTERN_DIR)) {
    SD.mkdir(PATTERN_DIR);
  }

  char names[32][32];
  int count = scanPatternFiles(names, 32);
  int maxNum = 0;
  for (int i = 0; i < count; i++) {
    int n = patternNumberFromName(names[i]);
    if (n > maxNum) maxNum = n;
  }
  int next = maxNum + 1;

  char path[64];
  snprintf(path, sizeof(path), "%s/pattern%d.json", PATTERN_DIR, next);
  savePattern(path);

  if (outName) {
    snprintf(outName, outNameLen, "pattern%d.json", next);
  }
  return next;
}

bool loadLatestPattern() {
  char names[32][32];
  int count = scanPatternFiles(names, 32);
  if (count == 0) return false;

  char path[64];
  snprintf(path, sizeof(path), "%s/%s", PATTERN_DIR, names[count - 1]);
  return loadPattern(path);
}
