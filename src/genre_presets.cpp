#include "genre_presets.h"

// Track order matches TRACK_SAMPLE_FILENAMES in audio_engine.cpp:
// 0=kick, 1=snare, 2=hihat, 3=tom, 4=clap, 5=cymbal.
// clang-format off
const GenrePreset GENRE_PRESETS[NUM_GENRE_PRESETS] = {
  {
    "Techno (4-on-floor)",
    {
      {1,0,0,0, 1,0,0,0, 1,0,0,0, 1,0,0,0},  // kick: every quarter
      {0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0},  // snare
      {0,0,1,0, 0,0,1,0, 0,0,1,0, 0,0,1,0},  // hihat: offbeat 8ths
      {0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0},  // tom
      {0,0,0,0, 1,0,0,0, 0,0,0,0, 1,0,0,0},  // clap: backbeat accent
      {0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,1},  // cymbal: end accent
    },
  },
  {
    "Hip-Hop (Boom Bap)",
    {
      {1,0,0,0, 0,0,1,0, 0,0,1,0, 0,0,0,0},  // kick: syncopated
      {0,0,0,0, 1,0,0,0, 0,0,0,0, 1,0,0,0},  // snare: backbeat
      {1,0,1,0, 1,0,1,0, 1,0,1,0, 1,0,1,0},  // hihat: straight 8ths
      {0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0},  // tom
      {0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0},  // clap
      {0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0},  // cymbal
    },
  },
  {
    "Trap",
    {
      {1,0,0,1, 0,0,0,0, 0,0,1,0, 0,0,0,0},  // kick: 808-style syncopation
      {0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0},  // snare
      {1,0,1,0, 1,0,1,1, 1,0,1,0, 1,0,1,1},  // hihat: rolls at end of each half
      {0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0},  // tom
      {0,0,0,0, 1,0,0,0, 0,0,0,0, 1,0,0,0},  // clap: backbeat
      {1,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0},  // cymbal: intro accent
    },
  },
  {
    "Electro-Funk",
    {
      {1,0,0,1, 0,0,1,0, 0,0,0,1, 0,0,1,0},  // kick: funky syncopation
      {0,0,0,0, 1,0,0,0, 0,0,0,0, 1,0,0,0},  // snare: backbeat
      {1,1,0,1, 1,1,0,1, 1,1,0,1, 1,1,0,1},  // hihat: funky 16ths, skip every 3rd
      {0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,1},  // tom: fill at the end
      {0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0},  // clap
      {0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0},  // cymbal
    },
  },
};
// clang-format on

bool loadGenrePreset(int index) {
  if (index < 0 || index >= NUM_GENRE_PRESETS) return false;
  for (int t = 0; t < NUM_TRACKS; t++) {
    for (int s = 0; s < NUM_STEPS; s++) {
      trackMatrix[t][s] = GENRE_PRESETS[index].pattern[t][s];
    }
  }
  return true;
}
