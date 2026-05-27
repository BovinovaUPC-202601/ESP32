#include "Max30102Reader.h"

Max30102Reader::Max30102Reader()
  : bufferIndex(0),
    irACPrev(0),
    irDCPrev(0),
    lastBeatTime(0),
    bpmHistoryIndex(0),
    bpmHistoryCount(0),
    spo2HistoryIndex(0),
    spo2HistoryCount(0),
    avgHeartRate(0),
    heartRateValid(false),
    avgSpo2(0),
    spo2Valid(false),
    fingerDetected(false) {
  
  // Inicializar historiales
  for (int i = 0; i < BPM_SAMPLES; i++) bpmHistory[i] = 0;
  for (int i = 0; i < SPO2_SAMPLES; i++) spo2History[i] = 0;
}

void Max30102Reader::begin() {
  // I2C para ESP32 - SDA=21, SCL=22
  Wire.begin(21, 22);
  Wire.setClock(400000);  // 400 kHz para mejor respuesta

  if (!sensor.begin(Wire, I2C_SPEED_FAST)) {
    Serial.println(F("ERROR: MAX30102 no encontrado. Revisa el cableado."));
    while (true) delay(1000);
  }

  byte ledBrightness = 60;    // Brillo medio-alto (0-255)
  byte sampleAverage = 4;     // Promedio de 4 muestras
  byte ledMode       = 2;     // Modo Red + IR
  int  sampleRate    = 100;   // 100 muestras/segundo
  int  pulseWidth    = 411;   // Ancho de pulso máximo para mejor señal
  int  adcRange      = 4096;  // Rango ADC reducido para mejor resolución

  sensor.setup(ledBrightness, sampleAverage, ledMode,
               sampleRate, pulseWidth, adcRange);

  sensor.setPulseAmplitudeRed(0x1F);  // Amplitud LED rojo
  sensor.setPulseAmplitudeIR(0x1F);   // Amplitud LED IR
  sensor.setPulseAmplitudeGreen(0);   // LED verde apagado

  Serial.println(F("MAX30102 inicializado correctamente"));
  Serial.println(F("Configuracion optimizada para deteccion de pulso"));
}

bool Max30102Reader::collectSample(uint32_t &ir, uint32_t &red) {
  sensor.check();
  
  if (sensor.available()) {
    red = sensor.getRed();
    ir  = sensor.getIR();
    sensor.nextSample();
    return true;
  }
  return false;
}

float Max30102Reader::lowPassFilter(float current, float previous, float alpha) {
  return alpha * current + (1.0f - alpha) * previous;
}

int Max30102Reader::calculateMovingAverage(int* history, int count) {
  if (count == 0) return 0;
  
  long sum = 0;
  for (int i = 0; i < count; i++) {
    sum += history[i];
  }
  return sum / count;
}

void Max30102Reader::detectBeat(uint32_t ir, uint32_t red) {
  // Verificar si hay dedo en el sensor
  fingerDetected = (ir > FINGER_THRESHOLD);
  
  if (!fingerDetected) {
    heartRateValid = false;
    return;
  }

  // Filtro DC para obtener componente AC (la señal pulsátil)
  float irDC = lowPassFilter((float)ir, irDCPrev, 0.02f);  // Filtro muy lento para DC
  float irAC = (float)ir - irDC;
  
  // Filtro paso bajo para suavizar la señal AC
  irAC = lowPassFilter(irAC, irACPrev, 0.3f);

  // Detección de cruce por cero (de negativo a positivo) - indica un latido
  static bool wasNegative = false;
  static uint32_t lastCrossTime = 0;
  static int beatsDetected = 0;

  bool isNegative = (irAC < 0);
  
  if (wasNegative && !isNegative) {
    // Cruce de negativo a positivo detectado
    uint32_t currentTime = millis();
    uint32_t timeDiff = currentTime - lastCrossTime;
    
    // Validar intervalo (corresponde a 40-180 BPM)
    // 40 BPM = 1500ms entre latidos, 180 BPM = 333ms entre latidos
    if (timeDiff > 333 && timeDiff < 1500 && lastCrossTime > 0) {
      int instantBPM = 60000 / timeDiff;
      
      // Solo aceptar si está en rango válido
      if (instantBPM >= MIN_BPM && instantBPM <= MAX_BPM) {
        // Agregar al historial de promedio móvil
        bpmHistory[bpmHistoryIndex] = instantBPM;
        bpmHistoryIndex = (bpmHistoryIndex + 1) % BPM_SAMPLES;
        if (bpmHistoryCount < BPM_SAMPLES) bpmHistoryCount++;
        
        // Calcular promedio
        avgHeartRate = calculateMovingAverage(bpmHistory, bpmHistoryCount);
        heartRateValid = (bpmHistoryCount >= 3);  // Necesitamos al menos 3 muestras
        beatsDetected++;
      }
    }
    lastCrossTime = currentTime;
  }
  
  wasNegative = isNegative;
  irACPrev = irAC;
  irDCPrev = irDC;
}

void Max30102Reader::calculateSpO2() {
  if (!fingerDetected || bufferIndex < BUFFER_SIZE) {
    spo2Valid = false;
    return;
  }

  // Calcular DC y AC para IR y Red
  uint32_t irMax = 0, irMin = UINT32_MAX;
  uint32_t redMax = 0, redMin = UINT32_MAX;
  uint64_t irSum = 0, redSum = 0;

  for (int i = 0; i < BUFFER_SIZE; i++) {
    if (irBuffer[i] > irMax) irMax = irBuffer[i];
    if (irBuffer[i] < irMin) irMin = irBuffer[i];
    if (redBuffer[i] > redMax) redMax = redBuffer[i];
    if (redBuffer[i] < redMin) redMin = redBuffer[i];
    irSum += irBuffer[i];
    redSum += redBuffer[i];
  }

  float irDC = (float)irSum / BUFFER_SIZE;
  float redDC = (float)redSum / BUFFER_SIZE;
  float irAC = (float)(irMax - irMin);
  float redAC = (float)(redMax - redMin);

  // Evitar división por cero
  if (irDC < 1 || redDC < 1 || irAC < 1) {
    spo2Valid = false;
    return;
  }

  // Calcular ratio R
  float R = (redAC / redDC) / (irAC / irDC);

  // Fórmula empírica para SpO2 (calibración típica)
  // SpO2 = 110 - 25 * R (aproximación lineal común)
  int calculatedSpO2 = (int)(110.0f - 25.0f * R);

  // Validar rango
  if (calculatedSpO2 >= MIN_SPO2 && calculatedSpO2 <= MAX_SPO2) {
    // Agregar al historial
    spo2History[spo2HistoryIndex] = calculatedSpO2;
    spo2HistoryIndex = (spo2HistoryIndex + 1) % SPO2_SAMPLES;
    if (spo2HistoryCount < SPO2_SAMPLES) spo2HistoryCount++;

    avgSpo2 = calculateMovingAverage(spo2History, spo2HistoryCount);
    spo2Valid = (spo2HistoryCount >= 2);
  }
}

void Max30102Reader::fillInitialBuffers() {
  Serial.println(F(""));
  Serial.println(F("=============================================="));
  Serial.println(F("  Preparando lectura de signos vitales"));
  Serial.println(F("=============================================="));
  Serial.println(F(""));
  Serial.println(F("Instrucciones:"));
  Serial.println(F("1. Coloca tu dedo INDICE sobre el sensor"));
  Serial.println(F("2. Aplica presion SUAVE y constante"));
  Serial.println(F("3. NO muevas el dedo durante la medicion"));
  Serial.println(F("4. Respira normalmente"));
  Serial.println(F(""));
  Serial.println(F("Cargando buffer inicial..."));

  // Limpiar historiales
  bpmHistoryCount = 0;
  bpmHistoryIndex = 0;
  spo2HistoryCount = 0;
  spo2HistoryIndex = 0;
  bufferIndex = 0;
  irACPrev = 0;
  irDCPrev = 0;

  // Llenar buffer inicial
  while (bufferIndex < BUFFER_SIZE) {
    uint32_t ir, red;
    if (collectSample(ir, red)) {
      irBuffer[bufferIndex] = ir;
      redBuffer[bufferIndex] = red;
      detectBeat(ir, red);
      bufferIndex++;
    }
    delay(5);
  }

  Serial.println(F("Buffer listo. Iniciando medicion continua..."));
  Serial.println(F(""));
}

void Max30102Reader::stepAndComputeAndPrint() {
  uint32_t ir, red;
  
  if (!collectSample(ir, red)) {
    return;  // No hay nueva muestra disponible
  }

  // Actualizar buffer circular
  memmove(irBuffer, irBuffer + 1, (BUFFER_SIZE - 1) * sizeof(uint32_t));
  memmove(redBuffer, redBuffer + 1, (BUFFER_SIZE - 1) * sizeof(uint32_t));
  irBuffer[BUFFER_SIZE - 1] = ir;
  redBuffer[BUFFER_SIZE - 1] = red;

  // Detectar latido
  detectBeat(ir, red);
  
  // Calcular SpO2 cada 50 muestras
  static int sampleCount = 0;
  sampleCount++;
  if (sampleCount >= 50) {
    calculateSpO2();
    sampleCount = 0;
  }

  // Imprimir resultados cada 500ms aproximadamente
  static uint32_t lastPrintTime = 0;
  if (millis() - lastPrintTime >= 500) {
    lastPrintTime = millis();

    Serial.print(F("IR="));
    Serial.print(ir);
    
    if (!fingerDetected) {
      Serial.println(F("  | COLOCA TU DEDO EN EL SENSOR"));
    } else {
      Serial.print(F("  | BPM="));
      if (heartRateValid) {
        Serial.print(avgHeartRate);
      } else {
        Serial.print(F("--"));
      }
      
      Serial.print(F("  SpO2="));
      if (spo2Valid) {
        Serial.print(avgSpo2);
        Serial.print(F("%"));
      } else {
        Serial.print(F("--"));
      }
      
      Serial.println(F("  | Mantén el dedo quieto"));
    }
  }
}
