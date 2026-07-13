#pragma once

#include <cstddef>

#define PATTERN_DIR "/patterns"

// Persists the current pattern (trackMatrix + bpm + volume) as a
// hand-editable JSON file on SD. Caller must already have the SPI bus
// pointed at the SD card's pins (see sd_bus.h for the touch<->SD
// bus-switch dance this project uses).
void savePattern(const char *path);

// Returns true if a pattern was found and loaded into trackMatrix/bpm/volume.
bool loadPattern(const char *path);

// Scans /patterns/ for patternN.json files, sorted ascending by N. Returns
// how many were found (up to maxNames).
int scanPatternFiles(char names[][32], int maxNames);

// Saves the current pattern to the next free /patterns/patternN.json slot
// (highest existing N + 1). Writes the filename (e.g. "pattern3.json") into
// outName if non-null. Returns the slot number used.
int saveNextPattern(char *outName, size_t outNameLen);

// Loads the highest-numbered existing pattern file, if any. Returns false
// if none exist yet.
bool loadLatestPattern();
