#pragma once

// Scans /Sounds/ on the SD card for subfolders (each one a sample set).
// Caller must point the shared SPI bus at the SD card first (see sd_bus.h).
// Returns the number of names written into `names` (each up to 31 chars).
int scanSoundSets(char names[][32], int maxNames);
