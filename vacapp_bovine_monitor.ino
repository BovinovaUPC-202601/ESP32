#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

#include "Max30102Reader.h"
#include "Mlx90614Reader.h"

// ============================================
// CONFIGURACION - MODIFICAR SEGUN TU RED
// ============================================
const char* WIFI_SSID     = "TU_SSID";
const char* WIFI_PASSWORD = "TU_PASSWORD";

// URL del backend VacApp (.NET) — reemplazar con el dominio desplegado
const char* API_URL = "https://tu-backend.azurewebsites.net/api/v1/iot-monitoring/telemetry";

// ============================================
// CONFIGURACION DEL DISPOSITIVO
// ============================================
const int    BOVINE_ID = 1;           // ID del bovino en la base de datos VacApp
const char*  DEVICE_ID = "esp32-001"; // ID único del dispositivo ESP32

// ============================================
// ACTUADORES
// ============================================
const int LED_PIN = 2;

// ============================================
// TIEMPOS DE LECTURA (en milisegundos)
// ============================================
const unsigned long TIEMPO_LECTURA_MAX = 40000;  // 40s para BPM
const unsigned long TIEMPO_LECTURA_MLX = 10000;  // 10s para temperatura

// ============================================
// MAQUINA DE ESTADOS
// ============================================
enum AppState {
  STATE_INIT,
  STATE_WAIT_MAX_PROMPT,
  STATE_READING_MAX,
  STATE_WAIT_MLX_PROMPT,
  STATE_READING_MLX,
  STATE_SENDING_DATA,
  STATE_DONE
};

AppState appState = STATE_INIT;
unsigned long stateStartMs = 0;

// ============================================
// SENSORES
// ============================================
Max30102Reader maxReader;
Mlx90614Reader tempReader;

// ============================================
// ACUMULADORES
// ============================================
struct VitalSigns {
  float bpmSum;
  int   bpmCount;
  double tempSum;
  int   tempCount;

  void reset() {
    bpmSum = 0; bpmCount = 0;
    tempSum = 0; tempCount = 0;
  }

  float  getBpmAvg()  { return (bpmCount  > 0) ? bpmSum  / bpmCount  : 0; }
  double getTempAvg() { return (tempCount > 0) ? tempSum / tempCount : 0; }
} vitals;

// ============================================
// FUNCIONES AUXILIARES
// ============================================

void printHeader(const char* title) {
  Serial.println();
  Serial.println(F("============================================"));
  Serial.print(F("  "));
  Serial.println(title);
  Serial.println(F("============================================"));
  Serial.println();
}

void printProgress(unsigned long elapsed, unsigned long total) {
  int percent = (elapsed * 100) / total;
  int seconds = (total - elapsed) / 1000;
  Serial.print(F("["));
  for (int i = 0; i < 20; i++) {
    if (i < percent / 5) Serial.print(F("#"));
    else Serial.print(F("-"));
  }
  Serial.print(F("] "));
  Serial.print(percent);
  Serial.print(F("% - "));
  Serial.print(seconds);
  Serial.println(F("s restantes"));
}

bool waitForUserConfirmation() {
  if (!Serial.available()) return false;
  String input = Serial.readStringUntil('\n');
  input.trim();
  input.toLowerCase();
  if (input == "si" || input == "s" || input == "yes" || input == "y") return true;
  Serial.println(F("Escribe 'si' o 's' para continuar."));
  return false;
}

void connectWiFi() {
  Serial.print(F("Conectando a WiFi"));
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  int retries = 30;
  while (WiFi.status() != WL_CONNECTED && retries-- > 0) {
    delay(500);
    Serial.print(F("."));
  }
  Serial.println();
  if (WiFi.status() == WL_CONNECTED) {
    Serial.print(F("Conectado! IP: "));
    Serial.println(WiFi.localIP());
  } else {
    Serial.println(F("No se pudo conectar a WiFi (continuando offline)"));
  }
}

// ============================================
// ENVIO AL BACKEND VACAPP
// Payload: { bovineId, deviceId, temperature, heartRate }
// Respuesta: { id, alarm, message }
// ============================================
void sendToBackend(float heartRate, double temperature) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println(F("WiFi desconectado. No se pueden enviar datos."));
    return;
  }

  HTTPClient http;
  http.begin(API_URL);
  http.addHeader("Content-Type", "application/json");

  // Construir JSON con campos del BC IoTMonitoring
  String json = "{";
  json += "\"bovineId\":" + String(BOVINE_ID) + ",";
  json += "\"deviceId\":\"" + String(DEVICE_ID) + "\",";
  json += "\"temperature\":" + String(temperature, 2) + ",";
  json += "\"heartRate\":" + String(heartRate, 1);
  json += "}";

  Serial.print(F("Enviando telemetria: "));
  Serial.println(json);

  int httpCode = http.POST(json);

  if (httpCode > 0) {
    Serial.print(F("HTTP: "));
    Serial.println(httpCode);

    String response = http.getString();
    Serial.print(F("Respuesta: "));
    Serial.println(response);

    // Parsear respuesta del backend VacApp
    StaticJsonDocument<256> doc;
    DeserializationError error = deserializeJson(doc, response);

    if (!error) {
      bool alarm   = doc["alarm"]   | false;
      const char* message = doc["message"] | "Sin mensaje";

      Serial.println(message);

      // Activar LED si hay alerta de signo vital fuera de rango bovino
      if (alarm) {
        digitalWrite(LED_PIN, HIGH);
        Serial.println(F("⚠ ALERTA: Signo vital fuera del rango normal bovino. LED ENCENDIDO"));
      } else {
        digitalWrite(LED_PIN, LOW);
        Serial.println(F("OK: Signos vitales normales. LED APAGADO"));
      }
    } else {
      Serial.print(F("Error parseando respuesta: "));
      Serial.println(error.c_str());
    }

  } else {
    Serial.print(F("Error HTTP: "));
    Serial.println(http.errorToString(httpCode));
  }

  http.end();
}

// ============================================
// SETUP
// ============================================
void setup() {
  Serial.begin(115200);
  while (!Serial) delay(10);

  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  printHeader("VacApp — Monitor de Signos Vitales Bovinos");
  Serial.println(F("MAX30102 (BPM) + MLX90614 (Temperatura)"));
  Serial.print(F("Bovino ID: "));
  Serial.println(BOVINE_ID);
  Serial.println();

  connectWiFi();
  Serial.println();

  Serial.println(F("Inicializando sensores..."));
  maxReader.begin();
  tempReader.begin();
  Serial.println();

  appState = STATE_WAIT_MAX_PROMPT;
}

// ============================================
// LOOP — MAQUINA DE ESTADOS
// ============================================
void loop() {
  switch (appState) {

    case STATE_WAIT_MAX_PROMPT:
      Serial.println(F("Listo para medir ritmo cardiaco (BPM)."));
      Serial.println(F("Escribe 'si' para comenzar:"));
      appState = STATE_INIT;
      break;

    case STATE_INIT:
      if (waitForUserConfirmation()) {
        vitals.reset();
        maxReader.fillInitialBuffers();
        stateStartMs = millis();
        appState = STATE_READING_MAX;
      }
      break;

    case STATE_READING_MAX: {
      unsigned long elapsed = millis() - stateStartMs;

      if (elapsed >= TIEMPO_LECTURA_MAX) {
        printHeader("Lectura BPM Completada");
        Serial.println(F("Ahora mide temperatura corporal."));
        Serial.println(F("Escribe 'si' para continuar:"));
        appState = STATE_WAIT_MLX_PROMPT;
        break;
      }

      maxReader.stepAndComputeAndPrint();

      if (maxReader.isHeartRateValid()) {
        vitals.bpmSum += maxReader.getHeartRate();
        vitals.bpmCount++;
      }

      static unsigned long lastProgress = 0;
      if (millis() - lastProgress >= 5000) {
        lastProgress = millis();
        printProgress(elapsed, TIEMPO_LECTURA_MAX);
      }

      delay(10);
      break;
    }

    case STATE_WAIT_MLX_PROMPT:
      if (waitForUserConfirmation()) {
        printHeader("Midiendo Temperatura Corporal");
        Serial.println(F("Acerca el sensor al bovino."));
        stateStartMs = millis();
        appState = STATE_READING_MLX;
      }
      break;

    case STATE_READING_MLX: {
      unsigned long elapsed = millis() - stateStartMs;

      if (elapsed >= TIEMPO_LECTURA_MLX) {
        appState = STATE_SENDING_DATA;
        break;
      }

      double temp = tempReader.readObjectTemp();
      if (temp > 0) {
        vitals.tempSum += temp;
        vitals.tempCount++;
        Serial.print(F("Temperatura: "));
        Serial.print(temp, 1);
        Serial.println(F(" C"));
      } else {
        Serial.println(F("Acerca mas el sensor al bovino..."));
      }

      delay(500);
      break;
    }

    case STATE_SENDING_DATA: {
      printHeader("Resumen de Mediciones");

      float  bpmAvg  = vitals.getBpmAvg();
      double tempAvg = vitals.getTempAvg();

      Serial.print(F("Ritmo Cardiaco: "));
      if (vitals.bpmCount > 0) {
        Serial.print(bpmAvg, 0);
        Serial.print(F(" BPM ("));
        Serial.print(vitals.bpmCount);
        Serial.println(F(" muestras)"));
      } else {
        Serial.println(F("Sin datos validos"));
      }

      Serial.print(F("Temperatura:    "));
      if (vitals.tempCount > 0) {
        Serial.print(tempAvg, 1);
        Serial.print(F(" C ("));
        Serial.print(vitals.tempCount);
        Serial.println(F(" muestras)"));
      } else {
        Serial.println(F("Sin datos validos"));
      }

      // Rango normal bovino: 38.0–39.5 C / 40–80 BPM
      Serial.println(F("Rango normal bovino: 38.0-39.5 C | 40-80 BPM"));
      Serial.println();

      if (vitals.bpmCount > 0 && vitals.tempCount > 0) {
        sendToBackend(bpmAvg, tempAvg);
      } else {
        Serial.println(F("Datos insuficientes para enviar."));
      }

      appState = STATE_DONE;
      break;
    }

    case STATE_DONE:
      printHeader("Medicion Finalizada");
      Serial.println(F("Escribe 'si' para medir de nuevo."));
      if (waitForUserConfirmation()) {
        appState = STATE_WAIT_MAX_PROMPT;
      }
      delay(10000);
      break;
  }
}
