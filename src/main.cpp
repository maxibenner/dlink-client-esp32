#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <FS.h>
#include <SPIFFS.h>

#include "ButtonModule.h"
#include "WifiModule.h"
#include "RecordingModule.h"
#include "secrets.h"

#include "driver/i2s.h"

// -------------------- Pins & UI --------------------
#define BUTTON_PIN 15

// I2S
#define I2S_NUM I2S_NUM_0
#define I2S_WS 25             // Word select (LRC)
#define I2S_SCK 33            // Serial clock (BCLK)
#define I2S_SD 32             // Serial data (DIN)
#define I2S_SAMPLE_RATE 16000 // 16kHz sample rate
#define I2S_BUFFER_SIZE 1024

// OLED
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 32
#define OLED_RESET -1
#define SCREEN_ADDRESS 0x3C
#define I2C_SDA 21
#define I2C_SCL 22

// Server
const char *UPLOAD_PATH = API_PATH;

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
Button button(BUTTON_PIN);
WifiModule net;

// RecordingModule: global instance
RecordingModule recorder(
    I2S_NUM,
    I2S_SCK,
    I2S_WS,
    I2S_SD,
    I2S_SAMPLE_RATE,
    512,
    30,
    "/rec.wav");

void setup()
{
  // General
  Serial.begin(115200);

  // Filesystem for temporary WAV storage
  if (!SPIFFS.begin(true))
  {
    Serial.println("SPIFFS mount failed");
  }

  // Button
  button.begin();

  // Screen
  Wire.begin(I2C_SDA, I2C_SCL);
  Wire.setClock(400000);

  if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS))
  {
    Serial.println("SSD1306 init failed");
    while (true)
      delay(1000);
  }
  display.clearDisplay();
  display.display();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println("Hold to record");
  display.display();

  recorder.begin();
  recorder.setUploadPath(UPLOAD_PATH);
}

void loop()
{

  button.update();

  if (button.wasPressed())
  {
    display.clearDisplay();
    display.setCursor(0, 0);
    display.println("Recording...");
    display.display();
    recorder.start();
  }

  if (button.wasReleased())
  {
    // Stop recording
    display.clearDisplay();
    display.setCursor(0, 0);
    display.println("Processing...");
    display.display();
    recorder.stop();

    // Start wifi
    display.clearDisplay();
    display.setCursor(0, 0);
    display.println("Connecting...");
    display.display();

    net.beginStation();
    if (!net.connect(WIFI_SSID, WIFI_PASSWORD, 15000))
    {
      display.clearDisplay();
      display.setCursor(0, 0);
      display.println("Failed, restart in 5s");
      delay(5000);
      ESP.restart();
    }
    display.clearDisplay();
    display.setCursor(0, 0);
    display.println("Uploading...");
    display.display();

    // Upload
    recorder.upload();

    display.clearDisplay();
    display.setCursor(0, 0);
    display.println("Success, shutting down...");
    display.display();
    delay(2000);

    // Clear display and restart ESP32
    display.clearDisplay();
    display.display();
    ESP.restart();
  }

  delay(1);
}
