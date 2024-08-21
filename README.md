# Druida
Gestor de Placas:

ESP32 (v2.0.17) (No usar la version 3.x.x)

Gestor de Librerias:

RTCLib de Adafruit (v2.1.4)

DHT sensor library de Adafruit (v1.4.6)

NTPClient de Fabrice Weinberg (v3.2.1)

UniversalTelegramBot de Brian Lough (v1.3.0)

IRremoteESP8266 de David Conran (v2.8.6)

DallasTemperature de Miles Burton (v3.9.0)


El programa es capaz de controlar 4 reles independientes. 
Pueden funcionar en modo Manual, o Automatico.
Rele 1: Sube parametro (Hum, Temp, DPV, TempA)
Rele 2: Baja parametro (Hum, Temp, DPV, TempA) 
Rele 3: Timer diario (Ej: Luz) (Hora encendido / Hora Apagado)
Rele 4: Timer diario + semanal (Ej: Riego) (Hora Enc / Hora Apag) + (DiaRiego / DiaNoRiego)
