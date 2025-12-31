#include <Arduino.h>
#include <FS.h>
#include <SPIFFS.h>
#include <esp_sleep.h>
#include <functional>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

// For screen
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
// --------

#include "ApiClientModule.h"
#include "ButtonModule.h"
#include "SpeakerModule.h"
#include "WifiModule.h"

// secrets
#include "secrets.h"

// -------------------- Pins & UI --------------------
#define BUTTON_PIN 33

// I2S Mic (INMP441)
#define I2S_MIC_NUM I2S_NUM_0
#define I2S_MIC_WS 25
#define I2S_MIC_SCK 14
#define I2S_MIC_SD 34

// Screen (SSD1306)
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1       // Reset pin # (or -1 if sharing Arduino reset pin)
#define SCREEN_ADDRESS 0x3D ///< See datasheet for Address; 0x3D for 128x64, 0x3C for 128x32
#define I2C_SDA 23
#define I2C_SCL 22

// I2S Speaker (MAX98357A)
#define I2S_SPK_NUM I2S_NUM_1
#define I2S_SPK_WS 18  // LRC
#define I2S_SPK_SCK 19 // BCLK
#define I2S_SPK_SD 21  // DIN

static const uint32_t kSampleRate = 8000;
static const size_t kChunkSamples = 4096;
static const uint32_t kMaxSeconds = 30;
static const char *kOutgoingPath = "/record.wav";
static const char *kIncomingPath = "/inbox.wav";
static const char *kSelfInboxPath = "/v1/" KEY_CLIENT "/inbox";
static const char *kPartnerInboxPath = "/v1/" KEY_RECIPIENT "/inbox";
static const uint32_t kInactivityMs = 120000; // 2 minutes before countdown starts
static const int kCountdownSeconds = 10;

static bool displayReady = false;
static bool displayPowered = true;
static uint32_t lastInteractionMs = 0;
static bool countdownActive = false;
static int countdownSecondsLeft = 0;
static uint32_t countdownLastTickMs = 0;
static bool progressActive = false;
static const char *progressLabel = nullptr;
static uint32_t progressLastTickMs = 0;
static int progressDots = 1;
static bool recordPromptEnabled = false;

static void wakeDisplayIfNeeded();
static void showStateOnDisplay(const char *line);
static void showCountdown(int secondsLeft);
static void showSleepMessage();
static void disableBoardLed();
static void showMomentaryNotice(const char *line1, const char *line2, uint32_t durationMs = 2000);
static void resetInactivityTimer();
static void cancelSleepCountdown();
static bool inactivityEligible();
static void handleInactivityCountdown();
static void enterDeepSleep();
static void startProgress(const char *label);
static void stopProgress(bool restoreState = true);
static void updateProgressAnimation();
static void showProgressFrame();
static bool performWithProgress(const char *label, const std::function<bool()> &operation, bool restoreStateAfter = true);
static bool waitForWifiConnection(uint32_t timeoutMs);
static bool ensureWifi(bool showProgress = true);
static bool checkSelfInbox(bool restoreState = true);
static bool checkPartnerInbox(bool restoreState = true);
static void primeStateAfterWake();
static void proceedToPartnerWorkflow(bool restoreStateAfterProgress = true);
static void powerDownWithMessage();
static void handleFatalWifiFailure();

enum class ClientState
{
  ReadyToDownload,
  ReadyToPlay,
  ReadyToDelete,
  ReadyToCheckPartner,
  ReadyToRecord,
  Recording,
  Recorded,
  ReadyToPowerDown
};

static ClientState clientState = ClientState::ReadyToRecord;

Button button(BUTTON_PIN);
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
SpeakerModule speaker(I2S_SPK_NUM, I2S_SPK_SCK, I2S_SPK_WS, I2S_SPK_SD);
WifiModule wifi;
ApiClientModule apiClient(I2S_MIC_NUM, I2S_MIC_SCK, I2S_MIC_WS, I2S_MIC_SD,
                          kSampleRate, kChunkSamples, kMaxSeconds, kOutgoingPath);

static const char *stateLabel(ClientState state)
{
  switch (state)
  {
  case ClientState::ReadyToDownload:
    return "readyToDownload";
  case ClientState::ReadyToPlay:
    return "readyToPlay";
  case ClientState::ReadyToDelete:
    return "readyToDelete";
  case ClientState::ReadyToCheckPartner:
    return "readyToCheckPartner";
  case ClientState::ReadyToRecord:
    return "readyToRecord";
  case ClientState::Recording:
    return "recording";
  case ClientState::Recorded:
    return "recorded";
  case ClientState::ReadyToPowerDown:
    return "readyToPowerDown";
  }
  return "unknown";
}

static void wakeDisplayIfNeeded()
{
  if (!displayPowered)
  {
    display.ssd1306_command(SSD1306_DISPLAYON);
    displayPowered = true;
  }
}

static void showStateOnDisplay(const char *line)
{
  if (!displayReady)
    return;
  (void)line;
  wakeDisplayIfNeeded();
  display.clearDisplay();
  display.setTextColor(WHITE);

  if (clientState == ClientState::ReadyToRecord)
  {
    if (!recordPromptEnabled)
    {
      display.display();
      return;
    }
    display.setTextSize(2);
    display.setCursor(0, 16);
    display.println(">Aufnehmen");
    display.display();
    return;
  }

  if (clientState == ClientState::ReadyToPlay)
  {
    display.setTextSize(2);
    display.setCursor(0, 16);
    display.println(">Abpielen");
    display.display();
    return;
  }

  // Leave the screen blank for all other states to avoid large-text flashes.
  display.display();
}

static void showCountdown(int secondsLeft)
{
  if (!displayReady)
    return;
  wakeDisplayIfNeeded();
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor(0, 0);
  display.println("Ruhezustand in");
  display.setTextSize(4);
  display.setCursor(28, 20);
  if (secondsLeft < 10)
    display.print("0");
  display.print(secondsLeft);
  display.display();
}

static void showSleepMessage()
{
  if (!displayReady)
    return;
  display.clearDisplay();
  display.display();
  display.ssd1306_command(SSD1306_DISPLAYOFF);
  displayPowered = false;
}

static void disableBoardLed()
{
#ifdef LED_BUILTIN
  // LilyGo T-Call LED is active-low on LED_BUILTIN; drive the pin high to keep it off.
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH);
#endif
}

static void showMomentaryNotice(const char *line1, const char *line2, uint32_t durationMs)
{
  if (!displayReady)
    return;
  wakeDisplayIfNeeded();
  display.clearDisplay();
  display.setTextColor(WHITE);
  display.setTextSize(2);
  display.setCursor(0, 8);
  if (line1)
    display.println(line1);
  if (line2)
    display.println(line2);
  display.display();
  delay(durationMs);
  display.clearDisplay();
  display.display();
}

static void powerDownWithMessage()
{
  startProgress("Herunterfahren");
  const uint32_t showUntil = millis() + 2000;
  while (millis() < showUntil)
  {
    updateProgressAnimation();
    delay(50);
  }
  stopProgress(false);
  enterDeepSleep();
}

static void handleFatalWifiFailure()
{
  showMomentaryNotice("WLAN", "Fehler");
  powerDownWithMessage();
}

static void showProgressFrame()
{
  if (!displayReady || !progressLabel)
    return;
  wakeDisplayIfNeeded();
  display.clearDisplay();
  display.setTextColor(WHITE);
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println(progressLabel);

  static const char *kDotFrames[] = {".", "..", "..."};
  const char *dots = kDotFrames[progressDots - 1];

  display.setTextSize(3);
  int16_t x1, y1;
  uint16_t w, h;
  display.getTextBounds(dots, 0, 0, &x1, &y1, &w, &h);
  const int16_t x = (SCREEN_WIDTH - static_cast<int>(w)) / 2;
  const int16_t y = (SCREEN_HEIGHT - static_cast<int>(h)) / 2;
  display.setCursor(x, y);
  display.print(dots);
  display.display();
}

static void startProgress(const char *label)
{
  progressLabel = label;
  progressDots = 1;
  progressLastTickMs = millis();
  progressActive = true;
  showProgressFrame();
}

static void stopProgress(bool restoreState)
{
  if (!progressActive)
    return;
  progressActive = false;
  progressLabel = nullptr;
  if (restoreState)
  {
    showStateOnDisplay(stateLabel(clientState));
  }
}

static void updateProgressAnimation()
{
  if (!progressActive)
    return;
  const uint32_t now = millis();
  if ((now - progressLastTickMs) < 350)
    return;
  progressLastTickMs = now;
  progressDots++;
  if (progressDots > 3)
    progressDots = 1;
  showProgressFrame();
}

static bool waitForWifiConnection(uint32_t timeoutMs)
{
  const uint32_t start = millis();
  while ((millis() - start) < timeoutMs)
  {
    if (wifi.isConnected())
      return true;
    delay(200);
  }
  return wifi.isConnected();
}

struct ProgressJob
{
  std::function<bool()> fn;
  bool result;
  volatile bool done;
};

static void progressJobTask(void *param)
{
  ProgressJob *job = static_cast<ProgressJob *>(param);
  job->result = job->fn();
  job->done = true;
  vTaskDelete(nullptr);
}

static bool performWithProgress(const char *label, const std::function<bool()> &operation, bool restoreStateAfter)
{
  ProgressJob *job = new ProgressJob{operation, false, false};
  startProgress(label);
  const BaseType_t created = xTaskCreate(progressJobTask, "progressJob", 4096, job, 1, nullptr);
  if (created != pdPASS)
  {
    Serial.println("[PROGRESS] Failed to start progress task, running inline");
    const bool fallbackResult = operation();
    delete job;
    stopProgress(restoreStateAfter);
    return fallbackResult;
  }

  while (!job->done)
  {
    updateProgressAnimation();
    delay(50);
  }

  stopProgress(restoreStateAfter);
  const bool result = job->result;
  delete job;
  return result;
}

static void cancelSleepCountdown()
{
  if (!countdownActive)
    return;
  countdownActive = false;
  showStateOnDisplay(stateLabel(clientState));
}

static void resetInactivityTimer()
{
  lastInteractionMs = millis();
  cancelSleepCountdown();
}

static bool inactivityEligible()
{
  return clientState != ClientState::Recording;
}

static void logState()
{
  Serial.print("[STATE] ");
  Serial.println(stateLabel(clientState));
}

static void transitionTo(ClientState next)
{
  if (clientState == next)
    return;
  recordPromptEnabled = (next == ClientState::ReadyToRecord);
  clientState = next;
  logState();
  showStateOnDisplay(stateLabel(clientState));
}

static bool ensureWifi(bool showProgress)
{
  if (wifi.isConnected())
    return true;
  wifi.beginStation();
  if (wifi.connect(WIFI_SSID, WIFI_PASSWORD, 1))
    return true;

  if (waitForWifiConnection(500))
    return true;

  const uint32_t remainingMs = 8000;
  auto waitRemaining = [&]() -> bool
  {
    return waitForWifiConnection(remainingMs);
  };

  const bool connected = showProgress ? performWithProgress("Verbindungsaufbau", waitRemaining) : waitRemaining();
  if (connected)
  {
    Serial.print("WiFi connected: ");
    Serial.println(wifi.localIP());
    return true;
  }
  Serial.println("WiFi connection failed");
  handleFatalWifiFailure();
  return false;
}

static bool checkSelfInbox(bool restoreState)
{
  return performWithProgress("Posteingang checken", []() -> bool
                             {
    apiClient.setInboxPath(kSelfInboxPath);
    return apiClient.status(); }, restoreState);
}

static bool checkPartnerInbox(bool restoreState)
{
  return performWithProgress("Postausgang checken", []() -> bool
                             {
    apiClient.setInboxPath(kPartnerInboxPath);
    return apiClient.status(); }, restoreState);
}

static void proceedToPartnerWorkflow(bool restoreStateAfterProgress)
{
  transitionTo(ClientState::ReadyToCheckPartner);
  const bool partnerHasMessage = checkPartnerInbox(restoreStateAfterProgress);
  if (partnerHasMessage)
  {
    showMomentaryNotice("Ausgang", "voll");
    transitionTo(ClientState::ReadyToPowerDown);
    powerDownWithMessage();
    return;
  }

  transitionTo(ClientState::ReadyToRecord);
}

static void primeStateAfterWake()
{
  const bool selfHasMessage = checkSelfInbox(false);
  if (selfHasMessage)
  {
    transitionTo(ClientState::ReadyToDownload);
    const bool downloaded = performWithProgress("Herunterladen", [&]() -> bool
                                                {
      apiClient.setInboxPath(kSelfInboxPath);
      return apiClient.downloadMessage(kIncomingPath); }, false);

    if (!downloaded)
    {
      return;
    }

    transitionTo(ClientState::ReadyToPlay);
    return;
  }

  showMomentaryNotice("Eingang", "leer");
  proceedToPartnerWorkflow(false);
}

static bool powerOn()
{
  if (!ensureWifi())
    return false;
  primeStateAfterWake();
  return true;
}

static void powerDown()
{
  apiClient.stop();
  if (SPIFFS.exists(kIncomingPath))
    SPIFFS.remove(kIncomingPath);
  if (SPIFFS.exists(kOutgoingPath))
    SPIFFS.remove(kOutgoingPath);
  WiFi.disconnect(true);
}

static void enterDeepSleep()
{
  Serial.println("[SLEEP] Entering deep sleep");
  powerDown();

  showSleepMessage();

  // Configure button (active-low) as wakeup source
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  esp_sleep_enable_ext0_wakeup(static_cast<gpio_num_t>(BUTTON_PIN), 0);

  delay(50);
  Serial.flush();
  esp_deep_sleep_start();
}

static void handleInactivityCountdown()
{
  if (!inactivityEligible())
  {
    cancelSleepCountdown();
    lastInteractionMs = millis();
    return;
  }

  const uint32_t now = millis();
  if (!countdownActive)
  {
    if ((now - lastInteractionMs) >= kInactivityMs)
    {
      countdownActive = true;
      countdownSecondsLeft = kCountdownSeconds;
      countdownLastTickMs = now;
      showCountdown(countdownSecondsLeft);
    }
    return;
  }

  if (now - countdownLastTickMs >= 1000)
  {
    countdownLastTickMs = now;
    countdownSecondsLeft--;
    if (countdownSecondsLeft <= 0)
    {
      enterDeepSleep();
    }
    else
    {
      showCountdown(countdownSecondsLeft);
    }
  }
}

static void handleButtonPress()
{
  Serial.print("Button -> ");
  Serial.println(stateLabel(clientState));

  switch (clientState)
  {
  case ClientState::ReadyToDownload:
  {
    if (!ensureWifi())
      break;
    const bool downloaded = performWithProgress("Nachricht empfangen", [&]() -> bool
                                                {
      apiClient.setInboxPath(kSelfInboxPath);
      return apiClient.downloadMessage(kIncomingPath); });
    if (downloaded)
      transitionTo(ClientState::ReadyToPlay);
    else
      transitionTo(ClientState::ReadyToCheckPartner);
    break;
  }

  case ClientState::ReadyToPlay:
  {
    const bool played = performWithProgress("Nachricht abspielen", [&]() -> bool
                                            { return speaker.playFile(kIncomingPath); }, false);
    if (!played)
      break;

    transitionTo(ClientState::ReadyToDelete);
    const bool deleted = performWithProgress("Nachricht entfernen", [&]() -> bool
                                             {
      apiClient.setInboxPath(kSelfInboxPath);
      return apiClient.deleteMessage(); }, false);
    if (!deleted)
      break;

    if (SPIFFS.exists(kIncomingPath))
      SPIFFS.remove(kIncomingPath);

    proceedToPartnerWorkflow(false);
    break;
  }

  case ClientState::ReadyToDelete:
  {
    if (!ensureWifi())
      break;
    const bool deleted = performWithProgress("Löschen", [&]() -> bool
                                             {
      apiClient.setInboxPath(kSelfInboxPath);
      return apiClient.deleteMessage(); }, false);
    if (deleted)
    {
      if (SPIFFS.exists(kIncomingPath))
        SPIFFS.remove(kIncomingPath);
      transitionTo(ClientState::ReadyToCheckPartner);
    }
    break;
  }

  case ClientState::ReadyToCheckPartner:
  {
    if (!ensureWifi())
      break;
    proceedToPartnerWorkflow();
    break;
  }

  case ClientState::ReadyToRecord:
    apiClient.start();
    transitionTo(ClientState::Recording);
    startProgress("Aufnahme             zum Stoppen druecken");
    break;

  case ClientState::Recording:
  {
    apiClient.stop();
    stopProgress();
    if (!ensureWifi())
    {
      transitionTo(ClientState::Recorded);
      break;
    }
    const bool uploaded = performWithProgress("Verschicken", [&]() -> bool
                                              {
      apiClient.setInboxPath(kPartnerInboxPath);
      return apiClient.upload(); });
    if (uploaded)
    {
      transitionTo(ClientState::ReadyToPowerDown);
      powerDownWithMessage();
    }
    else
    {
      transitionTo(ClientState::Recorded);
    }
    break;
  }

  case ClientState::Recorded:
  {
    if (!ensureWifi())
      break;
    const bool uploaded = performWithProgress("Verschicken", [&]() -> bool
                                              {
      apiClient.setInboxPath(kPartnerInboxPath);
      return apiClient.upload(); });
    if (uploaded)
    {
      transitionTo(ClientState::ReadyToPowerDown);
      powerDownWithMessage();
    }
    break;
  }

  case ClientState::ReadyToPowerDown:
    powerDownWithMessage();
    break;
  }
}

void setup()
{
  Serial.begin(115200);
  disableBoardLed();
  const esp_sleep_wakeup_cause_t wakeCause = esp_sleep_get_wakeup_cause();
  Wire.begin(I2C_SDA, I2C_SCL);

  if (!SPIFFS.begin(true))
  {
    Serial.println("[SPIFFS] mount failed");
  }

  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C))
  {
    Serial.println(F("failed to start SSD1306"));
    while (true)
    {
      delay(1000);
    }
  }

  display.clearDisplay();
  displayReady = true;
  displayPowered = true;
  showStateOnDisplay(stateLabel(clientState));

  speaker.begin();
  button.begin();
  wifi.beginStation();
  apiClient.begin();

  if (wakeCause != ESP_SLEEP_WAKEUP_EXT0)
  {
    Serial.println("[SLEEP] Cold boot detected, waiting for button wake");
    enterDeepSleep();
    return;
  }
  powerOn();

  Serial.println("Ready. Press button to follow workflow");
  logState();
  resetInactivityTimer();
}

void loop()
{
  button.update();
  bool pressed = button.wasPressed();
  if (pressed)
  {
    Serial.println("Button pressed");
    if (countdownActive)
    {
      cancelSleepCountdown();
      resetInactivityTimer();
    }
    else
    {
      resetInactivityTimer();
      handleButtonPress();
      resetInactivityTimer();
    }
  }

  updateProgressAnimation();
  handleInactivityCountdown();
  delay(1);
}