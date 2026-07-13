#include "sd_test.h"

#include <FS.h>
#include <SD.h>

#include "sd_bus.h"

void sdTestInit() {
  beginSdSpi();
  if (!SD.begin(SD_CS)) {
    Serial.println("SD card mount failed");
    return;
  }

  Serial.println("SD card mounted, root directory:");
  File root = SD.open("/");
  File entry = root.openNextFile();
  while (entry) {
    Serial.printf("  %s (%u bytes)\n", entry.name(), (unsigned)entry.size());
    entry = root.openNextFile();
  }
}
