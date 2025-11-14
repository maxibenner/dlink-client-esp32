#include "SpeakerModule.h"

// Match recorder defaults
#define PLAY_SAMPLE_RATE 16000

SpeakerModule::SpeakerModule(int i2s_num, int bck_pin, int ws_pin, int data_pin)
    : m_i2s_num(i2s_num), m_bck_pin(bck_pin), m_ws_pin(ws_pin), m_data_pin(data_pin) {}

void SpeakerModule::begin()
{
    const i2s_config_t i2s_config = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
        .sample_rate = PLAY_SAMPLE_RATE,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT, // we will send 16-bit stereo frames
        .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT, // interleaved stereo
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = 4,
        .dma_buf_len = 64};

    i2s_driver_install((i2s_port_t)m_i2s_num, &i2s_config, 0, NULL);

    const i2s_pin_config_t pins = {
        .bck_io_num = m_bck_pin,
        .ws_io_num = m_ws_pin,
        .data_out_num = m_data_pin,
        .data_in_num = I2S_PIN_NO_CHANGE};
    i2s_set_pin((i2s_port_t)m_i2s_num, &pins);
}

static bool readAndValidateWavHeader(fs::File &f, uint32_t &dataOffset, uint32_t &dataSize,
                                     uint32_t &sampleRate, uint16_t &bitsPerSample, uint16_t &channels)
{
    if (!f)
        return false;
    if (f.size() < 44)
        return false;

    // Read minimal 44B header
    uint8_t h[44];
    if (f.read(h, 44) != 44)
        return false;

    auto rd32 = [&](int idx) -> uint32_t
    {
        return (uint32_t)h[idx] | ((uint32_t)h[idx + 1] << 8) | ((uint32_t)h[idx + 2] << 16) | ((uint32_t)h[idx + 3] << 24);
    };
    auto rd16 = [&](int idx) -> uint16_t
    { return (uint16_t)h[idx] | ((uint16_t)h[idx + 1] << 8); };

    if (memcmp(&h[0], "RIFF", 4) != 0 || memcmp(&h[8], "WAVE", 4) != 0)
        return false;
    if (memcmp(&h[12], "fmt ", 4) != 0)
        return false;
    uint16_t audioFormat = rd16(20);
    channels = rd16(22);
    sampleRate = rd32(24);
    bitsPerSample = rd16(34);
    if (audioFormat != 1)
        return false; // PCM only

    // We wrote a canonical header, so assume data chunk immediately follows
    if (memcmp(&h[36], "data", 4) != 0)
        return false;
    dataSize = rd32(40);
    dataOffset = 44;
    return true;
}

bool SpeakerModule::playFile(const char *path)
{
    fs::File f = SPIFFS.open(path, FILE_READ);
    if (!f)
    {
        Serial.println("[PLAY] Failed to open file");
        return false;
    }

    uint32_t dataOffset = 0, dataSize = 0, sampleRate = 0;
    uint16_t bps = 0, ch = 0;
    if (!readAndValidateWavHeader(f, dataOffset, dataSize, sampleRate, bps, ch))
    {
        Serial.println("[PLAY] Not a supported WAV (expect 16-bit PCM)");
        f.close();
        return false;
    }

    // Reconfigure sample rate if header differs
    i2s_set_sample_rates((i2s_port_t)m_i2s_num, sampleRate);

    // Seek to data and play
    f.seek(dataOffset, SeekSet);
    const size_t MONO_SAMPLES = 512; // mono samples per read
    static int16_t mono[MONO_SAMPLES];
    static int16_t stereo[MONO_SAMPLES * 2];

    i2s_zero_dma_buffer((i2s_port_t)m_i2s_num);
    i2s_start((i2s_port_t)m_i2s_num);

    size_t remaining = dataSize;
    while (remaining > 0)
    {
        size_t toReadBytes = min(remaining, MONO_SAMPLES * sizeof(int16_t));
        size_t got = f.read((uint8_t *)mono, toReadBytes);
        if (got == 0)
            break;

        size_t samples = got / sizeof(int16_t);

        // If source is mono, duplicate to both L/R. If it's stereo, you could bypass this copy.
        if (ch == 1)
        {
            for (size_t i = 0, j = 0; i < samples; ++i)
            {
                int16_t s = mono[i];
                stereo[j++] = s; // Right
                stereo[j++] = s; // Left
            }
        }
        else
        {
            // If recorded/stored as stereo 16-bit, just read straight through.
            // Here we only handle mono for simplicity; real stereo would read directly to `stereo`.
            for (size_t i = 0, j = 0; i < samples; i += 2)
            {
                stereo[j++] = mono[i];     // Right (first)
                stereo[j++] = mono[i + 1]; // Left (second)
            }
        }

        size_t bytesToWrite = samples * 2 * sizeof(int16_t); // stereo
        size_t wrote = 0;
        i2s_write((i2s_port_t)m_i2s_num, (const void *)stereo, bytesToWrite, &wrote, portMAX_DELAY);

        if (wrote < bytesToWrite)
            break; // underrun/stop
        remaining -= got;
    }

    i2s_stop((i2s_port_t)m_i2s_num);
    f.close();
    return true;
}