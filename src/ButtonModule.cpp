#include "ButtonModule.h"

Button::Button(uint8_t pin)
    : _pin(pin),
      _stable(HIGH),
      _lastReading(HIGH),
      _lastChangeMs(0),
      _latchedPressed(false),
      _latchedReleased(false) {}

void Button::begin()
{
  pinMode(_pin, INPUT_PULLUP); // assume pull-up wiring (active-low)
  int r = digitalRead(_pin);
  _stable = r;
  _lastReading = r;
  _lastChangeMs = millis();
}

void Button::update()
{
  int reading = digitalRead(_pin);

  if (reading != _lastReading)
  {
    _lastReading = reading;
    _lastChangeMs = millis();
  }

  if ((millis() - _lastChangeMs) >= DEBOUNCE_MS && reading != _stable)
  {
    // Debounced state change
    bool wasPressed = (_stable == LOW); // active-low
    bool nowPressed = (reading == LOW);

    if (!wasPressed && nowPressed)
      _latchedPressed = true;
    if (wasPressed && !nowPressed)
      _latchedReleased = true;

    _stable = reading;
  }
}

bool Button::isPressed() const
{
  return (_stable == LOW); // active-low => LOW means pressed
}

bool Button::wasPressed()
{
  if (_latchedPressed)
  {
    _latchedPressed = false;
    return true;
  }
  return false;
}

bool Button::wasReleased()
{
  if (_latchedReleased)
  {
    _latchedReleased = false;
    return true;
  }
  return false;
}
