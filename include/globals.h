#pragma once

#include <Arduino.h>

constexpr int NUM_TRACKS = 6;
constexpr int NUM_STEPS = 16;

// Step matrix: trackMatrix[track][step]. Written by the touch handler in
// the main loop, read by the audio timer callback - plain volatile bools
// are fine here, a torn read just means a step trigger fires a beat late
// at worst.
extern volatile bool trackMatrix[NUM_TRACKS][NUM_STEPS];

// Updated by the audio engine when the step advances, read by the UI to
// move the playhead.
extern volatile int currentStep;

extern volatile int bpm;

// 0-100, applied to the final mixed sample before it goes to the DAC.
extern volatile int volume;

// Play/pause: when false, the audio engine stops advancing steps (existing
// decaying voices still finish playing out).
extern volatile bool running;

// 0-100 swing amount. 0 = straight 16ths. Delays every 2nd step within each
// pair while keeping the pair's total duration constant (so overall tempo
// is unaffected), for a shuffled/groovy feel.
extern volatile int swing;
