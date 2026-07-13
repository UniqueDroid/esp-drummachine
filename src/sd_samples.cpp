#include "sd_samples.h"

#include <SD.h>

#include <cstring>

namespace {

bool readExact(File &f, void *buf, size_t len) { return f.read((uint8_t *)buf, len) == (int)len; }

}  // namespace

bool loadWavSample(const char *path, int8_t **outData, uint32_t *outLength) {
  File f = SD.open(path);
  if (!f) {
    Serial.printf("sample %s: not found on SD\n", path);
    return false;
  }

  char riffId[4], waveId[4];
  uint32_t riffSize;
  if (!readExact(f, riffId, 4) || memcmp(riffId, "RIFF", 4) != 0 || !readExact(f, &riffSize, 4) ||
      !readExact(f, waveId, 4) || memcmp(waveId, "WAVE", 4) != 0) {
    Serial.printf("sample %s: not a RIFF/WAVE file\n", path);
    f.close();
    return false;
  }

  uint16_t numChannels = 0, bitsPerSample = 0;
  uint32_t sampleRate = 0;
  bool fmtFound = false;
  uint32_t dataSize = 0;
  bool dataFound = false;

  while (f.available() >= 8) {
    char chunkId[4];
    uint32_t chunkSize;
    if (!readExact(f, chunkId, 4) || !readExact(f, &chunkSize, 4)) break;

    if (memcmp(chunkId, "fmt ", 4) == 0) {
      uint16_t audioFormat;
      uint32_t byteRate;
      uint16_t blockAlign;
      readExact(f, &audioFormat, 2);
      readExact(f, &numChannels, 2);
      readExact(f, &sampleRate, 4);
      readExact(f, &byteRate, 4);
      readExact(f, &blockAlign, 2);
      readExact(f, &bitsPerSample, 2);
      if (chunkSize > 16) f.seek(f.position() + (chunkSize - 16));
      fmtFound = true;
    } else if (memcmp(chunkId, "data", 4) == 0) {
      dataSize = chunkSize;
      dataFound = true;
      break;  // we only need the PCM data, ignore anything after
    } else {
      f.seek(f.position() + chunkSize);  // skip unknown chunk (LIST, etc.)
    }
    if (chunkSize % 2 == 1) f.seek(f.position() + 1);  // chunks are word-aligned
  }

  if (!fmtFound || !dataFound || dataSize == 0) {
    Serial.printf("sample %s: no usable fmt/data chunk\n", path);
    f.close();
    return false;
  }
  if (numChannels != 1 || bitsPerSample != 8) {
    Serial.printf("sample %s: unsupported format (channels=%u bits=%u), need mono 8-bit\n", path,
                  numChannels, bitsPerSample);
    f.close();
    return false;
  }

  int8_t *buf = (int8_t *)malloc(dataSize);
  if (!buf) {
    Serial.printf("sample %s: out of memory (%u bytes)\n", path, dataSize);
    f.close();
    return false;
  }
  if (!readExact(f, buf, dataSize)) {
    Serial.printf("sample %s: short read\n", path);
    free(buf);
    f.close();
    return false;
  }
  f.close();

  for (uint32_t i = 0; i < dataSize; i++) {
    buf[i] = (int8_t)((int)(uint8_t)buf[i] - 128);
  }

  Serial.printf("sample %s: loaded %u bytes @ %u Hz\n", path, dataSize, sampleRate);
  *outData = buf;
  *outLength = dataSize;
  return true;
}
