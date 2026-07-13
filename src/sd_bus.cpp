#include "sd_bus.h"

#include <SPI.h>

#include "globals.h"

void beginSdSpi() {
  SPI.end();
  SPI.begin(SD_SCLK, SD_MISO, SD_MOSI, SD_CS);
}
