#pragma once

// Hardware/profile defines for CYD 2.8" (ESP32-2432S028R).
// TFT pin/driver config copied verbatim from the sibling claude-mascot-cyd
// project, already verified against real hardware there (ILI9341, rotation
// 3, TFT_INVERSION_ON). Do not re-guess this if it ever needs changing -
// copy from there instead.

// App layout
#define DASHBOARD_WIDTH 320
#define DASHBOARD_HEIGHT 240
#define DASHBOARD_ROTATION 3

// TFT_eSPI profile
#define USER_SETUP_LOADED 1
#define ILI9341_2_DRIVER 1
#define USE_HSPI_PORT 1
#define TFT_WIDTH 240
#define TFT_HEIGHT 320
#define TFT_INVERSION_ON 1

#define TFT_MISO 12
#define TFT_MOSI 13
#define TFT_SCLK 14
#define TFT_CS 15
#define TFT_DC 2
#define TFT_RST -1
#define TFT_BL 21
#define TFT_BACKLIGHT_ON 1

#define SPI_FREQUENCY 40000000
#define SPI_READ_FREQUENCY 16000000

// Fonts
#define LOAD_GLCD 1
#define LOAD_FONT2 1
#define LOAD_FONT4 1
#define LOAD_FONT6 1
#define LOAD_FONT7 1
#define LOAD_FONT8 1
#define LOAD_GFXFF 1
#define SMOOTH_FONT 1

// Touch (XPT2046), on its own bit-banged/software SPI via the ESP32 GPIO
// matrix - standard pinout for this exact board model, not the TFT's bus.
#define TOUCH_CLK 25
#define TOUCH_CS 33
#define TOUCH_MOSI 32
#define TOUCH_MISO 39
#define TOUCH_IRQ 36

#define TOUCH_MIN_PRESSURE 400

// No fixed raw-to-screen mapping here on purpose: this board's touch axes
// don't line up with the display axes in any single fixed way we could get
// right by guessing (already burned two guesses on swap/invert flags).
// touch_calibration.{h,cpp} solves a general affine transform from 3
// on-screen taps instead and persists it to NVS.

// SD card is on its own separate SPI bus (the default VSPI pins), NOT the
// TFT's HSPI bus - confirmed against the Bruce firmware's board definition
// for this exact board (boards/CYD-2432S028/CYD-2432S028.ini upstream:
// https://github.com/pr3y/Bruce), after our first assumption (shared TFT
// bus) produced "Select Failed" on real hardware. This bus is shared with
// the touch controller's SPI (same VSPI peripheral, different pin mux) -
// see sequencerUiRestoreTouchSpi() for why that matters.
#define SD_SCLK 18
#define SD_MISO 19
#define SD_MOSI 23
#define SD_CS 5

// Internal 8-bit DAC output, feeds the board's onboard mono amp (CN1/Speaker
// connector). Swap for an external I2S DAC (e.g. PCM5102A) once the
// sequencer itself works.
#define AUDIO_DAC_PIN 26
