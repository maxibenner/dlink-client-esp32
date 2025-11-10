#include "AudioRecorderModule.h"
#include <math.h>

// Keep these modest for simplicity/latency
// #define BUFFER_LENGTH 1024
#define BUFFER_LENGTH 1024
#define SAMPLE_RATE 16000

static int32_t i2s_buffer[BUFFER_LENGTH]; // raw 32-bit I2S container from INMP441

// temp
int16_t sBuffer[BUFFER_LENGTH];

AudioRecorderModule::AudioRecorderModule(int i2s_num, int sck_pin, int ws_pin, int sd_pin)
    : m_i2s_num(i2s_num), m_sck_pin(sck_pin), m_ws_pin(ws_pin), m_sd_pin(sd_pin) {}

void AudioRecorderModule::begin()
{
    const i2s_config_t i2s_config = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
        .sample_rate = SAMPLE_RATE,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT, // INMP441 records 24-bit inside of 32-bit frames
        .channel_format = I2S_CHANNEL_FMT_ONLY_RIGHT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = 2,
        // .dma_buf_count = 8,
        .dma_buf_len = BUFFER_LENGTH,
        .use_apll = false,
    };

    const i2s_pin_config_t pins = {I2S_PIN_NO_CHANGE, m_sck_pin, m_ws_pin, I2S_PIN_NO_CHANGE, m_sd_pin};

    i2s_driver_install((i2s_port_t)m_i2s_num, &i2s_config, 0, NULL);
    i2s_set_pin((i2s_port_t)m_i2s_num, &pins);
    i2s_start((i2s_port_t)m_i2s_num);
}

void AudioRecorderModule::plot()
{
    size_t bytesIn = 0;

    // Read a full buffer of 32-bit I2S frames
    esp_err_t result = i2s_read((i2s_port_t)m_i2s_num,
                                i2s_buffer,
                                sizeof(i2s_buffer),
                                &bytesIn,
                                portMAX_DELAY);

    if (result == ESP_OK && bytesIn >= sizeof(int32_t))
    {
        int n = bytesIn / sizeof(int32_t);

        // Downscale 24-bit MSB-aligned samples to 16-bit (tweak shift if needed)
        for (int i = 0; i < n; ++i)
        {
            // Serial.println(i2s_buffer[i]);
            // Serial.printf("[%d] 0x%08lX %ld\n", i, (unsigned long)i2s_buffer[i], (long)i2s_buffer[i]);
            // INMP441: 24-bit data left-justified in 32-bit; shift right to 16-bit range
            sBuffer[i] = (int16_t)(i2s_buffer[i] >> 11); // 8–12 is typical; pick what fits your gain
        }

        float mean = 0;
        for (int i = 0; i < n; ++i)
            mean += sBuffer[i];
        mean /= n;

        Serial.printf(">mean:%.2f,rangelimittop:%d,rangelimitbottom:%d\n", mean, 3000, -3000);

        Serial.println();
    }
}

bool AudioRecorderModule::startRecording(const char *path)
{
    if (m_is_recording)
        return true;

    // Create/overwrite file
    m_file = SPIFFS.open(path, FILE_WRITE);
    if (!m_file)
    {
        Serial.println("[WAV] Failed to open file");
        return false;
    }

    m_dataBytes = 0;
    writeWavHeader(m_file, SAMPLE_RATE, 16, 1); // placeholder sizes for now

    // Start I2S capture
    i2s_zero_dma_buffer((i2s_port_t)m_i2s_num);
    i2s_start((i2s_port_t)m_i2s_num);
    m_is_recording = true;
    return true;
}

void AudioRecorderModule::stopRecording()
{
    if (!m_is_recording)
        return;

    i2s_stop((i2s_port_t)m_i2s_num);

    // Patch WAV sizes
    finalizeWav(m_file, m_dataBytes);

    m_file.flush();
    m_file.close();
    m_is_recording = false;
}

bool AudioRecorderModule::isRecording() const noexcept { return m_is_recording; }

void AudioRecorderModule::handle()
{
    if (!m_is_recording)
        return;

    size_t bytes_read = 0;
    esp_err_t result = i2s_read((i2s_port_t)m_i2s_num,
                                (void *)i2s_buffer,
                                sizeof(i2s_buffer),
                                &bytes_read,
                                0 /* non-blocking; we call often */);

    if (result != ESP_OK || bytes_read == 0)
    {
        return;
    }

    const int n = bytes_read / sizeof(int32_t);

    // Convert 24-bit mic data (in 32-bit container) to signed 16-bit PCM
    // Many boards present data left-justified in 24 bits. Using >> 14 here
    // gives a decent level (empirically similar to your earlier logging).
    static int16_t pcm16[BUFFER_LENGTH];

    for (int i = 0; i < n; ++i)
    {
        int32_t s = i2s_buffer[i] >> 14; // scale down to ~18-bit then clipped to 16
        if (s > 32767)                   // ??????
            s = 32767;
        if (s < -32768)
            s = -32768;
        pcm16[i] = (int16_t)s;
    }

    size_t toWrite = n * sizeof(int16_t);
    size_t wrote = m_file.write((uint8_t *)pcm16, toWrite);
    m_dataBytes += wrote;
}

// -------------------- WAV helpers --------------------
// Simple 44-byte PCM WAV header. We'll patch sizes on stop.
void AudioRecorderModule::writeWavHeader(fs::File &f, uint32_t sampleRate, uint16_t bits, uint16_t channels)
{
    struct WAVHeader
    {
        char riff[4];           // "RIFF"
        uint32_t chunkSize;     // 36 + Subchunk2Size (to be filled)
        char wave[4];           // "WAVE"
        char fmt[4];            // "fmt "
        uint32_t subchunk1Size; // 16 for PCM
        uint16_t audioFormat;   // 1 = PCM
        uint16_t numChannels;   // 1
        uint32_t sampleRate;    // 16000
        uint32_t byteRate;      // sampleRate * numChannels * bits/8
        uint16_t blockAlign;    // numChannels * bits/8
        uint16_t bitsPerSample; // 16
        char data[4];           // "data"
        uint32_t subchunk2Size; // data bytes (to be filled)
    } __attribute__((packed));

    WAVHeader h;
    memcpy(h.riff, "RIFF", 4);
    h.chunkSize = 36; // placeholder (will add data size at the end)
    memcpy(h.wave, "WAVE", 4);
    memcpy(h.fmt, "fmt ", 4);
    h.subchunk1Size = 16;
    h.audioFormat = 1;
    h.numChannels = channels;
    h.sampleRate = sampleRate;
    h.bitsPerSample = bits;
    h.byteRate = sampleRate * channels * (bits / 8);
    h.blockAlign = channels * (bits / 8);
    memcpy(h.data, "data", 4);
    h.subchunk2Size = 0; // placeholder

    f.write((uint8_t *)&h, sizeof(h));
}

void AudioRecorderModule::finalizeWav(fs::File &f, uint32_t dataBytes)
{
    // Patch RIFF chunkSize
    uint32_t chunkSize = 36 + dataBytes;
    f.seek(4, SeekSet);
    f.write((uint8_t *)&chunkSize, sizeof(chunkSize));

    // Patch data subchunk size
    f.seek(40, SeekSet);
    f.write((uint8_t *)&dataBytes, sizeof(dataBytes));
}
