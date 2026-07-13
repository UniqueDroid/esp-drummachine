#pragma once

// Sample-accurate step sequencer + DAC audio output. Each track plays a WAV
// sample loaded from /Sounds/<set>/<track>.wav; tracks whose file is
// missing/invalid fall back to a short placeholder tone.
void audioEngineInit();

// Switches to a different sample set at runtime (e.g. "Set2"), reloading
// all 6 track samples from /Sounds/<setName>/. Mutes audio output and
// clears active voices while swapping buffers so nothing reads freed
// memory. Caller must point the shared SPI bus at the SD card first (see
// sd_bus.h) - this does synchronous SD file I/O.
void audioEngineLoadSampleSet(const char *setName);

// Name of the currently loaded sample set (e.g. "Set1").
const char *audioEngineCurrentSampleSet();
