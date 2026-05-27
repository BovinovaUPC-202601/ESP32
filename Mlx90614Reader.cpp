#include "Mlx90614Reader.h"

Mlx90614Reader::Mlx90614Reader()
  : mlx() {}

void Mlx90614Reader::begin() {
  Wire.setClock(100000);  // MLX90614 funciona mejor a 100kHz

  if (!mlx.begin()) {
    Serial.println(F("ERROR: MLX90614 no encontrado. Revisa las conexiones."));
    while (true) delay(1000);
  }

  Serial.println(F("MLX90614 inicializado correctamente"));
}

double Mlx90614Reader::readAmbientTemp() {
  return mlx.readAmbientTempC();
}

double Mlx90614Reader::readObjectTemp() {
  double temp = mlx.readObjectTempC();
  
  if (temp < 20.0 || temp > 45.0) {
    return -1.0;  // Temperatura fuera de rango para cuerpo humano
  }
  return temp;
}

void Mlx90614Reader::printOnce() {
  double tempAmbiente = readAmbientTemp();
  double tempObjeto   = readObjectTemp();

  Serial.print(F("Temp ambiente: "));
  Serial.print(tempAmbiente, 1);
  Serial.print(F(" C  |  Temp objeto: "));
  if (tempObjeto > 0) {
    Serial.print(tempObjeto, 1);
    Serial.println(F(" C"));
  } else {
    Serial.println(F("-- (acerca el sensor)"));
  }
}
