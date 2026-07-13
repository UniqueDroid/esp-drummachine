#include "touch_calibration.h"

#include <Preferences.h>

#include "globals.h"
#include "logo_image.h"

namespace {

constexpr int TARGET_X[3] = {30, DASHBOARD_WIDTH - 30, 30};
constexpr int TARGET_Y[3] = {30, 30, DASHBOARD_HEIGHT - 30};

double det3(double a, double b, double c, double d, double e, double f, double g, double h, double i) {
  return a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
}

// Solves target[k] = coefRawX*rawX[k] + coefRawY*rawY[k] + coefConst for k=0..2.
void solveAffineRow(const int target[3], const int rawX[3], const int rawY[3],
                    double *coefRawX, double *coefRawY, double *coefConst) {
  double detM = det3(rawX[0], rawY[0], 1, rawX[1], rawY[1], 1, rawX[2], rawY[2], 1);
  double detA = det3(target[0], rawY[0], 1, target[1], rawY[1], 1, target[2], rawY[2], 1);
  double detB = det3(rawX[0], target[0], 1, rawX[1], target[1], 1, rawX[2], target[2], 1);
  double detC = det3(rawX[0], rawY[0], target[0], rawX[1], rawY[1], target[1], rawX[2], rawY[2], target[2]);
  *coefRawX = detA / detM;
  *coefRawY = detB / detM;
  *coefConst = detC / detM;
}

void drawCrosshair(TFT_eSPI &tft, int x, int y) {
  tft.drawLine(x - 10, y, x + 10, y, TFT_WHITE);
  tft.drawLine(x, y - 10, x, y + 10, TFT_WHITE);
  tft.drawCircle(x, y, 6, TFT_WHITE);
}

void waitForTap(XPT2046_Touchscreen &ts, int *rawX, int *rawY) {
  while (!ts.touched()) {
    delay(10);
  }
  delay(30);  // let the reading settle
  TS_Point p = ts.getPoint();
  *rawX = p.x;
  *rawY = p.y;
  while (ts.touched()) {
    delay(10);  // wait for release before the next target
  }
}

}  // namespace

bool loadTouchCalibration(TouchCalibration *cal) {
  Preferences prefs;
  prefs.begin("touchcal", true);
  bool valid = prefs.getBool("valid", false);
  if (valid) {
    cal->a = prefs.getDouble("a", 0);
    cal->b = prefs.getDouble("b", 0);
    cal->c = prefs.getDouble("c", 0);
    cal->d = prefs.getDouble("d", 0);
    cal->e = prefs.getDouble("e", 0);
    cal->f = prefs.getDouble("f", 0);
  }
  prefs.end();
  return valid;
}

void runTouchCalibration(TFT_eSPI &tft, XPT2046_Touchscreen &ts, TouchCalibration *cal) {
  int rawX[3];
  int rawY[3];

  for (int i = 0; i < 3; i++) {
    tft.fillScreen(TFT_BLACK);
    int logoX = (DASHBOARD_WIDTH - LOGO_WIDTH) / 2;
    int logoY = (DASHBOARD_HEIGHT - LOGO_HEIGHT) / 2;
    // see the matching comment in sequencer_ui.cpp - pushImage() needs
    // setSwapBytes(true) for our RGB565 array to come out right.
    tft.setSwapBytes(true);
    tft.pushImage(logoX, logoY, LOGO_WIDTH, LOGO_HEIGHT, (const uint16_t *)logo_map);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setCursor(10, 10);
    tft.println("Touch calibration");
    tft.setCursor(10, 30);
    tft.printf("Tap target %d/3", i + 1);
    drawCrosshair(tft, TARGET_X[i], TARGET_Y[i]);
    waitForTap(ts, &rawX[i], &rawY[i]);
  }

  solveAffineRow(TARGET_X, rawX, rawY, &cal->a, &cal->b, &cal->c);
  solveAffineRow(TARGET_Y, rawX, rawY, &cal->d, &cal->e, &cal->f);

  Preferences prefs;
  prefs.begin("touchcal", false);
  prefs.putDouble("a", cal->a);
  prefs.putDouble("b", cal->b);
  prefs.putDouble("c", cal->c);
  prefs.putDouble("d", cal->d);
  prefs.putDouble("e", cal->e);
  prefs.putDouble("f", cal->f);
  prefs.putBool("valid", true);
  prefs.end();

  tft.fillScreen(TFT_BLACK);
}

void applyTouchCalibration(const TouchCalibration &cal, int rawX, int rawY, int *screenX, int *screenY) {
  *screenX = (int)lround(cal.a * rawX + cal.b * rawY + cal.c);
  *screenY = (int)lround(cal.d * rawX + cal.e * rawY + cal.f);
  *screenX = constrain(*screenX, 0, DASHBOARD_WIDTH - 1);
  *screenY = constrain(*screenY, 0, DASHBOARD_HEIGHT - 1);
}
