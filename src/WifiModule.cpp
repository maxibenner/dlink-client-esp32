#include "WifiModule.h"
#include "secrets.h"

void WifiModule::beginStation()
{
    WiFi.mode(WIFI_STA);
}

bool WifiModule::connect(const char *ssid, const char *password, uint32_t timeoutMs)
{
    WiFi.begin(ssid, password);

    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && (millis() - start) < timeoutMs)
    {
        delay(100);
    }
    return WiFi.status() == WL_CONNECTED;
}

bool WifiModule::isConnected() const
{
    return WiFi.status() == WL_CONNECTED;
}

IPAddress WifiModule::localIP() const
{
    return WiFi.localIP();
}

int WifiModule::httpGet(const String &url, String &payloadOut, uint32_t timeoutMs)
{
    if (!isConnected())
        return -1; // not connected

    WiFiClient client; // For HTTPS, switch to WiFiClientSecure and set fingerprints/validation
    HTTPClient http;

    http.setTimeout(timeoutMs);
    if (!http.begin(client, url))
    {
        return -2; // begin failed
    }

    int httpCode = http.GET();
    if (httpCode > 0)
    {
        if (httpCode == HTTP_CODE_OK)
        {
            payloadOut = http.getString();
        }
    }
    http.end();
    return httpCode; // Could be 200.., or negative error from HTTPClient
}