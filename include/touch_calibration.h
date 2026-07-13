#pragma once

#include <XPT2046_Touchscreen.h>
#include <TFT_eSPI.h>

// General affine raw-touch -> screen-pixel transform:
//   screenX = A*rawX + B*rawY + C
//   screenY = D*rawX + E*rawY + F
// Solved from 3 tapped reference points, so it self-corrects for axis swap,
// inversion and scale in one pass instead of needing hand-guessed flags.
struct TouchCalibration {
  double a, b, c, d, e, f;
};

// Loads a previously saved calibration from NVS. Returns false if none is
// stored yet (first boot).
bool loadTouchCalibration(TouchCalibration *cal);

// Draws 3 crosshair targets, waits for a tap on each, solves the affine
// transform and persists it to NVS.
void runTouchCalibration(TFT_eSPI &tft, XPT2046_Touchscreen &ts, TouchCalibration *cal);

// Applies a solved calibration to a raw touch reading.
void applyTouchCalibration(const TouchCalibration &cal, int rawX, int rawY, int *screenX, int *screenY);
