#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <ArduinoJson.h>
#include <ESP8266httpUpdate.h>

const char *ssid = "YOUR_WIFI";
const char *password = "YOUR_PASS";

// GitHub Pages URL (replace USER and REPO with your GitHub info)
const char *OTA_META_URL = "https://srujan-rai.github.io/REPO/ota/ota.json";

const unsigned long CHECK_INTERVAL = 60UL * 1000UL; // every 60 sec
unsigned long lastCheck = 0;

String currentVersion = "1.0.0"; // bump manually in code when releasing

void setup()
{
    Serial.begin(115200);
    WiFi.begin(ssid, password);

    Serial.print("Connecting to WiFi...");
    while (WiFi.status() != WL_CONNECTED)
    {
        delay(500);
        Serial.print(".");
    }
    Serial.println("\nWiFi connected!");
}

void loop()
{
    unsigned long now = millis();
    if (now - lastCheck > CHECK_INTERVAL)
    {
        lastCheck = now;
        checkForUpdate();
    }
}

void checkForUpdate()
{
    if (WiFi.status() != WL_CONNECTED)
        return;

    WiFiClient client;
    HTTPClient http;

    if (http.begin(client, OTA_META_URL))
    {
        int httpCode = http.GET();
        if (httpCode == 200)
        {
            String payload = http.getString();

            StaticJsonDocument<512> doc;
            DeserializationError err = deserializeJson(doc, payload);
            if (!err)
            {
                const char *newVersion = doc["version"];
                const char *binUrl = doc["url"];

                Serial.printf("Current: %s, Latest: %s\n", currentVersion.c_str(), newVersion);

                if (String(newVersion) != currentVersion)
                {
                    Serial.println("Updating...");
                    t_httpUpdate_return ret = ESPhttpUpdate.update(client, binUrl);
                    if (ret == HTTP_UPDATE_OK)
                    {
                        Serial.println("Update OK, rebooting...");
                    }
                    else
                    {
                        Serial.printf("Update failed: %d\n", ret);
                    }
                }
                else
                {
                    Serial.println("Already up to date.");
                }
            }
        }
        http.end();
    }
}
