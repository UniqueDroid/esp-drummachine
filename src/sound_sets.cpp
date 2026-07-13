#include "sound_sets.h"

#include <SD.h>

#include <cstring>

int scanSoundSets(char names[][32], int maxNames) {
  File dir = SD.open("/Sounds");
  if (!dir || !dir.isDirectory()) {
    Serial.println("scanSoundSets: /Sounds not found");
    return 0;
  }

  int count = 0;
  File entry = dir.openNextFile();
  while (entry && count < maxNames) {
    if (entry.isDirectory()) {
      strncpy(names[count], entry.name(), 31);
      names[count][31] = '\0';
      count++;
    }
    entry = dir.openNextFile();
  }
  return count;
}
