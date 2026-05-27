# VacApp — ESP32 Bovine Monitor

ESP32 firmware for real-time bovine vital sign monitoring. Sends telemetry directly to the VacApp .NET backend (IoTMonitoring BC).

## Sensors

| Sensor | Measures | Pin |
|--------|----------|-----|
| MAX30102 | Heart rate (BPM) | I2C |
| MLX90614 | Body temperature (°C) | I2C |

## Normal Bovine Ranges

| Vital Sign | Min | Max |
|------------|-----|-----|
| Temperature | 38.0 °C | 39.5 °C |
| Heart Rate | 40 BPM | 80 BPM |

If any reading is outside these ranges, the backend returns `alarm: true` and the LED on pin 2 turns on.

## Configuration

Edit `vacapp_bovine_monitor.ino` before flashing:

```cpp
const char* WIFI_SSID     = "TU_SSID";
const char* WIFI_PASSWORD = "TU_PASSWORD";
const char* API_URL       = "https://tu-backend.azurewebsites.net/api/v1/iot-monitoring/telemetry";
const int   BOVINE_ID     = 1;    // ID of the bovine in VacApp DB
const char* DEVICE_ID     = "esp32-001";
```

## API Payload

```json
POST /api/v1/iot-monitoring/telemetry
{
  "bovineId": 1,
  "deviceId": "esp32-001",
  "temperature": 38.7,
  "heartRate": 62.0
}
```

Response:
```json
{
  "id": 5,
  "alarm": false,
  "message": "OK: vital signs within normal range."
}
```

## Dependencies (Arduino Library Manager)

- `MAX3010x` by Sparkfun
- `MLX90614` by Adafruit  
- `ArduinoJson` by Benoit Blanchon
- `WiFi` (built-in ESP32)
