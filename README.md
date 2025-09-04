# ESP8266 OTA Pipeline with Rollback and MQTT Telemetry

This project demonstrates a DevOps-inspired pipeline for **safe Over-The-Air (OTA) firmware updates** on ESP8266 devices.  
It combines **CI/CD practices** with **rollback safety mechanisms** and **real-time telemetry reporting** over MQTT.

---

## Features

- **Automatic OTA Delivery**  
  New firmware is built via GitHub Actions and deployed to GitHub Pages for distribution.

- **Firmware Rollback**  
  If an update fails to boot, the ESP8266 automatically rolls back to the previous firmware version.

- **Boot Validation**  
  Each firmware version is validated at startup before being marked as stable.

- **MQTT Telemetry**  
  Device health metrics are published periodically:
  - Uptime
  - Free heap memory
  - Wi-Fi RSSI
  - Running firmware version

---

## Architecture

```mermaid
flowchart TD
    subgraph Cloud [Cloud / CI-CD Pipeline]
        A[GitHub Actions] -->|Builds Firmware| B[firmware.bin]
        B -->|Deployed to| C[GitHub Pages]
        D[ota.json Metadata] --> C
    end

    subgraph Device [ESP8266 Device]
        E[ESP8266]
        E -->|Fetch ota.json| C
        E -->|Download new firmware| C
        E -->|Validate Boot| F{Boot Success?}
        F -- Yes --> G[Set Boot Flag OK]
        F -- No --> H[Rollback: firmware-prev.bin from GitHub Pages]
        E -->|Telemetry: uptime, heap, RSSI, version| I[MQTT Broker]
    end

    I[MQTT Broker] --> J[Monitoring / Dashboard]
```
