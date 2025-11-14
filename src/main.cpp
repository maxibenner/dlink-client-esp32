#include <Arduino.h>
#include <FS.h>
#include <SPIFFS.h>

// For screen
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
// --------

#include "ButtonModule.h"
#include "AudioRecorderModule.h"
#include "SpeakerModule.h"

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

static const char *kFile = "/record.wav";

Button button(BUTTON_PIN);
AudioRecorderModule audioRecorder(I2S_MIC_NUM, I2S_MIC_SCK, I2S_MIC_WS, I2S_MIC_SD);
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
SpeakerModule speaker(I2S_SPK_NUM, I2S_SPK_SCK, I2S_SPK_WS, I2S_SPK_SD);

enum class Mode
{
  Ready,
  Recording,
  JustRecorded,
  Playing
};
static Mode mode = Mode::Ready;

void setup()
{
  Serial.begin(115200);

  // Setup I2C on custom pins
  Wire.begin(I2C_SDA, I2C_SCL);

  delay(200); // Necessary?

  // MICROPHONE -----------------------------------------------
  audioRecorder.begin();

  // DISPLAY -----------------------------------------------
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C))
  {
    Serial.println(F("failed to start SSD1306"));
    while (1)
      ;
  }
  Serial.println("Display Init success");

  display.clearDisplay();

  display.setTextSize(2);      // set text size
  display.setTextColor(WHITE); // set text color
  display.setCursor(0, 2);     // set position to display (x,y)
  display.println("Griasdi");  // set text
  display.display();

  // SPEAKER
  speaker.begin();

  // BUTTON
  button.begin();

  // // SSD1306_SWITCHCAPVCC = generate display voltage from 3.3V internally
  // if(!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
  //   Serial.println(F("SSD1306 allocation failed"));
  //   for(;;); // Don't proceed, loop forever
  // }

  // // Show initial display buffer contents on the screen --
  // // the library initializes this with an Adafruit splash screen.
  // display.display();

  // Serial.println("\nBooting...");

  // // ===================
  // if (!SPIFFS.begin(true))
  // {
  //   Serial.println("[SPIFFS] First mount failed");
  // }
  // // ===================

  // audioRecorder.begin();
  // speaker.begin();

  // Serial.println("Ready. Press to record, press to stop, press again to play.");
}

void loop()
{
  // Microphone
  // audioRecorder.plot(); // Working

  // Button
  button.update();

  if (button.wasPressed())
  {
    Serial.println("Button pressed");
    // switch (mode)
    // {
    // case Mode::Ready:
    //   if (audioRecorder.startRecording(kFile))
    //   {
    //     mode = Mode::Recording;
    //     Serial.println("[REC] Started");
    //   }
    //   else
    //   {
    //     Serial.println("[REC] Start failed");
    //   }
    //   break;

    // case Mode::Recording:
    //   audioRecorder.stopRecording();
    //   mode = Mode::JustRecorded;
    //   Serial.println("[REC] Stopped. Press to play.");
    //   break;

    // case Mode::JustRecorded:
    // {
    //   Serial.println("[PLAY] Playing...");
    //   mode = Mode::Playing;
    //   bool ok = speaker.playFile(kFile); // blocking until done
    //   Serial.println(ok ? "[PLAY] Done" : "[PLAY] Failed");
    //   if (SPIFFS.exists("/record.wav"))
    //   {
    //     SPIFFS.remove("/record.wav");
    //     Serial.println("Removing file");
    //   };
    //   mode = Mode::Ready;
    //   break;
    // }

    // case Mode::Playing:
    //   // Ignore presses while already playing
    //   break;
    // }
  }

  // // Keep pumping I2S->SPIFFS while recording
  // if (mode == Mode::Recording)
  // {
  //   audioRecorder.handle();
  // }

  delay(1);
}