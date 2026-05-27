#pragma once
#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_MLX90614.h>

class Mlx90614Reader {
public:
  Mlx90614Reader();

  void begin();
  void printOnce();

  double readAmbientTemp();
  double readObjectTemp();  // Retorna -1.0 si está fuera de rango

private:
  Adafruit_MLX90614 mlx;
};
