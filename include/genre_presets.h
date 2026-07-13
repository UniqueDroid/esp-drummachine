#pragma once

#include "globals.h"

struct GenrePreset {
  const char *name;
  bool pattern[NUM_TRACKS][NUM_STEPS];
};

constexpr int NUM_GENRE_PRESETS = 4;
extern const GenrePreset GENRE_PRESETS[NUM_GENRE_PRESETS];

// Copies a preset's pattern into trackMatrix. Returns false if index is
// out of range.
bool loadGenrePreset(int index);
