#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266httpUpdate.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

// ================= USER CONFIG =================
#ifndef FW_VERSION
#define FW_VERSION "v1.0.0" // fallback if not injected by build pipeline
#endif

const char *WIFI_SSID = "Airtel_Srujan";
const char *WIFI_PASS = "raisrujan@2003S";

// OTA metadata hosted on GitHub Pages
const char *OTA_META_URL =
    "https://srujan-rai.github.io/esp8266-ota-pipeline/ota.json";

// MQTT broker (optional monitoring)
const char *MQTT_BROKER = "192.168.1.5";
const int MQTT_PORT = 1883;
const char *MQTT_TOPIC = "esp8266/metrics";
const char *MQTT_OTA = "esp8266/ota";
// =================================================

String currentVersion = FW_VERSION;
WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);

unsigned long lastCheck = 0;
const unsigned long CHECK_INTERVAL = 60UL * 1000UL; // 1 minute

// --- WiFi & MQTT ---
void connectWiFi()
{
    if (WiFi.status() == WL_CONNECTED)
        return;
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    Serial.print("Connecting to WiFi");
    while (WiFi.status() != WL_CONNECTED)
    {
        delay(500);
        Serial.print(".");
    }
    Serial.println("\nWiFi connected");
}

void connectMQTT()
{
    mqttClient.setServer(MQTT_BROKER, MQTT_PORT);
    while (!mqttClient.connected())
    {
        if (mqttClient.connect("esp8266-client"))
        {
            Serial.println("MQTT connected");
        }
        else
        {
            delay(500);
        }
    }
}

// --- Telemetry ---
void publishMetrics()
{
    unsigned long uptime = millis() / 1000;
    uint32_t freeHeap = ESP.getFreeHeap();
    long rssi = WiFi.RSSI();
    char payload[128];
    snprintf(payload, sizeof(payload),
             "{\"uptime\":%lu,\"free_heap\":%u,\"wifi_rssi\":%ld,\"fw_version\":\"%s\"}",
             uptime, freeHeap, rssi, currentVersion.c_str());
    mqttClient.publish(MQTT_TOPIC, payload);
}

// --- OTA Update ---
void checkForUpdate()
{
    if (millis() - lastCheck < CHECK_INTERVAL)
        return;
    lastCheck = millis();

    if (WiFi.status() != WL_CONNECTED)
        return;

    BearSSL::WiFiClientSecure client;
    client.setInsecure(); // skip certificate check
    HTTPClient http;
    if (!http.begin(client, OTA_META_URL))
    {
        Serial.println("HTTP begin failed");
        return;
    }

    int httpCode = http.GET();
    if (httpCode != HTTP_CODE_OK)
    {
        Serial.printf("HTTP GET failed, code: %d\n", httpCode);
        http.end();
        return;
    }

    String body = http.getString();
    http.end();

    StaticJsonDocument<512> doc;
    if (deserializeJson(doc, body))
    {
        Serial.println("JSON parse error");
        return;
    }

    const char *version = doc["version"];
    const char *binUrl = doc["url"];

    if (!version || !binUrl)
    {
        Serial.println("Invalid JSON fields");
        return;
    }

    if (String(version) == currentVersion)
    {
        Serial.println("No new firmware");
        return;
    }

    Serial.printf("New firmware %s found at %s\n", version, binUrl);

    t_httpUpdate_return ret = ESPhttpUpdate.update(client, binUrl);
    switch (ret)
    {
    case HTTP_UPDATE_FAILED:
        Serial.printf("OTA failed. Error (%d): %s\n",
                      ESPhttpUpdate.getLastError(),
                      ESPhttpUpdate.getLastErrorString().c_str());
        mqttClient.publish(MQTT_OTA, "{\"ota_status\":0}");
        break;

    case HTTP_UPDATE_NO_UPDATES:
        Serial.println("No updates available");
        break;

    case HTTP_UPDATE_OK:
        Serial.println("OTA OK -> rebooting...");
        mqttClient.publish(MQTT_OTA, "{\"ota_status\":1}");
        break;
    }
}

void setup()
{
    Serial.begin(115200);
    connectWiFi();
    connectMQTT();
    lastCheck = millis();

    Serial.printf("Booting firmware version: %s\n", currentVersion.c_str());
}

void loop()
{
    if (WiFi.status() != WL_CONNECTED)
        connectWiFi();
    if (!mqttClient.connected())
        connectMQTT();
    mqttClient.loop();

    publishMetrics();
    checkForUpdate();

    delay(5000);
}
