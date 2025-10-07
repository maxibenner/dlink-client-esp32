#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include "ButtonModule.h"
#include "WifiModule.h"

static const char *kWifiSsid = WIFI_SSID;
static const char *kWifiPassword = WIFI_PASSWORD;
static const char *kApiHost = API_HOST;
static const char *kApiPath = API_PATH;

// Button
#define BUTTON_PIN 15

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 32 // change to 64 for 128x64
#define OLED_RESET -1
#define SCREEN_ADDRESS 0x3C // set to your scanner result
#define I2C_SDA 21
#define I2C_SCL 22

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
Button button(BUTTON_PIN);
WifiModule net;

void setup()
{
  // General
  Serial.begin(115200);

  // Button -----------------
  button.begin();

  // Microphone ---------------

  // Screen -----------------
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

  // Wifi -----------------
  net.beginStation();

  Serial.print("Connecting to Wi-Fi");
  if (!net.connect(kWifiSsid, kWifiPassword, 15000))
  {
    Serial.println("\nWi-Fi connect failed (timeout). Rebooting in 5s...");
    delay(5000);
    ESP.restart();
  }

  Serial.print("\nWi-Fi connected, IP: ");
  Serial.println(net.localIP());
}

void loop()
{
  button.update();

  if (button.wasPressed())
  {
    Serial.println("Making request");

    // HTTP Request -----------------
    String url = String(kApiHost) + kApiPath;
    Serial.print("GET ");
    Serial.println(url);

    String payload;
    int code = net.httpGet(url, payload);
    if (code == HTTP_CODE_OK)
    {
      Serial.println(payload);
      display.println(payload);
      display.display();
    }
    else
    {
      Serial.printf("HTTP GET failed: %d\n", code);
    }
  }

  if (button.wasReleased())
  {
    // Serial.println("Button released");
  }
}
