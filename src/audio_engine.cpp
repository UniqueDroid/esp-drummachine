#include "audio_engine.h"
#include "globals.h"
#include "sd_samples.h"

#include "driver/dac_common.h"
#include "esp_timer.h"
#include "soc/dac_channel.h"

namespace {

constexpr uint32_t SAMPLE_RATE = 11025;
constexpr int16_t START_AMP = 12000;
constexpr float DECAY_SECONDS = 0.15f;

// Placeholder pitches per track (kick..hihat-ish, low to high), used only
// for tracks whose WAV sample failed to load from SD.
constexpr float TRACK_FREQS_HZ[NUM_TRACKS] = {60, 150, 400, 800, 1500, 3000};

// Fixed filenames within a set folder - only the /Sounds/<set>/ prefix
// changes when switching sample sets.
const char *TRACK_SAMPLE_FILENAMES[NUM_TRACKS] = {
    "kick.wav", "snare.wav", "hihat.wav", "tom.wav", "clap.wav", "cymbal.wav",
};

char currentSampleSet[32] = "Set1";

int8_t sineTable[256];

struct TrackSample {
  int8_t *data;
  uint32_t length;
  bool loaded;
};
TrackSample trackSamples[NUM_TRACKS];

struct Voice {
  bool active;
  uint32_t phaseAcc;
  uint32_t phaseInc;
  int32_t amp;
  int32_t decayStep;
  uint32_t samplePos;
};

Voice voices[NUM_TRACKS];
dac_channel_t audioDacChannel;
esp_timer_handle_t audioTimer;

// Set while swapping sample buffers at runtime (sound-set switch) so the
// audio callback doesn't read a track's sample data mid-free/mid-reload.
volatile bool audioMuted = false;

void triggerVoice(int track) {
  Voice &v = voices[track];
  v.samplePos = 0;
  if (!trackSamples[track].loaded) {
    v.phaseAcc = 0;
    v.phaseInc = (uint32_t)(((uint64_t)(TRACK_FREQS_HZ[track] * (1ULL << 32))) / SAMPLE_RATE);
    v.amp = START_AMP;
    v.decayStep = (int32_t)(START_AMP / (SAMPLE_RATE * DECAY_SECONDS));
  }
  v.active = true;
}

// esp_timer callbacks run in the "esp_timer" FreeRTOS task by default (task
// context, not a hardware ISR) - safe to call regular driver code from here,
// unlike the hw_timer ISR we started with (which crashed on hardware:
// "Guru Meditation Error: LoadProhibited" - a driver call raced with a
// flash-cache-disabling operation elsewhere).
//
// This replaces an earlier I2S-based attempt (I2S_MODE_DAC_BUILT_IN) too:
// that one reconfigures the whole built-in-DAC subsystem even though only
// one channel is "enabled", which on this board disturbed GPIO25 - the same
// pin used as the touch controller's SPI clock (TOUCH_CLK). dac_output_*()
// only ever touches the one channel we pass in, so it leaves GPIO25 alone.
void onAudioSample(void *) {
  if (audioMuted) {
    dac_output_voltage(audioDacChannel, 128);
    return;
  }

  static uint32_t sampleCounter = 0;
  static uint32_t stepIntervalSamples = SAMPLE_RATE * 60 / 120 / 4;

  if (running) {
    sampleCounter++;
    if (sampleCounter >= stepIntervalSamples) {
      sampleCounter = 0;
      currentStep = (currentStep + 1) % NUM_STEPS;
      for (int t = 0; t < NUM_TRACKS; t++) {
        if (trackMatrix[t][currentStep]) {
          triggerVoice(t);
        }
      }
      int safeBpm = bpm;
      if (safeBpm < 30) safeBpm = 30;
      if (safeBpm > 300) safeBpm = 300;
      uint32_t baseInterval = (uint32_t)((uint64_t)SAMPLE_RATE * 60 / safeBpm / 4);

      // Swing: keep each pair of 16th notes (even+odd step) at a constant
      // total duration (so tempo/bar length is unaffected), but split it
      // unevenly - the odd step lands later, giving a shuffled feel.
      int safeSwing = swing;
      if (safeSwing < 0) safeSwing = 0;
      if (safeSwing > 100) safeSwing = 100;
      float firstFrac = 0.5f + safeSwing / 400.0f;  // 0.5 (straight) .. 0.75
      uint32_t pairDuration = 2 * baseInterval;
      if (currentStep % 2 == 0) {
        stepIntervalSamples = (uint32_t)(pairDuration * firstFrac);
      } else {
        stepIntervalSamples = (uint32_t)(pairDuration * (1.0f - firstFrac));
      }
    }
  }

  int32_t mix = 0;
  for (int t = 0; t < NUM_TRACKS; t++) {
    Voice &v = voices[t];
    if (!v.active) continue;

    if (trackSamples[t].loaded) {
      const TrackSample &s = trackSamples[t];
      if (v.samplePos >= s.length) {
        v.active = false;
        continue;
      }
      mix += s.data[v.samplePos];
      v.samplePos++;
    } else {
      uint8_t idx = v.phaseAcc >> 24;
      mix += ((int32_t)sineTable[idx] * v.amp) >> 12;
      v.phaseAcc += v.phaseInc;
      v.amp -= v.decayStep;
      if (v.amp <= 0) v.active = false;
    }
  }

  if (mix > 127) mix = 127;
  if (mix < -127) mix = -127;
  mix = (mix * volume) / 100;
  dac_output_voltage(audioDacChannel, (uint8_t)(mix + 128));
}

void loadSampleSetInternal(const char *setName) {
  for (int t = 0; t < NUM_TRACKS; t++) {
    if (trackSamples[t].loaded) {
      free(trackSamples[t].data);
      trackSamples[t].data = nullptr;
      trackSamples[t].loaded = false;
    }
    char path[80];
    snprintf(path, sizeof(path), "/Sounds/%s/%s", setName, TRACK_SAMPLE_FILENAMES[t]);
    trackSamples[t].loaded = loadWavSample(path, &trackSamples[t].data, &trackSamples[t].length);
  }
}

}  // namespace

void audioEngineInit() {
  for (int i = 0; i < 256; i++) {
    sineTable[i] = (int8_t)(sinf(2.0f * PI * i / 256.0f) * 127.0f);
  }
  for (int t = 0; t < NUM_TRACKS; t++) {
    voices[t] = {};
  }
  loadSampleSetInternal(currentSampleSet);

  audioDacChannel = (dac_channel_t)(AUDIO_DAC_PIN - DAC_CHANNEL_1_GPIO_NUM);
  dac_output_enable(audioDacChannel);

  esp_timer_create_args_t timerArgs = {};
  timerArgs.callback = &onAudioSample;
  timerArgs.name = "audio_sample";
  esp_timer_create(&timerArgs, &audioTimer);
  esp_timer_start_periodic(audioTimer, 1000000 / SAMPLE_RATE);
}

void audioEngineLoadSampleSet(const char *setName) {
  audioMuted = true;
  delay(5);  // let the audio callback observe the mute flag before we free buffers
  for (int t = 0; t < NUM_TRACKS; t++) {
    voices[t].active = false;  // nothing left pointing at soon-to-be-freed sample data
  }
  loadSampleSetInternal(setName);
  strncpy(currentSampleSet, setName, sizeof(currentSampleSet) - 1);
  currentSampleSet[sizeof(currentSampleSet) - 1] = '\0';
  audioMuted = false;
}

const char *audioEngineCurrentSampleSet() { return currentSampleSet; }
