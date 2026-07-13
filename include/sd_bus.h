#pragma once

// Touch and SD share one SPI peripheral at different pin mappings, and
// Arduino-ESP32's SPI.begin() is a no-op if the bus is already started -
// so every switch between them must go through SPI.end() first. Use these
// two helpers for that; sequencerUiRestoreTouchSpi() is the way back.
void beginSdSpi();
