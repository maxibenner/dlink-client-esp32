#include <Arduino.h>
#include <FS.h>
#include <SPIFFS.h>

#include "ButtonModule.h"
#include "AudioRecorderModule.h"
#include "SpeakerModule.h"

// -------------------- Pins & UI --------------------
#define BUTTON_PIN 15

// I2S Mic (INMP441)
#define I2S_MIC_NUM I2S_NUM_0
#define I2S_MIC_WS 25
#define I2S_MIC_SCK 14
#define I2S_MIC_SD 34

// I2S Speaker (MAX98357A)
#define I2S_SPK_NUM I2S_NUM_1
#define I2S_SPK_WS 14  // LRC
#define I2S_SPK_SCK 26 // BCLK
#define I2S_SPK_SD 27  // DIN

static const char *kFile = "/record.wav";

// Button button(BUTTON_PIN);
AudioRecorderModule audioRecorder(I2S_MIC_NUM, I2S_MIC_SCK, I2S_MIC_WS, I2S_MIC_SD);
// SpeakerModule speaker(I2S_SPK_NUM, I2S_SPK_SCK, I2S_SPK_WS, I2S_SPK_SD);

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
  delay(200);
  audioRecorder.begin();
  // Serial.println("\nBooting...");

  // button.begin();
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
  audioRecorder.plot();

  // Serial.print(">");

  // Serial.print("var1:");
  // Serial.print(cos(angle));
  // Serial.print(",");

  // Serial.print("var2:");
  // Serial.print(cos(angle + PI / 2) * 0.1);
  // Serial.print(",");

  // Serial.print("var3:");
  // Serial.print(cos(angle + PI / 4) * 1.2 + 2);
  // Serial.println(); // Writes \r\n

  // Serial.println("This is totally ignored");
  // delay(100);

  // angle += PI / 10;
  // button.update();

  // if (button.wasPressed())
  // {
  //   switch (mode)
  //   {
  //   case Mode::Ready:
  //     if (audioRecorder.startRecording(kFile))
  //     {
  //       mode = Mode::Recording;
  //       Serial.println("[REC] Started");
  //     }
  //     else
  //     {
  //       Serial.println("[REC] Start failed");
  //     }
  //     break;

  //   case Mode::Recording:
  //     audioRecorder.stopRecording();
  //     mode = Mode::JustRecorded;
  //     Serial.println("[REC] Stopped. Press to play.");
  //     break;

  //   case Mode::JustRecorded:
  //   {
  //     Serial.println("[PLAY] Playing...");
  //     mode = Mode::Playing;
  //     bool ok = speaker.playFile(kFile); // blocking until done
  //     Serial.println(ok ? "[PLAY] Done" : "[PLAY] Failed");
  //     if (SPIFFS.exists("/record.wav"))
  //     {
  //       SPIFFS.remove("/record.wav");
  //       Serial.println("Removing file");
  //     };
  //     mode = Mode::Ready;
  //     break;
  //   }

  //   case Mode::Playing:
  //     // Ignore presses while already playing
  //     break;
  //   }
  // }

  // // Keep pumping I2S->SPIFFS while recording
  // if (mode == Mode::Recording)
  // {
  //   audioRecorder.handle();
  // }

  delay(1);
}