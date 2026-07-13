#include "sequencer_ui.h"

#include <SPI.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>
#include <lvgl.h>

#include "audio_engine.h"
#include "genre_presets.h"
#include "globals.h"
#include "logo_image.h"
#include "pattern_storage.h"
#include "sd_bus.h"
#include "sound_sets.h"
#include "touch_calibration.h"
#include "wifi_portal.h"

namespace {

constexpr int MATRIX_HEIGHT = 144;  // 6 rows x 24px
constexpr int ROW_H = 24;           // 3 slider rows + 1 icon row below the matrix, 240 total
constexpr int MAX_SOUND_SETS = 8;
constexpr int MAX_PATTERN_FILES = 16;

const lv_color_t TRACK_COLORS[NUM_TRACKS] = {
    lv_color_hex(0xE74C3C), lv_color_hex(0xE67E22), lv_color_hex(0xF1C40F),
    lv_color_hex(0x2ECC71), lv_color_hex(0x1ABC9C), lv_color_hex(0x3498DB),
};

TFT_eSPI tft;
XPT2046_Touchscreen ts(TOUCH_CS, TOUCH_IRQ);
TouchCalibration calibration;

lv_disp_draw_buf_t dispDrawBuf;
lv_color_t *drawBuf;
lv_disp_drv_t dispDrv;
lv_indev_drv_t indevDrv;

lv_obj_t *stepMatrix;
lv_obj_t *playPauseBtn;
lv_obj_t *playPauseLabel;
lv_obj_t *volumeSlider;
lv_obj_t *volumeLabel;
lv_obj_t *tempoSlider;
lv_obj_t *tempoLabel;
lv_obj_t *swingSlider;
lv_obj_t *swingLabel;

lv_obj_t *soundSetPage;
lv_obj_t *soundSetList;
char soundSetNames[MAX_SOUND_SETS][32];

lv_obj_t *patternPage;
lv_obj_t *patternList;
char patternFileNames[MAX_PATTERN_FILES][32];

lv_obj_t *presetPage;

lv_obj_t *wifiIconLabel;
lv_obj_t *wifiPage;
lv_obj_t *wifiStatusLabel;
WifiPortalState lastDrawnWifiState = WifiPortalState::Off;

const char *stepMatrixMap[NUM_TRACKS * NUM_STEPS + NUM_TRACKS];
int lastDrawnStep = -1;

void buildStepMatrixMap() {
  // LVGL treats an empty string ("") as "end of row/map" - a real button
  // must have a non-empty label, so cells use a single space instead. Found
  // by writing a small native LVGL test harness (btnmatrix's per-item draw
  // callback never fired at all with "" labels, since btn_cnt came out 0).
  int idx = 0;
  for (int t = 0; t < NUM_TRACKS; t++) {
    for (int s = 0; s < NUM_STEPS; s++) {
      stepMatrixMap[idx++] = " ";
    }
    stepMatrixMap[idx++] = (t == NUM_TRACKS - 1) ? "" : "\n";
  }
}

void flushCb(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *colorP) {
  int32_t w = area->x2 - area->x1 + 1;
  int32_t h = area->y2 - area->y1 + 1;
  tft.startWrite();
  tft.setAddrWindow(area->x1, area->y1, w, h);
  tft.pushColors((uint16_t *)&colorP->full, w * h, true);
  tft.endWrite();
  lv_disp_flush_ready(disp);
}

void drawBootScreen(TFT_eSPI &tft) {
  tft.fillScreen(TFT_BLACK);
  int logoX = (DASHBOARD_WIDTH - LOGO_WIDTH) / 2;
  int logoY = 18;
  // pushImage() doesn't swap bytes by default (unlike pushColors(), which our
  // LVGL flush_cb calls with swap=true) - without this the RGB565 bytes come
  // out scrambled (looked like colour noise on real hardware).
  tft.setSwapBytes(true);
  tft.pushImage(logoX, logoY, LOGO_WIDTH, LOGO_HEIGHT, (const uint16_t *)logo_map);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextDatum(MC_DATUM);
  tft.drawString("Tap screen to calibrate", DASHBOARD_WIDTH / 2, logoY + LOGO_HEIGHT + 20, 2);
  tft.setTextDatum(TL_DATUM);  // restore default used elsewhere (calibration target labels)
}

void touchpadReadCb(lv_indev_drv_t *drv, lv_indev_data_t *data) {
  if (ts.touched()) {
    TS_Point p = ts.getPoint();
    if (p.z >= TOUCH_MIN_PRESSURE) {
      int sx, sy;
      applyTouchCalibration(calibration, p.x, p.y, &sx, &sy);
      data->point.x = sx;
      data->point.y = sy;
      data->state = LV_INDEV_STATE_PRESSED;
      return;
    }
  }
  data->state = LV_INDEV_STATE_RELEASED;
}

// Recolors each step-matrix button per its track color / on-off state, and
// highlights the current playhead column - lets one btnmatrix widget stand
// in for what would otherwise be 96 hand-drawn cells.
void matrixDrawPartCb(lv_event_t *e) {
  lv_obj_draw_part_dsc_t *dsc = lv_event_get_draw_part_dsc(e);
  // btnmatrix fires this for each button's background rect AND separately
  // for its (empty) label - only the rect draw has rect_dsc set.
  if (dsc->part != LV_PART_ITEMS || dsc->rect_dsc == NULL) return;

  int track = dsc->id / NUM_STEPS;
  int step = dsc->id % NUM_STEPS;
  bool on = trackMatrix[track][step];
  bool playhead = (step == currentStep);

  dsc->rect_dsc->bg_color = on ? TRACK_COLORS[track] : lv_color_hex(0x202020);
  dsc->rect_dsc->bg_opa = LV_OPA_COVER;
  if (playhead) {
    dsc->rect_dsc->border_color = lv_color_white();
    dsc->rect_dsc->border_width = 2;
    dsc->rect_dsc->border_opa = LV_OPA_COVER;
  }
}

void matrixValueChangedCb(lv_event_t *e) {
  uint16_t id = lv_btnmatrix_get_selected_btn(stepMatrix);
  if (id == LV_BTNMATRIX_BTN_NONE) return;
  int track = id / NUM_STEPS;
  int step = id % NUM_STEPS;
  trackMatrix[track][step] = !trackMatrix[track][step];
  lv_obj_invalidate(stepMatrix);
}

void playPauseClickedCb(lv_event_t *e) {
  running = !running;
  lv_label_set_text(playPauseLabel, running ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY);
}

void volumeChangedCb(lv_event_t *e) {
  volume = lv_slider_get_value(volumeSlider);
  lv_label_set_text_fmt(volumeLabel, "VOL %d%%", volume);
}

void tempoChangedCb(lv_event_t *e) {
  bpm = lv_slider_get_value(tempoSlider);
  lv_label_set_text_fmt(tempoLabel, "BPM %d", bpm);
}

void swingChangedCb(lv_event_t *e) {
  swing = lv_slider_get_value(swingSlider);
  lv_label_set_text_fmt(swingLabel, "SWG %d%%", swing);
}

void randomizeClickedCb(lv_event_t *e) {
  for (int t = 0; t < NUM_TRACKS; t++) {
    for (int s = 0; s < NUM_STEPS; s++) {
      trackMatrix[t][s] = (random(100) < 25);
    }
  }
  lv_obj_invalidate(stepMatrix);
}

void presetItemClickedCb(lv_event_t *e) {
  int index = (int)(intptr_t)lv_event_get_user_data(e);
  loadGenrePreset(index);
  sequencerUiRefreshFromState();
  lv_obj_add_flag(presetPage, LV_OBJ_FLAG_HIDDEN);
}

void presetBackClickedCb(lv_event_t *e) { lv_obj_add_flag(presetPage, LV_OBJ_FLAG_HIDDEN); }

void presetsClickedCb(lv_event_t *e) { lv_obj_clear_flag(presetPage, LV_OBJ_FLAG_HIDDEN); }

void refreshWifiUi() {
  WifiPortalState wifiState = wifiPortalGetState();
  // LVGL's built-in symbol font has no "no wifi" glyph - use colour instead
  // to tell "off" (tap to turn on) apart from "on" (tap to turn off).
  lv_color_t color = (wifiState == WifiPortalState::Off) ? lv_color_hex(0x808080) : lv_color_hex(0x3498DB);
  lv_obj_set_style_text_color(wifiIconLabel, color, 0);
  lv_label_set_text(wifiStatusLabel, wifiPortalStatusText());
  lastDrawnWifiState = wifiState;
}

void wifiClickedCb(lv_event_t *e) {
  wifiPortalToggle();
  refreshWifiUi();
  lv_obj_clear_flag(wifiPage, LV_OBJ_FLAG_HIDDEN);
}

void wifiBackClickedCb(lv_event_t *e) { lv_obj_add_flag(wifiPage, LV_OBJ_FLAG_HIDDEN); }

void saveClickedCb(lv_event_t *e) {
  beginSdSpi();
  char savedName[32];
  saveNextPattern(savedName, sizeof(savedName));
  sequencerUiRestoreTouchSpi();
  Serial.printf("saved %s\n", savedName);
}

void soundSetItemClickedCb(lv_event_t *e) {
  const char *name = (const char *)lv_event_get_user_data(e);
  beginSdSpi();
  audioEngineLoadSampleSet(name);
  sequencerUiRestoreTouchSpi();
  lv_obj_add_flag(soundSetPage, LV_OBJ_FLAG_HIDDEN);
}

void soundSetBackClickedCb(lv_event_t *e) { lv_obj_add_flag(soundSetPage, LV_OBJ_FLAG_HIDDEN); }

void settingsClickedCb(lv_event_t *e) {
  lv_obj_clean(soundSetList);

  beginSdSpi();
  int count = scanSoundSets(soundSetNames, MAX_SOUND_SETS);
  sequencerUiRestoreTouchSpi();

  if (count == 0) {
    lv_list_add_text(soundSetList, "No sets found in /Sounds/");
  }
  for (int i = 0; i < count; i++) {
    lv_obj_t *btn = lv_list_add_btn(soundSetList, LV_SYMBOL_AUDIO, soundSetNames[i]);
    lv_obj_add_event_cb(btn, soundSetItemClickedCb, LV_EVENT_CLICKED, soundSetNames[i]);
  }

  lv_obj_clear_flag(soundSetPage, LV_OBJ_FLAG_HIDDEN);
}

void patternItemClickedCb(lv_event_t *e) {
  const char *name = (const char *)lv_event_get_user_data(e);
  char path[64];
  snprintf(path, sizeof(path), "%s/%s", PATTERN_DIR, name);
  beginSdSpi();
  loadPattern(path);
  sequencerUiRestoreTouchSpi();
  sequencerUiRefreshFromState();
  lv_obj_add_flag(patternPage, LV_OBJ_FLAG_HIDDEN);
}

void patternBackClickedCb(lv_event_t *e) { lv_obj_add_flag(patternPage, LV_OBJ_FLAG_HIDDEN); }

void loadPatternClickedCb(lv_event_t *e) {
  lv_obj_clean(patternList);

  beginSdSpi();
  int count = scanPatternFiles(patternFileNames, MAX_PATTERN_FILES);
  sequencerUiRestoreTouchSpi();

  if (count == 0) {
    lv_list_add_text(patternList, "No saved patterns yet");
  }
  for (int i = 0; i < count; i++) {
    lv_obj_t *btn = lv_list_add_btn(patternList, LV_SYMBOL_FILE, patternFileNames[i]);
    lv_obj_add_event_cb(btn, patternItemClickedCb, LV_EVENT_CLICKED, patternFileNames[i]);
  }

  lv_obj_clear_flag(patternPage, LV_OBJ_FLAG_HIDDEN);
}

void createUi() {
  lv_obj_t *scr = lv_scr_act();
  // Default theme padding on the screen itself would push everything
  // aligned to (0,0) inward, clipping the top row of the matrix against
  // the visible area - make sure our absolute layout really starts at 0,0.
  lv_obj_set_style_pad_all(scr, 0, 0);

  buildStepMatrixMap();
  stepMatrix = lv_btnmatrix_create(scr);
  lv_obj_set_size(stepMatrix, DASHBOARD_WIDTH, MATRIX_HEIGHT);
  lv_obj_align(stepMatrix, LV_ALIGN_TOP_LEFT, 0, 0);
  lv_obj_set_style_pad_all(stepMatrix, 0, 0);
  lv_obj_set_style_pad_gap(stepMatrix, 1, 0);
  lv_obj_set_style_radius(stepMatrix, 0, LV_PART_ITEMS);
  lv_obj_set_style_border_width(stepMatrix, 0, LV_PART_ITEMS);
  // The container's own MAIN part can still carry a default theme border/
  // radius even with items zeroed - that eats into the content area from
  // the container edge, unevenly shrinking whichever row/column sits
  // against that edge (looked like the top row being "not fully there").
  lv_obj_set_style_radius(stepMatrix, 0, LV_PART_MAIN);
  lv_obj_set_style_border_width(stepMatrix, 0, LV_PART_MAIN);
  lv_obj_set_style_outline_width(stepMatrix, 0, LV_PART_MAIN);
  lv_btnmatrix_set_map(stepMatrix, stepMatrixMap);
  lv_btnmatrix_set_btn_ctrl_all(stepMatrix, LV_BTNMATRIX_CTRL_CHECKABLE);
  lv_obj_add_event_cb(stepMatrix, matrixValueChangedCb, LV_EVENT_VALUE_CHANGED, NULL);
  lv_obj_add_event_cb(stepMatrix, matrixDrawPartCb, LV_EVENT_DRAW_PART_BEGIN, NULL);

  int labelWidth = 70;
  int sliderWidth = DASHBOARD_WIDTH - labelWidth - 28;

  volumeLabel = lv_label_create(scr);
  lv_label_set_text_fmt(volumeLabel, "VOL %d%%", volume);
  lv_obj_align(volumeLabel, LV_ALIGN_TOP_LEFT, 4, MATRIX_HEIGHT + 8);

  volumeSlider = lv_slider_create(scr);
  lv_obj_set_size(volumeSlider, sliderWidth, ROW_H - 12);
  lv_obj_align(volumeSlider, LV_ALIGN_TOP_RIGHT, -4, MATRIX_HEIGHT + 9);
  lv_slider_set_range(volumeSlider, 0, 100);
  lv_slider_set_value(volumeSlider, volume, LV_ANIM_OFF);
  lv_obj_add_event_cb(volumeSlider, volumeChangedCb, LV_EVENT_VALUE_CHANGED, NULL);

  tempoLabel = lv_label_create(scr);
  lv_label_set_text_fmt(tempoLabel, "BPM %d", bpm);
  lv_obj_align(tempoLabel, LV_ALIGN_TOP_LEFT, 4, MATRIX_HEIGHT + ROW_H + 8);

  tempoSlider = lv_slider_create(scr);
  lv_obj_set_size(tempoSlider, sliderWidth, ROW_H - 12);
  lv_obj_align(tempoSlider, LV_ALIGN_TOP_RIGHT, -4, MATRIX_HEIGHT + ROW_H + 9);
  lv_slider_set_range(tempoSlider, 60, 200);
  lv_slider_set_value(tempoSlider, bpm, LV_ANIM_OFF);
  lv_obj_add_event_cb(tempoSlider, tempoChangedCb, LV_EVENT_VALUE_CHANGED, NULL);

  swingLabel = lv_label_create(scr);
  lv_label_set_text_fmt(swingLabel, "SWG %d%%", swing);
  lv_obj_align(swingLabel, LV_ALIGN_TOP_LEFT, 4, MATRIX_HEIGHT + 2 * ROW_H + 8);

  swingSlider = lv_slider_create(scr);
  lv_obj_set_size(swingSlider, sliderWidth, ROW_H - 12);
  lv_obj_align(swingSlider, LV_ALIGN_TOP_RIGHT, -4, MATRIX_HEIGHT + 2 * ROW_H + 9);
  lv_slider_set_range(swingSlider, 0, 100);
  lv_slider_set_value(swingSlider, swing, LV_ANIM_OFF);
  lv_obj_add_event_cb(swingSlider, swingChangedCb, LV_EVENT_VALUE_CHANGED, NULL);

  // Small square icon buttons instead of one wide text button - room for
  // more controls without eating into the grid/slider rows. 6 icons spread
  // evenly across the row.
  int iconY = MATRIX_HEIGHT + 3 * ROW_H + 3;
  int iconSize = ROW_H - 6;
  int iconXs[7] = {4, 46, 88, 130, 172, 214, 256};

  playPauseBtn = lv_btn_create(scr);
  lv_obj_set_size(playPauseBtn, iconSize, iconSize);
  lv_obj_align(playPauseBtn, LV_ALIGN_TOP_LEFT, iconXs[0], iconY);
  lv_obj_add_event_cb(playPauseBtn, playPauseClickedCb, LV_EVENT_CLICKED, NULL);
  playPauseLabel = lv_label_create(playPauseBtn);
  lv_label_set_text(playPauseLabel, running ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY);
  lv_obj_center(playPauseLabel);

  lv_obj_t *saveBtn = lv_btn_create(scr);
  lv_obj_set_size(saveBtn, iconSize, iconSize);
  lv_obj_align(saveBtn, LV_ALIGN_TOP_LEFT, iconXs[1], iconY);
  lv_obj_add_event_cb(saveBtn, saveClickedCb, LV_EVENT_CLICKED, NULL);
  lv_obj_t *saveLabel = lv_label_create(saveBtn);
  lv_label_set_text(saveLabel, LV_SYMBOL_SAVE);
  lv_obj_center(saveLabel);

  lv_obj_t *loadPatternBtn = lv_btn_create(scr);
  lv_obj_set_size(loadPatternBtn, iconSize, iconSize);
  lv_obj_align(loadPatternBtn, LV_ALIGN_TOP_LEFT, iconXs[2], iconY);
  lv_obj_add_event_cb(loadPatternBtn, loadPatternClickedCb, LV_EVENT_CLICKED, NULL);
  lv_obj_t *loadPatternLabel = lv_label_create(loadPatternBtn);
  lv_label_set_text(loadPatternLabel, LV_SYMBOL_FILE);
  lv_obj_center(loadPatternLabel);

  lv_obj_t *settingsBtn = lv_btn_create(scr);
  lv_obj_set_size(settingsBtn, iconSize, iconSize);
  lv_obj_align(settingsBtn, LV_ALIGN_TOP_LEFT, iconXs[3], iconY);
  lv_obj_add_event_cb(settingsBtn, settingsClickedCb, LV_EVENT_CLICKED, NULL);
  lv_obj_t *settingsLabel = lv_label_create(settingsBtn);
  lv_label_set_text(settingsLabel, LV_SYMBOL_SETTINGS);
  lv_obj_center(settingsLabel);

  lv_obj_t *randomizeBtn = lv_btn_create(scr);
  lv_obj_set_size(randomizeBtn, iconSize, iconSize);
  lv_obj_align(randomizeBtn, LV_ALIGN_TOP_LEFT, iconXs[4], iconY);
  lv_obj_add_event_cb(randomizeBtn, randomizeClickedCb, LV_EVENT_CLICKED, NULL);
  lv_obj_t *randomizeLabel = lv_label_create(randomizeBtn);
  lv_label_set_text(randomizeLabel, LV_SYMBOL_SHUFFLE);
  lv_obj_center(randomizeLabel);

  lv_obj_t *presetsBtn = lv_btn_create(scr);
  lv_obj_set_size(presetsBtn, iconSize, iconSize);
  lv_obj_align(presetsBtn, LV_ALIGN_TOP_LEFT, iconXs[5], iconY);
  lv_obj_add_event_cb(presetsBtn, presetsClickedCb, LV_EVENT_CLICKED, NULL);
  lv_obj_t *presetsLabel = lv_label_create(presetsBtn);
  lv_label_set_text(presetsLabel, LV_SYMBOL_LIST);
  lv_obj_center(presetsLabel);

  lv_obj_t *wifiBtn = lv_btn_create(scr);
  lv_obj_set_size(wifiBtn, iconSize, iconSize);
  lv_obj_align(wifiBtn, LV_ALIGN_TOP_LEFT, iconXs[6], iconY);
  lv_obj_add_event_cb(wifiBtn, wifiClickedCb, LV_EVENT_CLICKED, NULL);
  wifiIconLabel = lv_label_create(wifiBtn);
  lv_label_set_text(wifiIconLabel, LV_SYMBOL_WIFI);
  lv_obj_center(wifiIconLabel);

  // Sound-set picker, hidden by default - drawn on top of (and so also
  // blocks touches to) the main controls while shown.
  soundSetPage = lv_obj_create(scr);
  lv_obj_set_size(soundSetPage, DASHBOARD_WIDTH, DASHBOARD_HEIGHT);
  lv_obj_align(soundSetPage, LV_ALIGN_TOP_LEFT, 0, 0);
  lv_obj_set_style_bg_color(soundSetPage, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(soundSetPage, LV_OPA_COVER, 0);
  lv_obj_set_style_pad_all(soundSetPage, 4, 0);
  lv_obj_add_flag(soundSetPage, LV_OBJ_FLAG_HIDDEN);

  lv_obj_t *soundSetTitle = lv_label_create(soundSetPage);
  lv_label_set_text(soundSetTitle, "Sound Set");
  lv_obj_align(soundSetTitle, LV_ALIGN_TOP_MID, 0, 0);

  soundSetList = lv_list_create(soundSetPage);
  lv_obj_set_size(soundSetList, DASHBOARD_WIDTH - 16, DASHBOARD_HEIGHT - 60);
  lv_obj_align(soundSetList, LV_ALIGN_TOP_MID, 0, 24);

  lv_obj_t *soundSetBackBtn = lv_btn_create(soundSetPage);
  lv_obj_set_size(soundSetBackBtn, 100, 28);
  lv_obj_align(soundSetBackBtn, LV_ALIGN_BOTTOM_MID, 0, -4);
  lv_obj_add_event_cb(soundSetBackBtn, soundSetBackClickedCb, LV_EVENT_CLICKED, NULL);
  lv_obj_t *soundSetBackLabel = lv_label_create(soundSetBackBtn);
  lv_label_set_text(soundSetBackLabel, LV_SYMBOL_LEFT " Back");
  lv_obj_center(soundSetBackLabel);

  // Saved-pattern picker, same hidden-overlay pattern as the sound-set page.
  patternPage = lv_obj_create(scr);
  lv_obj_set_size(patternPage, DASHBOARD_WIDTH, DASHBOARD_HEIGHT);
  lv_obj_align(patternPage, LV_ALIGN_TOP_LEFT, 0, 0);
  lv_obj_set_style_bg_color(patternPage, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(patternPage, LV_OPA_COVER, 0);
  lv_obj_set_style_pad_all(patternPage, 4, 0);
  lv_obj_add_flag(patternPage, LV_OBJ_FLAG_HIDDEN);

  lv_obj_t *patternTitle = lv_label_create(patternPage);
  lv_label_set_text(patternTitle, "Load Pattern");
  lv_obj_align(patternTitle, LV_ALIGN_TOP_MID, 0, 0);

  patternList = lv_list_create(patternPage);
  lv_obj_set_size(patternList, DASHBOARD_WIDTH - 16, DASHBOARD_HEIGHT - 60);
  lv_obj_align(patternList, LV_ALIGN_TOP_MID, 0, 24);

  lv_obj_t *patternBackBtn = lv_btn_create(patternPage);
  lv_obj_set_size(patternBackBtn, 100, 28);
  lv_obj_align(patternBackBtn, LV_ALIGN_BOTTOM_MID, 0, -4);
  lv_obj_add_event_cb(patternBackBtn, patternBackClickedCb, LV_EVENT_CLICKED, NULL);
  lv_obj_t *patternBackLabel = lv_label_create(patternBackBtn);
  lv_label_set_text(patternBackLabel, LV_SYMBOL_LEFT " Back");
  lv_obj_center(patternBackLabel);

  // Genre-preset picker, same hidden-overlay pattern. Presets are fixed
  // (compiled-in), not scanned from SD, so the list is built once here.
  presetPage = lv_obj_create(scr);
  lv_obj_set_size(presetPage, DASHBOARD_WIDTH, DASHBOARD_HEIGHT);
  lv_obj_align(presetPage, LV_ALIGN_TOP_LEFT, 0, 0);
  lv_obj_set_style_bg_color(presetPage, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(presetPage, LV_OPA_COVER, 0);
  lv_obj_set_style_pad_all(presetPage, 4, 0);
  lv_obj_add_flag(presetPage, LV_OBJ_FLAG_HIDDEN);

  lv_obj_t *presetTitle = lv_label_create(presetPage);
  lv_label_set_text(presetTitle, "Genre Presets");
  lv_obj_align(presetTitle, LV_ALIGN_TOP_MID, 0, 0);

  lv_obj_t *presetList = lv_list_create(presetPage);
  lv_obj_set_size(presetList, DASHBOARD_WIDTH - 16, DASHBOARD_HEIGHT - 60);
  lv_obj_align(presetList, LV_ALIGN_TOP_MID, 0, 24);
  for (int i = 0; i < NUM_GENRE_PRESETS; i++) {
    lv_obj_t *btn = lv_list_add_btn(presetList, LV_SYMBOL_AUDIO, GENRE_PRESETS[i].name);
    lv_obj_add_event_cb(btn, presetItemClickedCb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
  }

  lv_obj_t *presetBackBtn = lv_btn_create(presetPage);
  lv_obj_set_size(presetBackBtn, 100, 28);
  lv_obj_align(presetBackBtn, LV_ALIGN_BOTTOM_MID, 0, -4);
  lv_obj_add_event_cb(presetBackBtn, presetBackClickedCb, LV_EVENT_CLICKED, NULL);
  lv_obj_t *presetBackLabel = lv_label_create(presetBackBtn);
  lv_label_set_text(presetBackLabel, LV_SYMBOL_LEFT " Back");
  lv_obj_center(presetBackLabel);

  // WiFi status page - shows AP/setup info or the connected IP, same
  // hidden-overlay pattern as the other pages.
  wifiPage = lv_obj_create(scr);
  lv_obj_set_size(wifiPage, DASHBOARD_WIDTH, DASHBOARD_HEIGHT);
  lv_obj_align(wifiPage, LV_ALIGN_TOP_LEFT, 0, 0);
  lv_obj_set_style_bg_color(wifiPage, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(wifiPage, LV_OPA_COVER, 0);
  lv_obj_set_style_pad_all(wifiPage, 4, 0);
  lv_obj_add_flag(wifiPage, LV_OBJ_FLAG_HIDDEN);

  lv_obj_t *wifiTitle = lv_label_create(wifiPage);
  lv_label_set_text(wifiTitle, "WiFi");
  lv_obj_align(wifiTitle, LV_ALIGN_TOP_MID, 0, 0);

  wifiStatusLabel = lv_label_create(wifiPage);
  lv_label_set_text(wifiStatusLabel, wifiPortalStatusText());
  lv_label_set_long_mode(wifiStatusLabel, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(wifiStatusLabel, DASHBOARD_WIDTH - 24);
  lv_obj_align(wifiStatusLabel, LV_ALIGN_TOP_MID, 0, 30);

  lv_obj_t *wifiBackBtn = lv_btn_create(wifiPage);
  lv_obj_set_size(wifiBackBtn, 100, 28);
  lv_obj_align(wifiBackBtn, LV_ALIGN_BOTTOM_MID, 0, -4);
  lv_obj_add_event_cb(wifiBackBtn, wifiBackClickedCb, LV_EVENT_CLICKED, NULL);
  lv_obj_t *wifiBackLabel = lv_label_create(wifiBackBtn);
  lv_label_set_text(wifiBackLabel, LV_SYMBOL_LEFT " Back");
  lv_obj_center(wifiBackLabel);
}

}  // namespace

void sequencerUiInit() {
  tft.init();
  tft.setRotation(DASHBOARD_ROTATION);
  tft.fillScreen(TFT_BLACK);

  SPI.begin(TOUCH_CLK, TOUCH_MISO, TOUCH_MOSI, TOUCH_CS);
  ts.begin();

  bool haveCalibration = loadTouchCalibration(&calibration);

  drawBootScreen(tft);
  bool tappedDuringBoot = false;
  uint32_t bootStart = millis();
  while (millis() - bootStart < 3000) {
    if (ts.touched()) {
      tappedDuringBoot = true;
      break;
    }
    delay(10);
  }
  if (tappedDuringBoot) {
    while (ts.touched()) delay(10);  // wait for release before the first target
  }

  if (!haveCalibration || tappedDuringBoot) {
    runTouchCalibration(tft, ts, &calibration);
  }
  Serial.printf("touch calibration: a=%f b=%f c=%f d=%f e=%f f=%f\n", calibration.a, calibration.b,
                calibration.c, calibration.d, calibration.e, calibration.f);

  lv_init();

  constexpr int BUF_LINES = 20;
  drawBuf = (lv_color_t *)malloc(DASHBOARD_WIDTH * BUF_LINES * sizeof(lv_color_t));
  lv_disp_draw_buf_init(&dispDrawBuf, drawBuf, nullptr, DASHBOARD_WIDTH * BUF_LINES);

  lv_disp_drv_init(&dispDrv);
  dispDrv.hor_res = DASHBOARD_WIDTH;
  dispDrv.ver_res = DASHBOARD_HEIGHT;
  dispDrv.flush_cb = flushCb;
  dispDrv.draw_buf = &dispDrawBuf;
  lv_disp_drv_register(&dispDrv);

  lv_indev_drv_init(&indevDrv);
  indevDrv.type = LV_INDEV_TYPE_POINTER;
  indevDrv.read_cb = touchpadReadCb;
  lv_indev_drv_register(&indevDrv);

  createUi();
  lastDrawnStep = currentStep;
}

void sequencerUiPollTouch() {
  static uint32_t lastMs = millis();
  uint32_t now = millis();
  lv_tick_inc(now - lastMs);
  lastMs = now;
  lv_timer_handler();

  if (wifiPortalGetState() != lastDrawnWifiState) {
    refreshWifiUi();
  }
}

void sequencerUiUpdatePlayhead() {
  if (currentStep == lastDrawnStep) return;
  lastDrawnStep = currentStep;
  lv_obj_invalidate(stepMatrix);
}

void sequencerUiRestoreTouchSpi() {
  SPI.end();  // see the matching comment in sd_test.cpp - begin() alone won't re-pin the bus
  SPI.begin(TOUCH_CLK, TOUCH_MISO, TOUCH_MOSI, TOUCH_CS);
}

void sequencerUiRefreshFromState() {
  lv_slider_set_value(volumeSlider, volume, LV_ANIM_OFF);
  lv_label_set_text_fmt(volumeLabel, "VOL %d%%", volume);
  lv_slider_set_value(tempoSlider, bpm, LV_ANIM_OFF);
  lv_label_set_text_fmt(tempoLabel, "BPM %d", bpm);
  lv_slider_set_value(swingSlider, swing, LV_ANIM_OFF);
  lv_label_set_text_fmt(swingLabel, "SWG %d%%", swing);
  lv_obj_invalidate(stepMatrix);
}
