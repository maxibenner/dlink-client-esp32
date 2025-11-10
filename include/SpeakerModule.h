#pragma once
#include <Arduino.h>
#include <FS.h>
#include <SPIFFS.h>
#include "driver/i2s.h"

class SpeakerModule {
public:
  SpeakerModule(int i2s_num, int bck_pin, int ws_pin, int data_pin);
  void begin();
  bool playFile(const char* path); // blocking playback; returns when finished

private:
  const int m_i2s_num;
  const int m_bck_pin, m_ws_pin, m_data_pin;
};