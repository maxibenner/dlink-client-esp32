#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>

class WifiModule
{
public:
    WifiModule() = default;

    // Set Wi-Fi to station mode (safe to call multiple times)
    void beginStation();

    // Connect with a timeout (ms). Returns true on success.
    bool connect(const char *ssid, const char *password, uint32_t timeoutMs = 15000);

    // Quick status helpers
    bool isConnected() const;
    IPAddress localIP() const;

    // Convenience HTTP GET. Returns HTTP status code (e.g. 200), or negative on failure.
    // On 200 OK, 'payloadOut' is filled with the response body.
    int httpGet(const String &url, String &payloadOut, uint32_t timeoutMs = 10000);
};
