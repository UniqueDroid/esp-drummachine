#pragma once

#include <cstdint>

// Loads one 8-bit/mono WAV file fully into a malloc'd buffer of signed
// samples (raw byte - 128), for direct use in the audio mixer. Returns
// false (leaving *outData untouched) if the file is missing or not in the
// expected format - callers should keep using the placeholder tone for
// that track in that case.
bool loadWavSample(const char *path, int8_t **outData, uint32_t *outLength);
