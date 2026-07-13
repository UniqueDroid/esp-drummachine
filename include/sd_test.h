#pragma once

// Mounts the SD card and lists the root directory over Serial, just to
// prove the wiring/CS pin. Real WAV streaming for sample playback replaces
// this once the sequencer timing and touch UI are confirmed on hardware.
void sdTestInit();
