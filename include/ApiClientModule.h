#pragma once
#include <Arduino.h>
#include <FS.h>
#include <SPIFFS.h>
#include <HTTPClient.h>
#include "driver/i2s.h"

// 16-bit mono PCM WAV header
struct WavHeader
{
    char riff[4] = {'R', 'I', 'F', 'F'};
    uint32_t chunkSize = 0;
    char wave[4] = {'W', 'A', 'V', 'E'};
    char fmt[4] = {'f', 'm', 't', ' '};
    uint32_t subchunk1Size = 16;
    uint16_t audioFormat = 1;
    uint16_t numChannels = 1;
    uint32_t sampleRate = 16000;
    uint32_t byteRate = 32000; // sampleRate * numChannels * (bitsPerSample/8)
    uint16_t blockAlign = 2;   // numChannels * (bitsPerSample/8)
    uint16_t bitsPerSample = 16;
    char data[4] = {'d', 'a', 't', 'a'};
    uint32_t subchunk2Size = 0;
};

class ApiClientModule
{
public:
    ApiClientModule(int i2s_num, int sck_pin, int ws_pin, int sd_pin,
                    uint32_t sampleRate, size_t chunkSamples,
                    uint32_t maxSeconds, const char *outPath);

    void begin();
    void start();
    void stop();
    void setInboxPath(const char *path);
    bool checkInbox();
    bool upload();

private:
    // File/WAV helpers
    void writeWavHeader(File &f, uint32_t numSamples);
    void flushChunk();

    // Task + processing
    static void readerTaskThunk(void *arg);
    void readerTask(); // runs on its own core
    void processChunk(int32_t *i2sBuf, size_t samples);

    // Configuration
    const int m_i2s_num;
    const int m_sck_pin, m_ws_pin, m_sd_pin;
    const uint32_t m_sampleRate;
    const size_t m_chunkSamples;
    const uint32_t m_maxSeconds;
    const char *m_outPath;

    // Runtime
    const char *m_inboxPath = nullptr;
    uint32_t m_samplePeriod = 0;

    volatile bool m_isRecording = false;
    uint32_t m_startMillis = 0;

    File m_file;
    int16_t *m_buf = nullptr;
    size_t m_bufIdx = 0;
    uint32_t m_totalSamples = 0;

    volatile bool m_stopRequested = false; // ask reader to finish & drain
    TaskHandle_t m_readerTask = nullptr;   // already present
    TaskHandle_t m_waiterTask = nullptr;   // who’s waiting for the drain to finish?
};
