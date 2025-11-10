#pragma once
#include <atomic>
#include <Arduino.h>
#include <FS.h>
#include <SPIFFS.h>
#include "driver/i2s.h"

class AudioRecorderModule {
public:
  AudioRecorderModule(int i2s_num, int sck_pin, int ws_pin, int sd_pin);

  void begin();
  bool startRecording(const char* path); // returns true on success
  void stopRecording();
  bool isRecording() const noexcept;
  void handle(); // pump samples to storage (call often while recording)
  void plot();

private:
  // WAV helpers
  void writeWavHeader(fs::File &f, uint32_t sampleRate, uint16_t bits, uint16_t channels);
  void finalizeWav(fs::File &f, uint32_t dataBytes);

  const int m_i2s_num;
  const int m_sck_pin, m_ws_pin, m_sd_pin;
  std::atomic<bool> m_is_recording{false};

  File m_file;
  uint32_t m_dataBytes = 0; // how many bytes of PCM have been written
};