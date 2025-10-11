#include "RecordingModule.h"
#include "secrets.h"

RecordingModule::RecordingModule(int i2s_num,
                                 int sck_pin,
                                 int ws_pin,
                                 int sd_pin,
                                 uint32_t sampleRate,
                                 size_t chunkSamples,
                                 uint32_t maxSeconds,
                                 const char *outPath)
    : m_i2s_num(i2s_num),
      m_sck_pin(sck_pin),
      m_ws_pin(ws_pin),
      m_sd_pin(sd_pin),
      m_sampleRate(sampleRate),
      m_chunkSamples(chunkSamples),
      m_maxSeconds(maxSeconds),
      m_outPath(outPath)
{
    m_samplePeriod = 1000000UL / m_sampleRate;
    m_buf = new int16_t[m_chunkSamples];
}

void RecordingModule::begin()
{
    i2s_config_t cfg = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
        .sample_rate = m_sampleRate,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
        .channel_format = I2S_CHANNEL_FMT_ONLY_RIGHT, // flipped because of bug in the library
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = 12,
        .dma_buf_len = 1024,
        .tx_desc_auto_clear = false,
        .fixed_mclk = 0};

    i2s_pin_config_t pins = {
        .bck_io_num = m_sck_pin,
        .ws_io_num = m_ws_pin,
        .data_out_num = I2S_PIN_NO_CHANGE,
        .data_in_num = m_sd_pin};

    i2s_driver_install((i2s_port_t)m_i2s_num, &cfg, 0, nullptr);
    i2s_set_pin((i2s_port_t)m_i2s_num, &pins);

    // Ensure exact mono/32-bit/16kHz clock setup
    i2s_set_clk((i2s_port_t)m_i2s_num, m_sampleRate,
                I2S_BITS_PER_SAMPLE_32BIT, I2S_CHANNEL_MONO);

    Serial.println("I2S initialized for digital mic (mono/right)");

    // Spawn a dedicated high-priority reader task
    // Core notes: Arduino loop runs on core 1. Wi-Fi often on core 0.
    // Run audio on core 0 with high priority to avoid starvation.
    xTaskCreatePinnedToCore(
        &RecordingModule::readerTaskThunk,
        "i2s_reader",
        4096, // stack
        this, // arg
        20,   // priority
        &m_readerTask,
        0); // core 0
}

void RecordingModule::setUploadPath(const char *path)
{
    m_uploadPath = path;
}

void RecordingModule::writeWavHeader(File &f, uint32_t numSamples)
{
    WavHeader h;
    h.sampleRate = m_sampleRate;
    h.byteRate = m_sampleRate * 2; // mono, 16-bit
    h.blockAlign = 2;
    h.bitsPerSample = 16;
    h.subchunk2Size = numSamples * 2;
    h.chunkSize = 36 + h.subchunk2Size;

    f.seek(0);
    f.write(reinterpret_cast<uint8_t *>(&h), sizeof(WavHeader));
}

void RecordingModule::flushChunk()
{
    if (m_bufIdx == 0 || !m_file)
        return;
    m_file.write(reinterpret_cast<uint8_t *>(m_buf), m_bufIdx * sizeof(int16_t));
    m_bufIdx = 0;
}

void RecordingModule::start()
{
    if (m_isRecording)
        return;

    if (SPIFFS.exists(m_outPath))
        SPIFFS.remove(m_outPath);
    m_file = SPIFFS.open(m_outPath, FILE_WRITE);
    if (!m_file)
    {
        Serial.println("Failed to open output WAV for writing");
        return;
    }

    // Reserve space for WAV header
    WavHeader blank;
    m_file.write(reinterpret_cast<uint8_t *>(&blank), sizeof(WavHeader));

    m_totalSamples = 0;
    m_bufIdx = 0;
    m_startMillis = millis();

    // RESET I2S/DMA STATE FOR A FRESH TAKE
    i2s_zero_dma_buffer((i2s_port_t)m_i2s_num);
    // Re-assert exact clocking each time (avoids drift after Wi-Fi)
    i2s_set_clk((i2s_port_t)m_i2s_num, m_sampleRate,
                I2S_BITS_PER_SAMPLE_32BIT, I2S_CHANNEL_MONO);
    i2s_start((i2s_port_t)m_i2s_num);

    // Let the reader task start consuming
    m_isRecording = true;

    Serial.println("Recording started");
}

void RecordingModule::stop()
{
    if (!m_isRecording)
        return;

    // Ask the reader to drain and remember who’s waiting
    m_waiterTask = xTaskGetCurrentTaskHandle();
    m_stopRequested = true;

    // Wait (with a timeout) for the reader to say “drained”
    const uint32_t notified = ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(150));
    if (notified == 0)
    {
        Serial.println("Drain timeout; proceeding cautiously");
        // Worst case: try one last app-level flush
        flushChunk();
    }

    // Now the reader is no longer touching I2S or the file. Safe to stop DMA.
    i2s_stop((i2s_port_t)m_i2s_num);
    i2s_zero_dma_buffer((i2s_port_t)m_i2s_num);

    // Finalize file
    if (m_file)
    {
        m_file.flush();
        writeWavHeader(m_file, m_totalSamples);
        m_file.close();
    }

    Serial.printf("Recording stopped. Samples: %u (~%.2fs)\n",
                  m_totalSamples, m_totalSamples / float(m_sampleRate));

    m_waiterTask = nullptr;
}

void RecordingModule::readerTaskThunk(void *arg)
{
    static_cast<RecordingModule *>(arg)->readerTask();
}

void RecordingModule::readerTask()
{
    int32_t *i2sBuf = (int32_t *)heap_caps_malloc(sizeof(int32_t) * 1024, MALLOC_CAP_8BIT);
    if (!i2sBuf)
    {
        Serial.println("Failed to alloc i2sBuf");
        vTaskDelete(nullptr);
        return;
    }

    for (;;)
    {
        if (!m_isRecording)
        {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        // Normal blocking read while recording
        size_t bytesRead = 0;
        esp_err_t err = i2s_read((i2s_port_t)m_i2s_num, (void *)i2sBuf,
                                 sizeof(int32_t) * 1024, &bytesRead, portMAX_DELAY);
        if (err == ESP_OK && bytesRead > 0)
        {
            processChunk(i2sBuf, bytesRead / sizeof(int32_t));
        }

        // If a stop was requested, switch to a bounded non-blocking drain
        if (m_stopRequested)
        {
            const uint32_t deadline = millis() + 50; // ~50ms to slurp residual DMA
            do
            {
                bytesRead = 0;
                err = i2s_read((i2s_port_t)m_i2s_num, (void *)i2sBuf,
                               sizeof(int32_t) * 1024, &bytesRead, pdMS_TO_TICKS(2));
                if (err != ESP_OK || bytesRead == 0)
                    break;
                processChunk(i2sBuf, bytesRead / sizeof(int32_t));
            } while (millis() < deadline);

            // Final app-buffer flush to file
            flushChunk();
            // Tell the waiter we’re fully drained
            if (m_waiterTask)
                xTaskNotifyGive(m_waiterTask);

            // Park here until a new start() happens
            m_isRecording = false;
            m_stopRequested = false;
        }
    }
}

static inline int16_t to_int16_from_i2s32(int32_t s32)
{
    // s32 is sign-extended 24-bit audio in the top bits of a 32-bit word.
    // Shift down to leave ~15–16 useful bits with a little headroom.
    int32_t x = s32 >> 11; // try 11–12; 11 keeps more volume, 12 more headroom
    if (x > 32767)
        x = 32767; // saturate, don't wrap
    if (x < -32768)
        x = -32768;
    return (int16_t)x;
}

void RecordingModule::processChunk(int32_t *i2sBuf, size_t samples)
{
    for (size_t i = 0; i < samples; ++i)
    {
        int32_t s = i2sBuf[i];
        // int16_t pcm = (int16_t)(s >> 8);
        int16_t pcm = to_int16_from_i2s32(s);
        m_buf[m_bufIdx++] = pcm;
        m_totalSamples++;

        if (m_bufIdx >= m_chunkSamples)
        {
            flushChunk();
        }
    }
}

bool RecordingModule::upload()
{
    if (m_isRecording)
    {
        Serial.println("Upload called while recording; stopping first");
        stop();
    }

    if (!m_uploadPath)
    {
        Serial.println("Upload path not set! Call setUploadPath().");
        return false;
    }

    String url = String(API_HOST) + String(m_uploadPath);
    Serial.print("POST ");
    Serial.println(url);

    File f = SPIFFS.open(m_outPath, FILE_READ);
    if (!f)
    {
        Serial.println("No WAV to upload");
        return false;
    }

    HTTPClient http;
    http.begin(url);
    http.addHeader("Content-Type", "audio/wav");

    int httpCode = http.sendRequest("POST", &f, f.size());
    if (httpCode > 0)
    {
        Serial.printf("Upload response: %d\n", httpCode);
        String resp = http.getString();
        Serial.println(resp);
    }
    else
    {
        Serial.printf("HTTP POST failed: %s\n", http.errorToString(httpCode).c_str());
        http.end();
        f.close();
        return false;
    }

    http.end();
    f.close();
    SPIFFS.remove(m_outPath);
    return true;
}
