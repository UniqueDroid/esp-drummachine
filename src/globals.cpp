#include "globals.h"

volatile bool trackMatrix[NUM_TRACKS][NUM_STEPS] = {};
volatile int currentStep = 0;
volatile int bpm = 120;
volatile int volume = 80;
volatile bool running = false;
volatile int swing = 0;
