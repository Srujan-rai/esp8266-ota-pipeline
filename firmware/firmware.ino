#include <ESP8266WiFi.h>
#include <WiFiClientSecure.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266httpUpdate.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <EEPROM.h>

// ================= USER CONFIG =================
#ifndef FW_VERSION
#define FW_VERSION "v1.0.0"
#endif

const char *WIFI_SSID = "Airtel_Srujan";
const char *WIFI_PASS = "raisrujan@2003S";

const char *MQTT_BROKER = "192.168.1.5";
const int MQTT_PORT = 1883;
const char *MQTT_TOPIC = "esp8266/metrics";
const char *MQTT_OTA = "esp8266/ota";

const char *OTA_META_URL = "https://srujan-rai.github.io/esp8266-ota-pipeline/ota.json";
const char *ROLLBACK_URL = "https://srujan-rai.github.io/esp8266-ota-pipeline/firmware-prev.bin";

// ================= BOOT FLAG (EEPROM) =================
#define EEPROM_SIZE 4
#define EEPROM_ADDR 0

#define BOOT_FLAG_OK 0xAA
#define BOOT_FLAG_FAIL 0x55

String currentVersion = FW_VERSION;

WiFiClientSecure secureClient; // HTTPS for OTA
WiFiClient wifiClient;         // plain TCP for MQTT
PubSubClient mqttClient(wifiClient);

unsigned long lastCheck = 0;
const unsigned long CHECK_INTERVAL = 60UL * 1000UL; // 1 min

// --- EEPROM helpers ---
void eepromWriteBootFlag(uint8_t flag)
{
    EEPROM.begin(EEPROM_SIZE);
    EEPROM.write(EEPROM_ADDR, flag);
    EEPROM.commit();
    EEPROM.end();
}

uint8_t eepromReadBootFlag()
{
    EEPROM.begin(EEPROM_SIZE);
    uint8_t val = EEPROM.read(EEPROM_ADDR);
    EEPROM.end();
    return val;
}

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
    char payload[160];
    snprintf(payload, sizeof(payload),
             "{\"uptime\":%lu,\"free_heap\":%u,\"wifi_rssi\":%ld,\"fw_version\":\"%s\"}",
             uptime, freeHeap, rssi, currentVersion.c_str());
    mqttClient.publish(MQTT_TOPIC, payload);
}

// --- Rollback ---
void performRollback()
{
    Serial.println("[ROLLBACK] Attempting rollback to previous firmware...");
    secureClient.setInsecure();
    Serial.printf("[ROLLBACK] Download URL: %s\n", ROLLBACK_URL);

    t_httpUpdate_return ret = ESPhttpUpdate.update(secureClient, ROLLBACK_URL);

    if (ret == HTTP_UPDATE_OK)
    {
        Serial.println("[ROLLBACK] Rollback flashed OK. Marking success.");
        eepromWriteBootFlag(BOOT_FLAG_OK); // ✅ prevent rollback loop
    }
    else
    {
        int err = ESPhttpUpdate.getLastError();
        const String errStr = ESPhttpUpdate.getLastErrorString();
        Serial.printf("[ROLLBACK] Rollback failed (err=%d): %s\n", err, errStr.c_str());
        // leave FAIL flag → will retry rollback next boot
        delay(3000);
    }
}

// --- OTA Check ---
void checkForUpdate()
{
    if (millis() - lastCheck < CHECK_INTERVAL)
        return;
    lastCheck = millis();

    if (WiFi.status() != WL_CONNECTED)
        return;

    HTTPClient http;
    secureClient.setInsecure();

    if (!http.begin(secureClient, OTA_META_URL))
    {
        Serial.println("HTTP begin failed");
        return;
    }

    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    int httpCode = http.GET();
    if (httpCode != HTTP_CODE_OK)
    {
        Serial.printf("HTTP GET failed, code: %d\n", httpCode);
        http.end();
        return;
    }

    String body = http.getString();
    http.end();

    Serial.println("Received OTA metadata:");
    Serial.println(body);

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

    secureClient.setInsecure();
    eepromWriteBootFlag(BOOT_FLAG_FAIL); // mark fail BEFORE OTA
    t_httpUpdate_return ret = ESPhttpUpdate.update(secureClient, binUrl);

    switch (ret)
    {
    case HTTP_UPDATE_FAILED:
        Serial.printf("OTA failed. Error (%d): %s\n",
                      ESPhttpUpdate.getLastError(),
                      ESPhttpUpdate.getLastErrorString().c_str());
        mqttClient.publish(MQTT_OTA, "{\"ota_status\":0}");
        eepromWriteBootFlag(BOOT_FLAG_FAIL);
        break;

    case HTTP_UPDATE_NO_UPDATES:
        Serial.println("No updates available");
        break;

    case HTTP_UPDATE_OK:
        Serial.println("OTA OK -> rebooting...");
        eepromWriteBootFlag(BOOT_FLAG_OK); // ✅ success
        mqttClient.publish(MQTT_OTA, "{\"ota_status\":1}");
        break;
    }
}

// --- Setup & Loop ---
void setup()
{
    Serial.begin(115200);
    connectWiFi();
    connectMQTT();
    lastCheck = millis();

    uint8_t bootFlag = eepromReadBootFlag();
    Serial.printf("Booting firmware version: %s (bootFlag=0x%02X)\n",
                  currentVersion.c_str(), bootFlag);

    if (bootFlag == BOOT_FLAG_FAIL)
    {
        performRollback();
    }
    else
    {
        // Fresh boot is considered successful
        eepromWriteBootFlag(BOOT_FLAG_OK);
    }
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
