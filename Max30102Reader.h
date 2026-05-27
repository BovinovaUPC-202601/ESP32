#pragma once
#include <Arduino.h>
#include <Wire.h>
#include "MAX30105.h"

class Max30102Reader {
public:
  Max30102Reader();

  void begin();
  void fillInitialBuffers();
  void stepAndComputeAndPrint();

  // Getters
  int32_t getHeartRate() const { return avgHeartRate; }
  bool    isHeartRateValid() const { return heartRateValid && fingerDetected; }

  int32_t getSpo2() const { return avgSpo2; }
  bool    isSpo2Valid() const { return spo2Valid && fingerDetected; }

  bool isFingerDetected() const { return fingerDetected; }

private:
  MAX30105 sensor;

  // Configuración de muestreo
  static const int SAMPLE_RATE    = 100;   // Hz - muestreo más rápido para mejor detección
  static const int BUFFER_SIZE    = 150;   // 1.5 segundos de datos
  static const int BPM_SAMPLES    = 8;     // Muestras para promedio de BPM
  static const int SPO2_SAMPLES   = 5;     // Muestras para promedio de SpO2

  // Umbrales
  static const uint32_t FINGER_THRESHOLD = 50000;  // Umbral para detectar dedo
  static const int MIN_BPM = 40;
  static const int MAX_BPM = 180;
  static const int MIN_SPO2 = 70;
  static const int MAX_SPO2 = 100;

  // Buffers de señal
  uint32_t irBuffer[BUFFER_SIZE];
  uint32_t redBuffer[BUFFER_SIZE];
  int bufferIndex;

  // Variables para detección de picos
  float irACPrev;
  float irDCPrev;
  uint32_t lastBeatTime;
  
  // Arrays para promedio móvil
  int bpmHistory[BPM_SAMPLES];
  int bpmHistoryIndex;
  int bpmHistoryCount;

  int spo2History[SPO2_SAMPLES];
  int spo2HistoryIndex;
  int spo2HistoryCount;

  // Resultados
  int32_t avgHeartRate;
  bool heartRateValid;
  int32_t avgSpo2;
  bool spo2Valid;
  bool fingerDetected;

  // Métodos internos
  bool collectSample(uint32_t &ir, uint32_t &red);
  void detectBeat(uint32_t ir, uint32_t red);
  void calculateSpO2();
  int calculateMovingAverage(int* history, int count);
  float lowPassFilter(float current, float previous, float alpha);
};
