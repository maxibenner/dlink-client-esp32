#pragma once
#include <Arduino.h>

class Button
{
public:
    explicit Button(uint8_t pin);

    void begin();  // call in setup()
    void update(); // call every loop()

    bool isPressed() const; // debounced current state (active-low with INPUT_PULLUP)
    bool wasPressed();      // true exactly once after a press edge
    bool wasReleased();     // true exactly once after a release edge

private:
    uint8_t _pin;
    int _stable;                 // last debounced electrical level (HIGH/LOW)
    int _lastReading;            // last instantaneous read
    unsigned long _lastChangeMs; // when _lastReading last changed

    bool _latchedPressed; // edge latches
    bool _latchedReleased;

    static const unsigned long DEBOUNCE_MS = 50;
};
