
// Proyecto: Druida BOT de DataDruida
// Autor: Bryan Murphy
// Año: 2025
// Licencia: MIT

#include "config.h"

// ================== PROTOTIPOS (coinciden con el uso) ==================
int realDaysSince(uint32_t startEpoch, int tzOffsetSec = -3 * 3600);
int virtualDaysSince(uint32_t startEpoch, int horasLuz, int horasOscuridad, int tzOffsetSec = -3 * 3600);

inline void setRelayActiveLow(uint8_t pin, bool on) {
  digitalWrite(pin, on ? LOW : HIGH);
}

// ================== RELÉ 5 (igual que lo tenías) ==================
inline void setRelay5(bool on) {
  uint8_t level = on ? (R5_ACTIVO_EN_HIGH ? HIGH : LOW)
                     : (R5_ACTIVO_EN_HIGH ? LOW  : HIGH);
  digitalWrite(RELAY5, level);
  R5estado = level;
}

// Estado físico interpretado (ON real según polaridad)
inline bool r5FisicamenteOn() {
  return R5_ACTIVO_EN_HIGH ? (R5estado == HIGH) : (R5estado == LOW);
}

// Helper para descomponer minutos absolutos a HH:MM
static inline void splitHM(int absMin, int &h, int &m) {
  absMin = (absMin % 1440 + 1440) % 1440;
  h = absMin / 60;
  m = absMin % 60;
}


constexpr uint32_t SEC_PER_HOUR = 3600UL;
constexpr uint32_t SEC_PER_DAY  = 86400UL;

// ===== Día real (24 h), contado desde 1
int realDaysSince(uint32_t startEpoch, int tzOffsetSec) {
  if (startEpoch == 0) return 0;

  // trabajar en 64 bits para evitar desbordes y permitir tz negativos
  int64_t nowLocal   = nowUtcSec64()       + (int64_t)tzOffsetSec;
  int64_t startLocal = (int64_t)startEpoch + (int64_t)tzOffsetSec;

  if (nowLocal < startLocal) return 0;

  uint64_t elapsed = (uint64_t)(nowLocal - startLocal);
  uint32_t daysCompleted = (uint32_t)(elapsed / SEC_PER_DAY);
  return (int)(daysCompleted + 1);
}

// ===== Día virtual (horasLuz + horasOscuridad), contado desde 1
int virtualDaysSince(uint32_t startEpoch,
                     int horasLuz,
                     int horasOscuridad,
                     int tzOffsetSec) {
  if (startEpoch == 0) return 0;
  if (horasLuz < 0 || horasOscuridad < 0) return 0;

  // duración del ciclo en segundos (64 bits)
  uint64_t cycleSec = (uint64_t)((uint32_t)horasLuz + (uint32_t)horasOscuridad) * (uint64_t)SEC_PER_HOUR;
  if (cycleSec == 0) return 0;

  int64_t nowLocal   = nowUtcSec64()       + (int64_t)tzOffsetSec;
  int64_t startLocal = (int64_t)startEpoch + (int64_t)tzOffsetSec;

  if (nowLocal < startLocal) return 0;

  uint64_t elapsed = (uint64_t)(nowLocal - startLocal);
  uint64_t cyclesCompleted = elapsed / cycleSec;   // floor
  return (int)(cyclesCompleted + 1);
}





void handleNewMessages(int numNewMessages);

bool waitForI2CBusFree(uint8_t sdaPin, uint8_t sclPin, unsigned long timeout = 1000) {
  pinMode(sdaPin, INPUT_PULLUP);
  pinMode(sclPin, INPUT_PULLUP);

  unsigned long startTime = millis();
  while (digitalRead(sdaPin) == LOW || digitalRead(sclPin) == LOW) {
    if (millis() - startTime > timeout) {
      Serial.println("⚠️ Timeout esperando que el bus I2C esté libre.");
      return false;
    }
    delay(10);
  }
  return true;
}


void setup() {
  // ----- Ventana segura anti-brownout (habilita persistencia luego en loop) -----
  bootMs = millis();   // canPersist se habilita en loop() tras ~15 s

  // ----- Buses / periféricos base -----
  waitForI2CBusFree(SDA_NANO, SCL_NANO);
  I2CNano.begin(SDA_NANO, SCL_NANO);
  Wire.begin();

  Serial.begin(115200);
  EEPROM.begin(512);
  Wire.setClock(100000);   // baja I²C a 100 kHz (probá 50000 si sigue)
  Wire.setTimeOut(20);     // evita cuelgues por NACK/ruido (opcional pero útil)


  rtc.begin();
  aht.begin(&Wire);        // en vez de aht.begin();


  // Watchdog
  esp_task_wdt_init(WDT_TIMEOUT, true);
  esp_task_wdt_add(NULL);

  // ----- Motivo de reinicio (por si lo usás para el mensaje de Telegram) -----
  esp_reset_reason_t resetReason = esp_reset_reason();

  // ----- Cargar configuración ANTES de tocar relés o inicializaciones largas -----
  Carga_General();
  debugPrintConfig();

  // ----- Configuración de pines de relé -----
  pinMode(RELAY1, OUTPUT);
  pinMode(RELAY2, OUTPUT);
  pinMode(RELAY3, OUTPUT);
  pinMode(RELAY4, OUTPUT);
  pinMode(RELAY5, OUTPUT);

  // Estados seguros para R1–R4
  digitalWrite(RELAY1, HIGH);
  digitalWrite(RELAY2, HIGH);
  digitalWrite(RELAY3, HIGH);
  digitalWrite(RELAY4, HIGH);

  // Aplicar inmediatamente el último estado guardado de R5 (activo en HIGH)
  setRelay5(estadoR5 == 1);

  // ----- Inicializar pantalla OLED con reintentos -----
  {
    int retriesDisplayInit = 0;
    while (!display.begin(0x3C, true)) {  // true = soft reset
      Serial.println(F("Error al inicializar la OLED (SH1106), reintentando..."));
      retriesDisplayInit++;
      delay(500);
      if (retriesDisplayInit > 5) {
        Serial.println(F("No se pudo inicializar la OLED, reiniciando..."));
        ESP.restart();
      }
      yield();
    }
    display.clearDisplay();
    display.display();
  }

  // ----- Servo / dimmer -----
  dimmerServo.attach(SERVO);
  moveServoSlowly(currentPosition); // mover a la última posición guardada

  // ----- UI local -----
  mostrarMensajeBienvenida();

  // ===========================
  //   Conectividad de Red
  // ===========================
  if (modoWiFi == 1) {
    // Modo cliente (STA)
    connectToWiFi(ssid.c_str(), password.c_str());

    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("Conectado a Wi-Fi exitosamente.");

      // TLS para Telegram (si aplica)
      secured_client.setCACert(TELEGRAM_CERTIFICATE_ROOT);

      // Servidor web
      startWebServer();

      // Mensajes de arranque (Telegram)
      String motivoReinicio = obtenerMotivoReinicio(); // usa resetReason internamente si querés
      bot.sendMessage(chat_id, "Druida Bot is ON (" + motivoReinicio + ")");

      String keyboardJson = "[[\"STATUS\"], [\"CONTROL\"], [\"CONFIG\"]]";
      bot.sendMessageWithReplyKeyboard(chat_id, "MENU PRINCIPAL:", "", keyboardJson, true);

      // ----------- Sincronizar NTP -----------
      static const char* NTP_SERVERS[] = {
        "time.google.com",
        "time.cloudflare.com",
        "ar.pool.ntp.org",
        "pool.ntp.org",
        nullptr
      };
      const long GMT_OFFSET_SEC = 0;
      const int  DST_OFFSET_SEC = 0;

      bool horaSincronizada = false;
      for (uint8_t i = 0; NTP_SERVERS[i] != nullptr && !horaSincronizada; ++i) {
        Serial.printf("\n⏱️  Probando NTP: %s\n", NTP_SERVERS[i]);
        configTime(GMT_OFFSET_SEC, DST_OFFSET_SEC, NTP_SERVERS[i]);

        time_t now = 0;
        uint32_t t0 = millis();
        while (now < 24 * 3600 && (millis() - t0) < 6000UL) {
          now = time(nullptr);
          delay(200);
          yield();
        }
        horaSincronizada = (now >= 24 * 3600);
      }

      if (horaSincronizada) {
        Serial.println("✅ Hora NTP sincronizada");
        struct tm timeinfo;
        if (getLocalTime(&timeinfo)) {
          rtc.adjust(DateTime(
            timeinfo.tm_year + 1900,
            timeinfo.tm_mon + 1,
            timeinfo.tm_mday,
            timeinfo.tm_hour,
            timeinfo.tm_min,
            timeinfo.tm_sec
          ));
          const char* days[] = { "Domingo","Lunes","Martes","Miércoles","Jueves","Viernes","Sábado" };
          diaNumero = timeinfo.tm_wday;
          Serial.print("📆 Día de hoy: ");
          Serial.println(days[diaNumero]);
        }
      } else {
        Serial.println("❌ NTP no sincronizado, continúo sin hora precisa");
      }

    } else {
      // Fallback: sin Wi-Fi → AP + servidor web para configurar
      Serial.println("No se pudo conectar a Wi-Fi. Cambio a Modo AP.");
      startAccessPoint();
      startWebServer();
    }

  } else {
    // Modo AP directo
    Serial.println("\nModo AP activado para configuración.");
    startAccessPoint();
    startWebServer();
  }

  // ----- Info en Serial -----
  Serial.print("chat_id: ");
  Serial.println(chat_id);
  Serial.println("Menu Serial:");
  Serial.println("1. Modificar Red WiFi");
  Serial.println("2. Modificar Chat ID");
  Serial.println("3. Modificar Señal IR");
  Serial.println("4. Mostrar sensores");
}



// Sincroniza pin físico y variable de estado
inline void setRelay2(bool on) {
  digitalWrite(RELAY2, on ? HIGH : LOW);
  R2estado = on ? HIGH : LOW;
}

// Verifica rango y NaN en una sola línea
inline bool sensorOK(float v, float lo, float hi) {
  return !isnan(v) && v >= lo && v <= hi;
}

// Arranca (o reinicia) la pausa inteligente
inline void startPause() {
  enEsperaR2     = true;
  tiempoInicioR2 = millis();
  tiempoEsperaR2 = R2_WAIT_MS;

  // Copia lecturas para debug
  humedadReferenciaR2     = humedad;
  temperaturaReferenciaR2 = temperature;
  dpvReferenciaR2         = DPV;
}


void loop() {
  if (!canPersist && millis() - bootMs > 15000UL) canPersist = true;
  server.handleClient();
  unsigned long currentMillis = millis();
  unsigned long intervaloGoogle = tiempoGoogle * 60UL * 1000UL;
  unsigned long intervaloTelegram = tiempoTelegram * 60UL * 1000UL;

  // Verifica la conexión WiFi a intervalos regulares
  if (currentMillis - previousMillis >= wifiCheckInterval && modoWiFi == 1) {
    previousMillis = currentMillis;
    checkWiFiConnection();
  }

    // Si han pasado 20 segundos, enviamos el mensaje
  if (currentMillis - previousMillisWD >= interval) {
    previousMillis = currentMillis;
    Serial.println("PING");
  }

    if (WiFi.status() == WL_CONNECTED) {
      if (millis() - bot_lasttime > BOT_MTBS) {
        int numNewMessages = bot.getUpdates(bot.last_message_received + 1);
        int maxMessagesToProcess = 10;  // límite para evitar bloqueo
        int messagesProcessed = 0;
        while (numNewMessages && messagesProcessed < maxMessagesToProcess) {
          Serial.println("got response");
          handleNewMessages(numNewMessages);
          numNewMessages = bot.getUpdates(bot.last_message_received + 1);
          delay(10);
          messagesProcessed++;
        }
        bot_lasttime = millis();
      }
    }

  static unsigned long tLast = 0;
  unsigned long t = millis();
  if (t - tLast >= 10000UL) {   // cada 10 s (ajustá a gusto)
    tLast = t;
    tickDaily();
  }
  requestSensorData();

  sensors_event_t humidity, temp;
  aht.getEvent(&humidity, &temp);

  float temperature = temp.temperature;
  float humedad = humidity.relative_humidity;

  if (temperature > maxTemp) {
    maxTemp = temperature;
  }

  if (temperature < minTemp) {
    minTemp = temperature;
  }

  if (humedad > maxHum) {
    maxHum = humedad;
  }

  if (humedad < minHum) {
    minHum = humedad;
  }


  int serial = Serial.read();
  //ACA SE PONE LA HORA EN EL RTC

  DateTime now = rtc.now();
  int hour = now.hour();
  int minute = now.minute();
  int second = now.second();
  int day = now.dayOfTheWeek();
  unsigned long nowMin = now.unixtime() / 60UL;  // minutos desde época Unix

  hour -= 3;
  if (hour < 0) {
    hour = 24 + hour;
  }




if (currentMillis - previousMillisGoogle >= intervaloGoogle) {
  previousMillisGoogle = currentMillis;

  if (WiFi.status() == WL_CONNECTED) {
    // Enviar datos a Google Sheets
    sendDataToGoogleSheets();

    // Resetear máximos y mínimos
    maxHum = -999;
    minHum = 999;
    maxTemp = -999;
    minTemp = 999;
  }
}

if (currentMillis - previousMillisTelegram >= intervaloTelegram) {
  previousMillisTelegram = currentMillis;

  if (WiFi.status() == WL_CONNECTED) {
    // Leer datos actualizados
    sensors_event_t humidity, temp;
    aht.getEvent(&humidity, &temp);
    float temperature = temp.temperature;
    float humedad = humidity.relative_humidity;
    requestSensorData();

    // Obtener hora ajustada
    DateTime now = rtc.now();
    int horaBot = now.hour() - 3;
    if (horaBot < 0) horaBot += 24;

    String dateTime = "📅 Fecha y Hora: " + String(now.day()) + "/" + String(now.month()) + "/" + String(now.year()) + " "
                    + horaBot + ":" + String(now.minute()) + ":" + String(now.second()) + "\n";

    String statusMessage = "🌡️ Temperatura: " + String(temperature, 1) + " °C\n";
    statusMessage += "💧 Humedad: " + String(humedad, 1) + " %\n";
    statusMessage += "🌬️ VPD: " + String(DPV, 1) + " hPa\n";
    statusMessage += "🌱 Humedad 1: " + String(sensor1Value) + " %\n";
    statusMessage += "🌱 Humedad 2: " + String(sensor2Value) + " %\n";
    statusMessage += "🌱 Humedad 3: " + String(sensor3Value) + " %\n";
    statusMessage += "🧪 pH: " + String(sensorPH, 2) + "\n";
    statusMessage += dateTime;

    bot.sendMessage(chat_id, statusMessage, "");
  }
}

  if (temperature > 40) {
    temperature = 40;
    if (WiFi.status() == WL_CONNECTED) {
      bot.sendMessage(chat_id, "Alerta, temperatura demasiado alta");
    }
  }

  DPV = calcularDPV(temperature, humedad);


  if (serial == '1') {
    Serial.println("Cambiar de red WiFi: ");
    modificarWifi();
    serial = 0;
  }

  if (serial == '2') {
    modificarChatID();
    serial = 0;
  }

  /*if (serial == '3') {
    modificarValoresArray(false); //MODO AUTOMATICO
    serial = 0;
  }*/

  if (serial == '4') {
    sendDataToGoogleSheets();
    serial = 0;
  }

  /*if (serial == '5') {
    irsend.sendRaw(rawData, 72, 38);
    delay(200);
    mostrarArray();
    serial = 0;
  }*/

  if (serial == '6') {
    moveServoSlowly(180);
    delay(200);
  }

  if (serial == '7') {
    moveServoSlowly(0);
    delay(200);
  }


  if (reset == 1) {
    esp_restart();
  }

  //MOSTRAR VALORES POR PANTALLA OLED:


  //manejarReles();

  //MODO MANUAL R1
  if (modoR1 == MANUAL) {
    if (estadoR1 == 1 && R1estado == HIGH) {
      digitalWrite(RELAY1, LOW);
      R1estado = LOW;
    }

    if (estadoR1 == 0 && R1estado == LOW) {
      digitalWrite(RELAY1, HIGH);
      R1estado = HIGH;
    }
  }

// MODO MANUAL R5 (blindado)
if (modoR5 == MANUAL) {
  bool deberiaOn = (estadoR5 == 1);
  bool fisicoOn  = r5FisicamenteOn();

  // Si el estado lógico (persistido) no coincide con el físico, corregimos
  if (deberiaOn != fisicoOn) {
    setRelay5(deberiaOn);   // aplica nivel correcto y sincroniza R5estado
  }
}


  //MODO MANUAL R2
  if (modoR2 == MANUAL) {
    if (estadoR2 == 1 && R2estado == HIGH) {
      digitalWrite(RELAY2, LOW);
      R2estado = LOW;
    }
    if (estadoR2 == 0 && R2estado == LOW) {
      digitalWrite(RELAY2, HIGH);
      R2estado = HIGH;
    }
  }

    //MODO MANUAL R2 IR
  /*if (modoR2ir == MANUAL) {
    if (estadoR2ir == 1 && R2irestado == HIGH) {
      irsend.sendRaw(rawData, 72, 38);  // Envía la señal IR ajustada con frecuencia de 38 kHz
      R2irestado = LOW;
    }
    if (estadoR2ir == 0 && R2irestado == LOW) {
      irsend.sendRaw(rawData, 72, 38);  // Envía la señal IR ajustada con frecuencia de 38 kHz
      R2irestado = HIGH;
    }
  }*/

  //MODO MANUAL R3
  if (modoR3 == MANUAL) {
    if (estadoR3 == 1 && R3estado == HIGH) {
      digitalWrite(RELAY3, LOW);
      R3estado = LOW;
    }
    if (estadoR3 == 0 && R3estado == LOW) {
      digitalWrite(RELAY3, HIGH);
      R3estado = HIGH;
    }
  }

  //MODO MANUAL R4
  if (modoR4 == MANUAL) {
    if (estadoR4 == 1 && R4estado == HIGH) {
      digitalWrite(RELAY4, LOW);
      R4estado = LOW;
    }
    if (estadoR4 == 0 && R4estado == LOW) {
      digitalWrite(RELAY4, HIGH);
      R4estado = HIGH;
    }
  }

  /*if (modoR1 == AUTO) {

  if (paramR1 == H) {

      if (humedad < minR1 && R1estado == HIGH) {
        digitalWrite(RELAY1, LOW);
        R1estado = LOW;
      }
      if (humedad > maxR1 && R1estado == LOW) {
        digitalWrite(RELAY1, HIGH);
        R1estado = HIGH;
      }
  }

  if (paramR1 == T) {

      if (temperature < minR1 && R1estado == HIGH) {
        digitalWrite(RELAY1, LOW);
        R1estado = LOW;
      }
      if (temperature > maxR1 && R1estado == LOW) {
        digitalWrite(RELAY1, HIGH);
        R1estado = HIGH;
      }
  }

  if (paramR1 == D) {
    if (DPV < minR1 && R1estado == HIGH) {
      digitalWrite(RELAY1, LOW);
      R1estado = LOW;
    }
    if (DPV > maxR1 && R1estado == LOW) {
      digitalWrite(RELAY1, HIGH);
      R1estado = HIGH;
    }
  }

}*/


//NUEVO MODO MIXTO PARA SUBIR Y BAJAR

if (modoR1 == AUTO) {

  if (paramR1 == H) {
    if (direccionR1 == 0) {
      // SUBIR humedad: se apaga si está bajo, se enciende si está alto
      if (humedad < minR1 && R1estado == HIGH) {
        digitalWrite(RELAY1, LOW);
        R1estado = LOW;
      }
      if (humedad > maxR1 && R1estado == LOW) {
        digitalWrite(RELAY1, HIGH);
        R1estado = HIGH;
      }
    } else {
      // BAJAR humedad: se enciende si está alto, se apaga si está bajo
      if (humedad > maxR1 && R1estado == LOW) {
        digitalWrite(RELAY1, HIGH);
        R1estado = HIGH;
      }
      if (humedad < minR1 && R1estado == HIGH) {
        digitalWrite(RELAY1, LOW);
        R1estado = LOW;
      }
    }
  }

  if (paramR1 == T) {
    if (direccionR1 == 0) {
      // SUBIR temperatura
      if (temperature < minR1 && R1estado == HIGH) {
        digitalWrite(RELAY1, LOW);
        R1estado = LOW;
      }
      if (temperature > maxR1 && R1estado == LOW) {
        digitalWrite(RELAY1, HIGH);
        R1estado = HIGH;
      }
    } else {
      // BAJAR temperatura
      if (temperature > maxR1 && R1estado == LOW) {
        digitalWrite(RELAY1, HIGH);
        R1estado = HIGH;
      }
      if (temperature < minR1 && R1estado == HIGH) {
        digitalWrite(RELAY1, LOW);
        R1estado = LOW;
      }
    }
  }

  if (paramR1 == D) {
    if (direccionR1 == 0) {
      // SUBIR DPV
      if (DPV < minR1 && R1estado == HIGH) {
        digitalWrite(RELAY1, LOW);
        R1estado = LOW;
      }
      if (DPV > maxR1 && R1estado == LOW) {
        digitalWrite(RELAY1, HIGH);
        R1estado = HIGH;
      }
    } else {
      // BAJAR DPV
      if (DPV > maxR1 && R1estado == LOW) {
        digitalWrite(RELAY1, HIGH);
        R1estado = HIGH;
      }
      if (DPV < minR1 && R1estado == HIGH) {
        digitalWrite(RELAY1, LOW);
        R1estado = LOW;
      }
    }
  }

}

if (modoR5 == AUTO) {
  bool fisicoOn = r5FisicamenteOn();

  // === HUMEDAD ===
  if (paramR5 == 'H') {
    if (direccionR5 == 0) {
      // SUBIR humedad: encender si < min, apagar si > max
      if (!fisicoOn && humedad < minR5) {
        setRelay5(true);
        estadoR5 = 1;
        fisicoOn = true;
      }
      if (fisicoOn && humedad > maxR5) {
        setRelay5(false);
        estadoR5 = 0;
        fisicoOn = false;
      }
    } else {
      // BAJAR humedad: encender si > max, apagar si < min
      if (!fisicoOn && humedad > maxR5) {
        setRelay5(true);
        estadoR5 = 1;
        fisicoOn = true;
      }
      if (fisicoOn && humedad < minR5) {
        setRelay5(false);
        estadoR5 = 0;
        fisicoOn = false;
      }
    }
  }

  // === TEMPERATURA ===
  if (paramR5 == 'T') {
    if (direccionR5 == 0) {
      // SUBIR temperatura
      if (!fisicoOn && temperature < minR5) {
        setRelay5(true);
        estadoR5 = 1;
        fisicoOn = true;
      }
      if (fisicoOn && temperature > maxR5) {
        setRelay5(false);
        estadoR5 = 0;
        fisicoOn = false;
      }
    } else {
      // BAJAR temperatura
      if (!fisicoOn && temperature > maxR5) {
        setRelay5(true);
        estadoR5 = 1;
        fisicoOn = true;
      }
      if (fisicoOn && temperature < minR5) {
        setRelay5(false);
        estadoR5 = 0;
        fisicoOn = false;
      }
    }
  }

  // === DPV (VPD) ===
  if (paramR5 == 'D') {
    if (direccionR5 == 0) {
      // SUBIR DPV
      if (!fisicoOn && DPV < minR5) {
        setRelay5(true);
        estadoR5 = 1;
        fisicoOn = true;
      }
      if (fisicoOn && DPV > maxR5) {
        setRelay5(false);
        estadoR5 = 0;
        fisicoOn = false;
      }
    } else {
      // BAJAR DPV
      if (!fisicoOn && DPV > maxR5) {
        setRelay5(true);
        estadoR5 = 1;
        fisicoOn = true;
      }
      if (fisicoOn && DPV < minR5) {
        setRelay5(false);
        estadoR5 = 0;
        fisicoOn = false;
      }
    }
  }
}






  // DATA TIMERS
timeOnR3 = horaOnR3 * 60 + minOnR3;
timeOffR3 = horaOffR3 * 60 + minOffR3;
timeOnR4 = horaOnR4 * 60 + minOnR4;
timeOffR4 = horaOffR4 * 60 + minOffR4;
timeOnR1 = horaOnR1 * 60 + minOnR1;
timeOffR1 = horaOffR1 * 60 + minOffR1;

// >>> NUEVO R5
timeOnR5 = horaOnR5 * 60 + minOnR5;
timeOffR5 = horaOffR5 * 60 + minOffR5;

// MODO AUTO R3 (Riego)

// Convierte todo a minutos para facilitar la comparación
int currentTime = hour * 60 + minute;
int startR3 = timeOnR3;
int offR3   = timeOffR3;
int startR4 = timeOnR4;
int offR4   = timeOffR4;
int startR1 = timeOnR1;
int offR1   = timeOffR1;
// >>> NUEVO R5
int startR5 = timeOnR5;
int offR5   = timeOffR5;
int c;

// MODO TIMER R1  (activo-bajo)
if (modoR1 == TIMER) {
  if (startR1 < offR1) {
    // Caso normal: encendido antes que apagado
    if (currentTime >= startR1 && currentTime < offR1) {
      if (R1estado == HIGH){
        digitalWrite(RELAY1, LOW);
        R1estado = LOW;
      }
    } else {
      if (R1estado == LOW) {
        digitalWrite(RELAY1, HIGH);
        R1estado = HIGH;
      }
    }
  } else {
    // Cruza medianoche
    if (currentTime >= startR1 || currentTime < offR1) {
      if (R1estado == HIGH){
        digitalWrite(RELAY1, LOW);
        R1estado = LOW;
      }
    } else {
      if (R1estado == LOW) {
        digitalWrite(RELAY1, HIGH);
        R1estado = HIGH;
      }
    }
  }
}

// >>> MODO TIMER R5 (lógica inversa de R1, activo-alto)
if (modoR5 == TIMER) {
  // Normalización defensiva a 0..1439
  auto normMins = [](int m) -> int {
    m %= 1440;
    if (m < 0) m += 1440;
    return m;
  };
  currentTime = normMins(currentTime);
  startR5     = normMins(startR5);
  offR5       = normMins(offR5);

  // Decidir si estamos dentro de la ventana
  bool inWindow;
  if (startR5 == offR5) {
    // ventana vacía -> siempre OFF
    inWindow = false;
  } else if (startR5 < offR5) {
    // Caso normal (no cruza medianoche)
    inWindow = (currentTime >= startR5) && (currentTime < offR5);
  } else {
    // Cruza medianoche (ej. 22:00 -> 06:00)
    inWindow = (currentTime >= startR5) || (currentTime < offR5);
  }

  bool fisicoOn = r5FisicamenteOn();

  if (inWindow) {
    // Debe estar ON
    if (!fisicoOn) {
      setRelay5(true);   // aplica nivel correcto y sincroniza R5estado
      estadoR5 = 1;      // mantenemos en RAM (no escribimos EEPROM aquí)
    }
  } else {
    // Debe estar OFF
    if (fisicoOn) {
      setRelay5(false);
      estadoR5 = 0;
    }
  }
}




  if (modoR2 == AUTO) {

  if (paramR2 == H) {
    if (isnan(humedad) || humedad < 0 || humedad > 99.9) {
      //bot.sendMessage(chat_id, "⚠️ Alerta: Valor de *humedad* fuera de rango o inválido en R2. Revisa el sensor o el ambiente.", "");
      humedad = 0;
    } else {
      if (humedad > maxR2 && R2estado == HIGH) {
        digitalWrite(RELAY2, LOW);
        R2estado = LOW;
        delay(200);
      }
      if (humedad < minR2 && R2estado == LOW) {
        digitalWrite(RELAY2, HIGH);
        R2estado = HIGH;
        delay(200);
      }
    }
  }

  if (paramR2 == T) {
    if (isnan(temperature) || temperature < -10 || temperature > 50) {
      //bot.sendMessage(chat_id, "⚠️ Alerta: Valor de *temperatura* fuera de rango o inválido en R2. Revisa el sensor o el ambiente.", "");
    } else {
      if (temperature > maxR2 && R2estado == HIGH) {
        digitalWrite(RELAY2, LOW);
        R2estado = LOW;
        delay(200);
      }
      if (temperature < minR2 && R2estado == LOW) {
        digitalWrite(RELAY2, HIGH);
        R2estado = HIGH;
        delay(200);
      }
    }
  }

  if (paramR2 == D) {
    if (DPV > maxR2 && R2estado == HIGH) {
      digitalWrite(RELAY2, LOW);
      R2estado = LOW;
      delay(200);
    }
    if (DPV < minR2 && R2estado == LOW) {
      digitalWrite(RELAY2, HIGH);
      R2estado = HIGH;
      delay(200);
    }
  }

  if (paramR2 == HT) {
    static byte activador = 0; // 0=nada, 1=humedad, 2=temperatura

    bool humedadValida = !(isnan(humedad) || humedad < 0 || humedad > 99.9);
    bool temperaturaValida = !(isnan(temperature) || temperature < -10 || temperature > 50);

    if (!humedadValida) {
      //bot.sendMessage(chat_id, "⚠️ Alerta: Valor de *humedad* fuera de rango o inválido en R2 (HT).", "");
    }

    if (!temperaturaValida) {
      //bot.sendMessage(chat_id, "⚠️ Alerta: Valor de *temperatura* fuera de rango o inválido en R2 (HT).", "");
    }

    // Solo ejecutar lógica si ambos valores son válidos
    if (humedadValida && temperaturaValida) {
      if (R2estado == HIGH) {
        if (humedad > maxR2) {
          digitalWrite(RELAY2, LOW);
          R2estado = LOW;
          activador = 1;
          delay(200);
        } else if (temperature > maxTR2) {
          digitalWrite(RELAY2, LOW);
          R2estado = LOW;
          activador = 2;
          delay(200);
        }
      }
      else if (R2estado == LOW) {
        if ((activador == 1 && humedad < minR2) || 
            (activador == 2 && temperature < minTR2)) {
          digitalWrite(RELAY2, HIGH);
          R2estado = HIGH;
          activador = 0;
          delay(200);
        }
      }
    }
  }
}



// NUEVO MODO AUTO MAS EFICIENTE — OPTIMIZADO

if (modoR2 == AUTOINT) {
  const unsigned long ahora = millis();

  // --- Reevaluación al vencer el tiempo de espera ---
  if (enEsperaR2 && ahora - tiempoInicioR2 >= tiempoEsperaR2) {
    
    auto reevaluar = [&](float valorActual, float min, float max) {
      float mid = (min + max) / 2.0;

      if (valorActual < mid) {
        // No mejoró → apagar y renovar pausa
        if (R2estado == HIGH) {
          digitalWrite(RELAY2, LOW);
          R2estado = LOW;
        }
        tiempoInicioR2 = ahora;
        tiempoEsperaR2 = 10UL * 60UL * 1000UL;
        enEsperaR2 = true;
      } else if (valorActual >= max) {
        // Sigue crítico → encender y renovar pausa
        if (R2estado == LOW) {
          digitalWrite(RELAY2, HIGH);
          R2estado = HIGH;
        }
        tiempoInicioR2 = ahora;
        tiempoEsperaR2 = 10UL * 60UL * 1000UL;
        enEsperaR2 = true;
      } else {
        // Entró en zona segura → fin de pausa
        enEsperaR2 = false;
      }
    };

    if (paramR2 == H)       reevaluar(humedad, minR2, maxR2);
    else if (paramR2 == T)  reevaluar(temperature, minR2, maxR2);
    else if (paramR2 == D)  reevaluar(DPV, minR2, maxR2);
    else if (paramR2 == HT) {
      float midHum  = (minR2  + maxR2 ) / 2.0;
      float midTemp = (minTR2 + maxTR2) / 2.0;

      if (humedad < midHum && temperature < midTemp) {
        if (R2estado == HIGH) {
          digitalWrite(RELAY2, LOW);
          R2estado = LOW;
        }
        tiempoInicioR2 = ahora;
        tiempoEsperaR2 = 10UL * 60UL * 1000UL;
        enEsperaR2 = true;
      } else if (humedad > maxR2 || temperature > maxTR2) {
        if (R2estado == LOW) {
          digitalWrite(RELAY2, HIGH);
          R2estado = HIGH;
        }
        tiempoInicioR2 = ahora;
        tiempoEsperaR2 = 10UL * 60UL * 1000UL;
        enEsperaR2 = true;
      } else {
        enEsperaR2 = false;
      }
    }
  }

  // --- Evaluación normal (sin espera) ---
  else if (!enEsperaR2) {
    bool humedadOk    = !isnan(humedad)    && humedad    >= 0.0  && humedad    <= 99.9;
    bool temperaturaOk= !isnan(temperature)&& temperature>= -10.0&& temperature<= 50.0;

    if (paramR2 == H && humedadOk) {
      if (humedad < minR2 && R2estado == LOW) {
        digitalWrite(RELAY2, HIGH);
        R2estado = HIGH;
        tiempoInicioR2 = ahora;
        tiempoEsperaR2 = 10UL * 60UL * 1000UL;
        enEsperaR2 = true;
      } else if (humedad >= maxR2 && R2estado == HIGH) {
        digitalWrite(RELAY2, LOW);
        R2estado = LOW;
      }
    }

    else if (paramR2 == T && temperaturaOk) {
      if (temperature < minR2 && R2estado == LOW) {
        digitalWrite(RELAY2, HIGH);
        R2estado = HIGH;
        tiempoInicioR2 = ahora;
        tiempoEsperaR2 = 10UL * 60UL * 1000UL;
        enEsperaR2 = true;
      } else if (temperature >= maxR2 && R2estado == HIGH) {
        digitalWrite(RELAY2, LOW);
        R2estado = LOW;
      }
    }

    else if (paramR2 == D) {
      if (DPV < minR2 && R2estado == LOW) {
        digitalWrite(RELAY2, HIGH);
        R2estado = HIGH;
        tiempoInicioR2 = ahora;
        tiempoEsperaR2 = 10UL * 60UL * 1000UL;
        enEsperaR2 = true;
      } else if (DPV >= maxR2 && R2estado == HIGH) {
        digitalWrite(RELAY2, LOW);
        R2estado = LOW;
      }
    }

    else if (paramR2 == HT && humedadOk && temperaturaOk) {
      if (R2estado == LOW && (humedad < minR2 || temperature < minTR2)) {
        digitalWrite(RELAY2, HIGH);
        R2estado = HIGH;
        tiempoInicioR2 = ahora;
        tiempoEsperaR2 = 10UL * 60UL * 1000UL;
        enEsperaR2 = true;
      } else if (R2estado == HIGH && (humedad >= maxR2 || temperature >= maxTR2)) {
        digitalWrite(RELAY2, LOW);
        R2estado = LOW;
      }
    }
  }
}






if (modoR1 == AUTORIEGO) {


  // Comprobación de si estamos dentro del horario configurado
  bool dentroDeHorario = false;
  if (startR1 < offR1) {
    dentroDeHorario = (currentTime >= startR1 && currentTime < offR1);
  } else {
    dentroDeHorario = (currentTime >= startR1 || currentTime < offR1);
  }

  if (dentroDeHorario) {
    bool valorInvalido = false;
    float valorActual = 0;

    // Selección del parámetro
    if (paramR1 == H) {
      valorActual = humedad;
      if (isnan(valorActual) || valorActual < 0 || valorActual > 99.9) valorInvalido = true;
    } else if (paramR1 == T) {
      valorActual = temperature;
      if (isnan(valorActual) || valorActual < -10 || valorActual > 50) valorInvalido = true;
    } else if (paramR1 == D) {
      valorActual = DPV;
      // Suponemos que DPV siempre es válido
    }

    /*if (valorInvalido) {
      bot.sendMessage(chat_id, "⚠️ Alerta: Valor de parámetro inválido. Humidificador apagado por seguridad.", "");
      digitalWrite(RELAY1, LOW);
      R1estado = LOW;
      enHumidificacion = false;
      return;
    }*/

    // Si el valor está por debajo del mínimo configurado, iniciamos ciclo intermitente
    if (valorActual < minR1) {
      unsigned long currentMillis = millis();
      unsigned long intervalo = (enHumidificacion ? tiempoEncendidoR1 : tiempoApagadoR1) * 60000UL;

      if (currentMillis - previousMillisR1 >= intervalo) {
        previousMillisR1 = currentMillis;
        enHumidificacion = !enHumidificacion;

        if (enHumidificacion) {
          digitalWrite(RELAY1, HIGH);
          R1estado = HIGH;
        } else {
          digitalWrite(RELAY1, LOW);
          R1estado = LOW;
        }
      }
    } else {
      // Si ya está dentro del rango, apagar el humidificador si estaba encendido
      if (R1estado == HIGH) {
        digitalWrite(RELAY1, LOW);
        R1estado = LOW;
        enHumidificacion = false;
      }
    }

  } else {
    // Fuera del horario configurado: asegurarse de apagar
    if (R1estado == HIGH) {
      digitalWrite(RELAY1, LOW);
      R1estado = LOW;
    }
    enHumidificacion = false;
  }
}





  //MODO AUTO R2   IR    (DOWN)

  /*if (modoR2ir == AUTO) {
    //Serial.print("Rele 2 (Down) Automatico");

    if (paramR2ir == H) {
      if (humedad > maxR2ir && R2irestado == HIGH) {
        irsend.sendRaw(rawData, 72, 38);
        R2irestado = LOW;
        delay(200);
      }
      if (humedad < minR2ir && R2irestado == LOW) {
        irsend.sendRaw(rawData, 72, 38);
        R2irestado = HIGH;
        delay(200);
      }
    }
    if (paramR2ir == T) {
      if (temperature > maxR2ir && R2irestado == HIGH) {
        R2irestado = LOW;
        irsend.sendRaw(rawData, 72, 38);
        delay(200);
      }
      if (temperature < minR2ir && R2irestado == LOW) {
        R2irestado = HIGH;
        irsend.sendRaw(rawData, 72, 38);
        delay(200);
      }
    }

    if (paramR2ir == D) {
      if (DPV > maxR2ir && R2irestado == HIGH) {
        R2irestado = LOW;
        irsend.sendRaw(rawData, 72, 38);
        delay(200);
      }
      if (DPV < minR2ir && R2irestado == LOW) {
        R2irestado = HIGH;
        irsend.sendRaw(rawData, 72, 38);
        delay(200);
      }
    }

  }*/


// MODO RIEGO R3
unsigned long previousMillisRiego = 0;  // Variable para manejar el tiempo
bool enRiego = false;                   // Flag para saber si está en riego

if (modoR3 == AUTO) {
  for (int c = 0; c < 7; c++) {
    if (diasRiego[c] == 1) { // Verificar si el día actual está habilitado para riego
      if (c == day) { // Comprobar si hoy es el día de riego
        if (startR3 < offR3) { 
          // Caso normal: encendido antes que apagado
          if (currentTime >= startR3 && currentTime < offR3) {
            riegoIntermitente();  // Llamada a la función de riego
          } else {
            // Apagar el relé fuera del horario
            digitalWrite(RELAY3, HIGH);
            R3estado = HIGH;
            enRiego = false; // Reiniciar flag
          }
        } else { 
          // Caso cruzando medianoche: encendido después que apagado
          if (currentTime >= startR3 || currentTime < offR3) {
            riegoIntermitente();  // Llamada a la función de riego
          } else {
            // Apagar el relé fuera del horario
            digitalWrite(RELAY3, HIGH);
            R3estado = HIGH;
            enRiego = false; // Reiniciar flag
          }
        }
      }
    }
  }
}
 


  // MODO AUTO R4 (Luz)
if (modoR4 == AUTO) {
  if (startR4 < offR4) {
    // Caso normal: encendido antes que apagado
    if (currentTime >= startR4 && currentTime < offR4) {
      if (R4estado == HIGH) {
        digitalWrite(RELAY4, LOW);
        R4estado = LOW;
      }
    } else {
      if (R4estado == LOW) {
        digitalWrite(RELAY4, HIGH);
        R4estado = HIGH;
      }
    }
  } else {
    // Caso cruzando medianoche: encendido después que apagado
    if (currentTime >= startR4 || currentTime < offR4) {
      if (R4estado == HIGH) {
        digitalWrite(RELAY4, LOW);
        R4estado = LOW;
      }
    } else {
      if (R4estado == LOW) {
        digitalWrite(RELAY4, HIGH);
        R4estado = HIGH;
      }
    }
  }
}

if (modoR4 == SUPERCICLO1313) {
  // currentTime debe ser minutos 0..1439 (igual que en AUTO)
  // startR4 y offR4 también están en minutos 0..1439

  // --- Caso A: encendido antes que apagado (no cruza medianoche) ---
  if (startR4 < offR4) {
    // Dentro de la ventana [startR4, offR4) => Luz encendida
    if (currentTime >= startR4 && currentTime < offR4) {
      // Asegura estado ON (mismo criterio que tu AUTO)
      if (R4estado == HIGH) {
        digitalWrite(RELAY4, LOW);
        R4estado = LOW;

        // Al encender, fijamos el próximo apagado = start + 13 h
        offR4 = (startR4 + SUPERCYCLE_13H) % 1440;
        horaOffR4 = offR4 / 60;
        minOffR4  = offR4 % 60;
        Guardado_General();
      }
    } else {
      // Fuera de la ventana => Luz apagada
      if (R4estado == LOW) {
        digitalWrite(RELAY4, HIGH);
        R4estado = HIGH;

        // Al apagar, fijamos el próximo encendido = off + 13 h
        startR4 = (offR4 + SUPERCYCLE_13H) % 1440;
        horaOnR4 = startR4 / 60;
        minOnR4  = startR4 % 60;
        Guardado_General();
      }
    }

  // --- Caso B: cruza medianoche (encendido después que apagado) ---
  } else { // startR4 >= offR4
    // Encendida si estamos en [startR4, 1440) U [0, offR4)
    if (currentTime >= startR4 || currentTime < offR4) {
      if (R4estado == HIGH) {
        digitalWrite(RELAY4, LOW);
        R4estado = LOW;

        // Al encender, próximo apagado = start + 13 h
        offR4 = (startR4 + SUPERCYCLE_13H) % 1440;
        horaOffR4 = offR4 / 60;
        minOffR4  = offR4 % 60;
        Guardado_General();
      }
    } else {
      // Apagada en el resto
      if (R4estado == LOW) {
        digitalWrite(RELAY4, HIGH);
        R4estado = HIGH;

        // Al apagar, próximo encendido = off + 13 h
        startR4 = (offR4 + SUPERCYCLE_13H) % 1440;
        horaOnR4 = startR4 / 60;
        minOnR4  = startR4 % 60;
        Guardado_General();
      }
    }
  }
}

// SUPERCICLO



// ---------------- SUPERCICLO (simple y AUTO-like) ----------------
// ---------------- SUPERCICLO (simple, AUTO-like, usando hour/minute/nowMin) ----------------
// ---------------- SUPERCICLO (simple, AUTO-like, usando hour/minute/nowMin) ----------------
if (modoR4 == SUPERCICLO) {
  // Cambiá este flag si tu relé es activo en HIGH
  const bool ACTIVE_LOW = true;

  // Hora local (vos ya hiciste hour -= 3 arriba)
  const int currentTime = (hour * 60 + minute) % 1440;   // 0..1439

  // Duraciones configuradas por el usuario (en minutos)
  int Lm = (int)horasLuz;        if (Lm <= 0) Lm = 1;
  int Om = (int)horasOscuridad;  if (Om <  0) Om = 0;

  // Estado físico actual del relé
  int pinLevel = digitalRead(RELAY4);
  bool isOnPin = ACTIVE_LOW ? (pinLevel == LOW) : (pinLevel == HIGH);
  R4estado = pinLevel;

  auto setRelay = [&](bool turnOn){
    int lvl = ACTIVE_LOW ? (turnOn ? LOW : HIGH) : (turnOn ? HIGH : LOW);
    digitalWrite(RELAY4, lvl);
    R4estado = lvl;
    isOnPin   = turnOn;
  };

  // Helpers con tus 4 variables canónicas (persistidas)
  auto getOnAbs  = [&](){ return (horaOnR4  * 60 + minOnR4 ) % 1440; };
  auto getOffAbs = [&](){ return (horaOffR4 * 60 + minOffR4) % 1440; };

  // Igual que AUTO: ¿now está dentro de [on, off)?
  auto inWindow = [&](int nowM, int startM, int endM){
    if (startM < endM)  return (nowM >= startM && nowM < endM);
    else                return (nowM >= startM || nowM < endM); // cruza medianoche
  };

  // --- Canonizá: off = on + Lm (en RAM; la persistencia se hace en los bordes)
  int onAbs  = getOnAbs();
  int offAbs = (onAbs + Lm) % 1440;

  // --- Programación de próximos bordes (minutos del día)
  static int nextOnAbs  = -1;
  static int nextOffAbs = -1;

  // --- Tick por minuto usando tu nowMin (minutos desde epoch)
  static unsigned long lastNowMinTick = 0;
  static int lastMinuteOfDay = -1;
  int prevMinuteOfDay = lastMinuteOfDay;
  bool minuteTick = (nowMin != lastNowMinTick);
  if (minuteTick) {
    lastNowMinTick   = nowMin;
    lastMinuteOfDay  = currentTime;
  }

  // Detección robusta de objetivo, con cruce de medianoche
  auto hit = [&](int prev, int now, int target)->bool{
    if (prev < 0)    return (now == target);
    if (prev == now) return (now == target);
    if (prev < now)  return (target > prev && target <= now);
    return (target > prev || target <= now); // cruce 23:59 -> 00:00
  };

  // --- Seed inicial de próximos, alineando hardware al estado que corresponde ahora
  if (nextOnAbs < 0 && nextOffAbs < 0) {
    bool shouldOn = inWindow(currentTime, onAbs, offAbs);
    if (shouldOn) {
      if (!isOnPin) setRelay(true);
      nextOffAbs = offAbs;  // próximo borde: APAGAR
    } else {
      if (isOnPin) setRelay(false);
      nextOnAbs = onAbs;    // próximo borde: ENCENDER
    }
  }

  // --- BORDES (se ejecutan 1 vez por minuto real) ---
  if (minuteTick) {
    // 1) Si estamos ON, ¿toca APAGAR?
    if (isOnPin && nextOffAbs >= 0 && hit(prevMinuteOfDay, currentTime, nextOffAbs)) {
      setRelay(false);

      // Al APAGAR: calcular SIEMPRE la PRÓXIMA ventana y PERSISTIR SIEMPRE
      int newOnAbs  = (nextOffAbs + Om) % 1440;      // próximo encendido
      int newOffAbs = (newOnAbs + Lm)  % 1440;       // su apagado
      horaOnR4  = newOnAbs  / 60;  minOnR4  = newOnAbs  % 60;
      horaOffR4 = newOffAbs / 60;  minOffR4 = newOffAbs % 60;
      Guardado_General();                             // persistir SIEMPRE en APAGADO

      // Reprogramar bordes
      nextOnAbs  = newOnAbs;
      nextOffAbs = -1;

      // Refrescar canónicos para la UI/“próximos”
      onAbs  = newOnAbs;
      offAbs = newOffAbs;
    }

    // 2) Si estamos OFF, ¿toca ENCENDER?
    if (!isOnPin && nextOnAbs >= 0 && hit(prevMinuteOfDay, currentTime, nextOnAbs)) {
      setRelay(true);

      // Al ENCENDER: calcular SIEMPRE la ventana actual y PERSISTIR SIEMPRE
      int newOnAbs  = nextOnAbs;                     // encendió ahora
      int newOffAbs = (nextOnAbs + Lm) % 1440;       // apagado de esta ventana
      horaOnR4  = newOnAbs  / 60;  minOnR4  = newOnAbs  % 60;
      horaOffR4 = newOffAbs / 60;  minOffR4 = newOffAbs % 60;
      Guardado_General();                             // persistir SIEMPRE en ENCENDIDO

      // Reprogramar bordes
      nextOffAbs = newOffAbs;
      nextOnAbs  = -1;

      // Refrescar canónicos para la UI/“próximos”
      onAbs  = newOnAbs;
      offAbs = newOffAbs;
    }
  }

  // --- (Opcional) enforcement suave por si algo externo movió el pin
  bool wantOn = inWindow(currentTime, onAbs, offAbs);
  if (wantOn != isOnPin) setRelay(wantOn);

  // --- (Opcional) exponer “próximos” para la UI
  if (wantOn) {
    nextOffR4Abs = offAbs;
    nextOnR4Abs  = (offAbs + Om) % 1440;
  } else {
    nextOnR4Abs  = onAbs;
    nextOffR4Abs = offAbs;
  }
}













 // Crear la variable 'dateTime' para usar en mostrarEnPantallaOLED
String hora = formatoHora(hour, now.minute());

mostrarEnPantallaOLED(temperature, humedad, DPV, hora);

esp_task_wdt_reset();
//ProteccionLoop(); 

  delay(2000);

}


/*double avergearray(int* arr, int number) {
  int i;
  int max, min;
  double avg;
  long amount = 0;

  if (number <= 0) {
    Serial.println("Error number for the array to averaging!\n");
    return 0;
  }

  if (number < 5) {  // less than 5, calculated directly statistics
    for (i = 0; i < number; i++) {
      amount += arr[i];
    }
    avg = amount / number;
    return avg;
  } else {
    if (arr[0] < arr[1]) {
      min = arr[0];
      max = arr[1];
    } else {
      min = arr[1];
      max = arr[0];
    }

    for (i = 2; i < number; i++) {
      if (arr[i] < min) {
        amount += min;  // arr<min
        min = arr[i];
      } else {
        if (arr[i] > max) {
          amount += max;  // arr>max
          max = arr[i];
        } else {
          amount += arr[i];  // min<=arr<=max
        }
      }
    }
    avg = (double)amount / (number - 2);
  }
  return avg;
}*/


float calcularDPV(float temperature, float humedad) {
  const float VPS_values[] = {
    6.57, 7.06, 7.58, 8.13, 8.72, 9.36, 10.02, 10.73, 11.48, 12.28,
    13.12, 14.02, 14.97, 15.98, 17.05, 18.18, 19.37, 20.54, 21.97, 23.38,
    24.86, 26.43, 28.09, 29.83, 31.67, 33.61, 35.65, 37.79, 40.05, 42.42,
    44.92, 47.54, 50.29, 53.18, 56.21, 59.40, 62.73, 66.23, 69.90, 73.74,
    77.76
  };

  if (temperature > 41) {
    Serial.println("Error: Temperatura fuera de rango válido (1-41)");
    bot.sendMessage(chat_id, "Temperatura fuera de rango válido (1-41)", "");
    temperature = 40;

    return 0.0;  // Otra opción podría ser devolver un valor de error
  }
  float temp = temperature - 2;

  float VPS = VPS_values[static_cast<int>(temp) - 1];
  float DPV = 100 - humedad;
  float DPV1 = DPV / 100;
  float DPV2 = DPV1 * VPS;

  return DPV2;
}

//Carga descarga
void Carga_General() {
  Serial.println("Inicializando carga");

  // === Helpers temporales por tamaño ===
  uint8_t  b8;
  uint16_t w16;
  int32_t  i32;
  int64_t  i64;

  // 4 bytes
  EEPROM.get(0,  minR1);
  EEPROM.get(4,  maxR1);
  EEPROM.get(8,  minR2);
  EEPROM.get(12, maxR2);

  // 1 byte
  EEPROM.get(16, b8); paramR1 = b8;
  EEPROM.get(17, b8); paramR2 = b8;

  // 1 byte
  EEPROM.get(18, b8); modoR1 = b8;
  EEPROM.get(19, b8); modoR2 = b8;

  // 1 byte
  EEPROM.get(20, b8); modoR3 = b8;

  // 2 bytes (minutos)
  EEPROM.get(21, w16); timeOnR3  = w16;
  EEPROM.get(23, w16); timeOffR3 = w16;

  // 1 byte
  EEPROM.get(25, b8);  modoR4 = b8;

  // 2 bytes
  EEPROM.get(26, w16); timeOnR4  = w16;
  EEPROM.get(28, w16); timeOffR4 = w16;

  // 1 byte x 7
  for (int u = 0; u < 7; u++) {
    int h = 30 + u;
    EEPROM.get(h, b8);
    diasRiego[u] = b8;
  }

  // Strings
  ssid     = readStringFromEEPROM(37);
  password = readStringFromEEPROM(87);

  // 1 byte (estados)
  EEPROM.get(137, b8); estadoR1 = b8;
  EEPROM.get(138, b8); estadoR2 = b8;
  EEPROM.get(139, b8); estadoR3 = b8;
  EEPROM.get(140, b8); estadoR4 = b8;

  // 4 bytes
  EEPROM.get(141, i32); horaOnR3  = i32;
  EEPROM.get(145, i32); minOnR3   = i32;
  EEPROM.get(149, i32); horaOffR3 = i32;
  EEPROM.get(153, i32); minOffR3  = i32;

  EEPROM.get(158, i32); horaOnR4  = i32;
  EEPROM.get(162, i32); minOnR4   = i32;
  EEPROM.get(166, i32); horaOffR4 = i32;
  EEPROM.get(170, i32); minOffR4  = i32;

  // chat_id (64 bits)
  chat_id = readStringFromEEPROM(215);

  // 4 bytes
  EEPROM.get(240, minR3);
  EEPROM.get(245, maxR3);

  // ====== R5 ======
  // 4 bytes
  EEPROM.get(250, minR5);
  EEPROM.get(255, maxR5);

  // 1 byte
  EEPROM.get(260, b8); modoR5 = b8;

  // 1 byte
  EEPROM.get(265, b8); paramR5 = b8;

  // 1 byte  (¡crítico para no pisar/leer de más!)
  EEPROM.get(270, b8); estadoR5 = b8;

  // 1 byte
  EEPROM.get(272, b8); direccionR5 = b8;

  // ====== R1 (sin cambios) ======
  EEPROM.get(276, i32); horaOnR1  = i32;
  EEPROM.get(280, i32); horaOffR1 = i32;
  EEPROM.get(284, i32); minOnR1   = i32;
  EEPROM.get(288, i32); minOffR1  = i32;

  EEPROM.get(292, i32); tiempoRiego   = i32;
  EEPROM.get(296, i32); tiempoNoRiego = i32;

  EEPROM.get(300, b8);  direccionR1   = b8;

  EEPROM.get(304, i32); currentPosition = i32;
  EEPROM.get(308, i32); horaAmanecer    = i32;
  EEPROM.get(312, i32); horaAtardecer   = i32;

  EEPROM.get(316, b8);  modoWiFi = b8;

  EEPROM.get(320, i32); R1name = i32;

  // ====== R5 tiempos ======
  EEPROM.get(324, i32); minOnR5  = i32;
  // (ANTES: 328) -> AHORA lee minOffR5 en 460
  EEPROM.get(460, i32); minOffR5 = i32;   // <-- NUEVO: minOffR5 @ 460

  // 4 bytes
  EEPROM.get(330, i32); minTR2 = i32;
  EEPROM.get(334, i32); maxTR2 = i32;

  // ====== R5 horas ======
  EEPROM.get(338, i32); horaOnR5  = i32;
  // (ANTES: 342) -> AHORA lee horaOffR5 en 450
  EEPROM.get(450, i32); horaOffR5 = i32;  // <-- NUEVO: horaOffR5 @ 450
  EEPROM.get(455, i32); supercycleStartEpochR4 = i32;


  // 4 bytes varios
  EEPROM.get(346, i32); horasLuz        = i32;
  EEPROM.get(350, i32); horasOscuridad  = i32;
  EEPROM.get(354, i32); tiempoGoogle    = i32;
  EEPROM.get(358, i32); tiempoTelegram  = i32;
  EEPROM.get(362, i32); cantidadRiegos  = i32;
  EEPROM.get(366, i32); unidadRiego     = i32;
  EEPROM.get(370, i32); unidadNoRiego   = i32;

  // ====== R5 name ======
  EEPROM.get(380, i32); R5name = i32;

  // ====== NUEVO: Ciclos VEGE/FLORA ======
  uint32_t _vegeStart = 0, _floraStart = 0, _dateKey = 0;
  int32_t  _vegeDays  = 0, _floraDays  = 0;
  uint8_t  _vegeAct   = 0, _floraAct   = 0;

  EEPROM.get(ADDR_VEGE_START,   _vegeStart);
  EEPROM.get(ADDR_FLORA_START,  _floraStart);
  EEPROM.get(ADDR_VEGE_DAYS,    _vegeDays);
  EEPROM.get(ADDR_FLORA_DAYS,   _floraDays);
  EEPROM.get(ADDR_LAST_DATEKEY, _dateKey);
  EEPROM.get(ADDR_VEGE_ACTIVE,  _vegeAct);
  EEPROM.get(ADDR_FLORA_ACTIVE, _floraAct);

  // Asignación con saneo básico
  vegeStartEpoch  = _vegeStart;                 // 0 = no iniciado
  floraStartEpoch = _floraStart;
  vegeDays        = (_vegeDays  < 0) ? 0 : _vegeDays;
  floraDays       = (_floraDays < 0) ? 0 : _floraDays;
  lastDateKey     = _dateKey;                   // 0 = no inicializado
  vegeActive      = (_vegeAct  != 0);
  floraActive     = (_floraAct != 0);

  // (Opcional) Normalización de coherencia:
  if (!vegeActive)  vegeDays  = (vegeDays  > 0 ? vegeDays  : 0);
  if (!floraActive) floraDays = (floraDays > 0 ? floraDays : 0);

  // ---- Normalizaciones defensivas (evita valores basura en arranque) ----
  // modos 0/1
  if (modoR1 > 9) modoR1 = 1;
  if (modoR2 > 9) modoR2 = 1;
  if (modoR3 > 9) modoR3 = 1;
  if (modoR4 > 13) modoR4 = 1;
  if (modoR5 > 9) modoR5 = 1;

  // estados 0/1
  if (estadoR1 > 1) estadoR1 = 0;
  if (estadoR2 > 1) estadoR2 = 0;
  if (estadoR3 > 1) estadoR3 = 0;
  if (estadoR4 > 1) estadoR4 = 0;
  if (estadoR5 > 1) estadoR5 = 0;

  // params/direcciones (si esperás 0..N chicos)
  if (paramR5 > 255)      paramR5 = 0;      // por si es int en tu sketch
  if (direccionR5 > 1)    direccionR5 = 0;  // si usás solo 0/1 (invertir o no)

  Serial.println("Carga completa");
}




void Guardado_General() {
  Serial.println("Guardando en memoria..");

  // 4 bytes (int/float)
  EEPROM.put(0,   minR1);
  EEPROM.put(4,   maxR1);
  EEPROM.put(8,   minR2);
  EEPROM.put(12,  maxR2);

  // 1 byte (si tus params son enumeraciones 0..N)
  EEPROM.put(16,  (uint8_t)paramR1);
  EEPROM.put(17,  (uint8_t)paramR2);

  // 1 byte (modo 0/1)
  EEPROM.put(18,  (uint8_t)modoR1);
  EEPROM.put(19,  (uint8_t)modoR2);

  // 1 byte
  EEPROM.put(20,  (uint8_t)modoR3);

  // 2 bytes (si tus timeOn/OffR3 son minutos en uint16)
  EEPROM.put(21,  (uint16_t)timeOnR3);
  EEPROM.put(23,  (uint16_t)timeOffR3);

  // 1 byte
  EEPROM.put(25,  (uint8_t)modoR4);

  // 2 bytes (uint16)
  EEPROM.put(26,  (uint16_t)timeOnR4);
  EEPROM.put(28,  (uint16_t)timeOffR4);

  // 1 byte x 7 (bool/byte de días de riego)
  for (int y = 0; y < 7; y++) {
    int p = 30 + y;
    EEPROM.put(p, (uint8_t)diasRiego[y]);
  }

  // Cadenas (ya usás helper)
  writeStringToEEPROM(37,  ssid);
  writeStringToEEPROM(87,  password);

  // 1 byte cada uno (estados ON/OFF)
  EEPROM.put(137, (uint8_t)estadoR1);
  EEPROM.put(138, (uint8_t)estadoR2);
  EEPROM.put(139, (uint8_t)estadoR3);
  EEPROM.put(140, (uint8_t)estadoR4);

  // 4 bytes c/u
  EEPROM.put(141, (int32_t)horaOnR3);
  EEPROM.put(145, (int32_t)minOnR3);
  EEPROM.put(149, (int32_t)horaOffR3);
  EEPROM.put(153, (int32_t)minOffR3);

  EEPROM.put(158, (int32_t)horaOnR4);
  EEPROM.put(162, (int32_t)minOnR4);
  EEPROM.put(166, (int32_t)horaOffR4);
  EEPROM.put(170, (int32_t)minOffR4);

  // chat_id
  writeStringToEEPROM(215, chat_id);

  // 4 bytes
  EEPROM.put(240, minR3);
  EEPROM.put(245, maxR3);

  // === R5: min/max/modo ===
  EEPROM.put(250, minR5);
  EEPROM.put(255, maxR5);
  EEPROM.put(260, (uint8_t)modoR5);

  // === R5: param/estado/direccion ===
  EEPROM.put(265, (uint8_t)paramR5);
  EEPROM.put(270, (uint8_t)estadoR5);
  EEPROM.put(272, (uint8_t)direccionR5);

  // ==== R1 (sin cambios) ====
  EEPROM.put(276, (int32_t)horaOnR1);
  EEPROM.put(280, (int32_t)horaOffR1);
  EEPROM.put(284, (int32_t)minOnR1);
  EEPROM.put(288, (int32_t)minOffR1);
  EEPROM.put(292, (int32_t)tiempoRiego);
  EEPROM.put(296, (int32_t)tiempoNoRiego);

  EEPROM.put(300, (uint8_t)direccionR1);

  EEPROM.put(304, (int32_t)currentPosition);
  EEPROM.put(308, (int32_t)horaAmanecer);
  EEPROM.put(312, (int32_t)horaAtardecer);

  EEPROM.put(316, (uint8_t)modoWiFi);

  EEPROM.put(320, (int32_t)R1name);

  // === R5: minOn/minOff (4 bytes c/u) ===
  EEPROM.put(324, (int32_t)minOnR5);
  // EEPROM.put(328, (int32_t)minOffR5);           // DEPRECATED
  EEPROM.put(460, (int32_t)minOffR5);             // <-- NUEVO lugar

  // 4 bytes c/u
  EEPROM.put(330, (int32_t)minTR2);
  EEPROM.put(334, (int32_t)maxTR2);

  // === R5: horarios (4 bytes c/u) ===
  EEPROM.put(338, (int32_t)horaOnR5);
  // EEPROM.put(342, (int32_t)horaOffR5);          // DEPRECATED
  EEPROM.put(450, (int32_t)horaOffR5);            // <-- NUEVO lugar
  EEPROM.put(455, (int32_t)supercycleStartEpochR4);

  // 4 bytes varios
  EEPROM.put(346, (int32_t)horasLuz);
  EEPROM.put(350, (int32_t)horasOscuridad);
  EEPROM.put(354, (int32_t)tiempoGoogle);
  EEPROM.put(358, (int32_t)tiempoTelegram);
  EEPROM.put(362, (int32_t)cantidadRiegos);
  EEPROM.put(366, (int32_t)unidadRiego);
  EEPROM.put(370, (int32_t)unidadNoRiego);

  // === R5: nombre (numérico) ===
  EEPROM.put(380, (int32_t)R5name);

  // ====== Ciclos VEGE/FLORA ======
  EEPROM.put(ADDR_VEGE_START,   (uint32_t)vegeStartEpoch);
  EEPROM.put(ADDR_FLORA_START,  (uint32_t)floraStartEpoch);
  EEPROM.put(ADDR_VEGE_DAYS,    (int32_t)vegeDays);
  EEPROM.put(ADDR_FLORA_DAYS,   (int32_t)floraDays);
  EEPROM.put(ADDR_LAST_DATEKEY, (uint32_t)lastDateKey);
  EEPROM.put(ADDR_VEGE_ACTIVE,  (uint8_t)(vegeActive ? 1 : 0));
  EEPROM.put(ADDR_FLORA_ACTIVE, (uint8_t)(floraActive ? 1 : 0));

  EEPROM.commit();
  Serial.println("Guardado realizado con exito.");
}




//ACA SE CONFIGURAN TODOS LOS COMANDOS DEL BOT DE TELEGRAM, HABRIA QUE PASARLO A OTRA PESTAÑA


void modificarWifi() {
  int serial = 0;
  while (Serial.available() > 0) {
    Serial.read();
  }
  Serial.println("1. Red con contraseña");
  Serial.println("2. Red sin contraseña");
  while (Serial.available() > 0) {
    Serial.read();
  }
  while (!Serial.available()) {}
  if (Serial.available() > 0) {
    serial = Serial.read();
    Serial.print("conPW: ");
    Serial.println(serial);
  }

  Serial.println("Por favor, introduce el SSID:");
  while (!Serial.available()) {}
  if (Serial.available() > 0) {
    ssid = readSerialInput();
    if (serial == '1') {
      Serial.println("Por favor, introduce la contraseña:");
      while (Serial.available() == 0) {
        delay(100);
      }
    }
    if (serial == '1') {
      password = readSerialInput();
    } else {
      password = "";
    }

    // Mostrar los valores introducidos para depuración
    Serial.print("SSID introducido: ");
    Serial.println(ssid);
    Serial.print("Contraseña introducida: ");
    Serial.println(password);
    Guardado_General();

    // if (ssid.length() > 0 && password.length() > 0) {

    connectToWiFi(ssid.c_str(), password.c_str());
    startWebServer();

    // } else {
    //    Serial.println("SSID o contraseña vacíos, por favor intenta de nuevo.");
    // }
  }
}

String readSerialInput() {
  String input = "";
  while (true) {
    if (Serial.available() > 0) {
      char c = Serial.read();
      if (c == '\n' || c == '\r') {
        if (input.length() > 0) {
          break;  // Romper el bucle si ya se ha leído algo
        }
        continue;  // Ignorar '\n' y '\r' si aún no se ha leído nada
      }
      input += c;
      // Romper si la entrada es mayor que un tamaño razonable
      if (input.length() > 50) {
        break;
      }
    }
  }
  return input;
}


void connectToWiFi(const char* ssid, const char* password) {
  Serial.println();
  Serial.print(F("Conectando a WiFi: "));
  Serial.println(ssid && ssid[0] ? ssid : "(SSID vacío)");

  // Saneamiento previo
  WiFi.persistent(false);       // no grabar credenciales en flash
  WiFi.setAutoReconnect(true);  // que el stack retente solo
  WiFi.setSleep(false);         // más estable para IoT continuo

  // Modo STA limpio
  WiFi.disconnect(true, true);  // corta y limpia
  WiFi.mode(WIFI_STA);
  delay(50);

  // Hostname (si aplica)
  #ifdef ARDUINO_ESP32_RELEASE
  WiFi.setHostname("DruidaBot");
  #endif

  // Inicio de conexión
  if (conPW == 1 && password && password[0]) {
    WiFi.begin(ssid, password);
  } else {
    WiFi.begin(ssid);
  }

  // Timings (ligeros)
  const uint32_t TIMEOUT_INITIAL_MS   = 7000UL;   // espera inicial breve
  const uint32_t TIMEOUT_RETRY_MS_BASE= 5000UL;   // 5s,10s,20s
  const int      MAX_RETRIES          = 3;

  // Espera inicial breve
  uint32_t t0 = millis();
  while ((millis() - t0) < TIMEOUT_INITIAL_MS) {
    if (WiFi.status() == WL_CONNECTED) break;
    delay(200);
    Serial.print('.');
    yield();
  }

  // Reintentos con backoff corto
  int retry = 0;
  while (WiFi.status() != WL_CONNECTED && retry < MAX_RETRIES) {
    ++retry;
    uint32_t thisTimeout = TIMEOUT_RETRY_MS_BASE << (retry - 1); // 5s,10s,20s
    Serial.printf("\n↻ Reintento WiFi #%d (timeout %lus)\n", retry, thisTimeout / 1000UL);

    WiFi.disconnect(true, true);
    delay(120);

    if (conPW == 1 && password && password[0]) WiFi.begin(ssid, password);
    else                                       WiFi.begin(ssid);

    uint32_t tr = millis();
    while ((millis() - tr) < thisTimeout) {
      if (WiFi.status() == WL_CONNECTED) break;
      delay(250);
      Serial.print('.');
      yield();
    }
  }

  // Resultado
  if (WiFi.status() == WL_CONNECTED) {
    IPAddress ip = WiFi.localIP();
    long rssi = WiFi.RSSI();
    Serial.println(F("\n✅ WiFi conectado."));
    Serial.print(F("IP: "));   Serial.println(ip);
    Serial.print(F("RSSI: ")); Serial.print(rssi); Serial.println(F(" dBm"));

    // OLED (igual que tenías)
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SH110X_WHITE);
    display.setCursor(16, 20);
    display.println(F("WiFi conectado"));
    display.setCursor(8, 34);
    display.print(F("IP: "));
    display.println(ip);
    display.display();
    delay(200);
    return;
  }

  // ❌ FALLÓ: NO abrir AP acá (lo hace checkWiFiConnection)
  Serial.println(F("\n❌ No se pudo conectar a WiFi (STA)."));
  // Si querés, podés mostrar algo en OLED aquí indicando "Sin WiFi".
  // El AP se activa sólo desde checkWiFiConnection() cuando corresponde.
}





void writeStringToEEPROM(int addrOffset, const String& strToWrite) {
  byte len = strToWrite.length();
  if (len > 32) len = 32;         // Limitar longitud a 32 caracteres
  EEPROM.write(addrOffset, len);  // Guardar longitud de la cadena
  for (int i = 0; i < len; i++) {
    EEPROM.write(addrOffset + 1 + i, strToWrite[i]);
  }
  EEPROM.write(addrOffset + 1 + len, '\0');  // Añadir terminador nulo
}

String readStringFromEEPROM(int addrOffset) {
  int newStrLen = EEPROM.read(addrOffset);
  if (newStrLen > 32) newStrLen = 32;  // Limitar longitud a 32 caracteres
  char data[newStrLen + 1];
  for (int i = 0; i < newStrLen; i++) {
    data[i] = EEPROM.read(addrOffset + 1 + i);
  }
  data[newStrLen] = '\0';  // Asegurar terminación nula
  return String(data);
}

void modificarChatID() {
  while (Serial.available() > 0) {
    Serial.read();
  }
  Serial.println("Por favor ingrese el nuevo Chat ID:");
  while (!Serial.available()) {}
  if (Serial.available() > 0) {
    chat_id = Serial.readStringUntil('\n');
    Serial.println("Chat ID Modificado: ");
    Serial.print("Nuevo Chat ID: ");
    Serial.println(chat_id);
    Guardado_General();
  }
}



void sendDataToGoogleSheets() {
  HTTPClient http;

  // Construir la URL con los parámetros que deseas enviar
  String url = "https://script.google.com/macros/s/" + String(scriptId) + "/exec?"
               + "maxTemperature=" + String(maxTemp, 2)
               + "&minTemperature=" + String(minTemp, 2)
               + "&maxHumidity=" + String(maxHum, 2)
               + "&minHumidity=" + String(minHum, 2);

  Serial.print("Enviando datos a Google Sheets ");
  Serial.println(url);

  // Realizar la solicitud HTTP GET
  http.begin(url);
  int httpResponseCode = http.GET();

  if (httpResponseCode > 0) {
    String payload = http.getString();
    Serial.print("Payload recibido");
    Serial.println(payload);
  } else {
    Serial.print("Error en la solicitud HTTP: ");
    Serial.println(httpResponseCode);
  }

  http.end();
}

// Globals requeridas (asegúrate de declararlas en tu sketch principal):
// bool apMode = false;
// unsigned long lastRetryTime = 0;

void checkWiFiConnection() {
  // --- Parámetros de estrategia (mismos valores que usabas) ---
  const uint32_t CONNECT_SPIN_MS        = 3000UL;    // espera corta al intentar conectar
  const uint32_t BACKOFF_BASE_MS        = 60000UL;   // 1 min (base)
  const uint32_t BACKOFF_MAX_MS         = 5UL * 60UL * 1000UL; // 5 min (tope)
  const uint32_t AP_RETRY_MIN_MS        = 60000UL;   // mínimo 1 min entre intentos en AP
  const uint32_t INTERNET_CHECK_PERIOD  = 15000UL;   // chequeo internet cada 15 s conectado

  // Estado interno persistente a nivel de función (sin nuevas globales)
  static uint32_t nextRetryDelayMs = BACKOFF_BASE_MS;
  static uint32_t lastInternetCheck = 0;
  static bool     webServerUp = false;

  auto ensureWebServer = [&]() {
    if (!webServerUp) {
      startWebServer();
      webServerUp = true;
    }
  };
  auto stopWebServerFlag = [&]() {
    // si tenés stopWebServer(), llamalo acá
    webServerUp = false;
  };

  // Helper: prueba de internet real por DNS
  auto internetOK = []() -> bool {
    IPAddress ip;
    bool ok = WiFi.hostByName("time.google.com", ip) == 1;  // 1 = éxito
    return ok && (ip != IPAddress((uint32_t)0));
  };

  // Si el WiFi está deshabilitado por UI/config, no hacer nada
  if (modoWiFi != 1) return;

  // ===========================
  //     CASO CONECTADO
  // ===========================
  if (WiFi.status() == WL_CONNECTED) {
    // Si veníamos en AP, cerrarlo
    if (apMode) {
      Serial.println(F("Conexión STA restablecida. Cerrando AP..."));
      WiFi.softAPdisconnect(true);
      WiFi.mode(WIFI_STA);
      delay(100);
      apMode = false;
      stopWebServerFlag(); // reiniciar limpio
    }

    // Asegurar server en STA
    ensureWebServer();

    // Chequeo de internet real cada cierto tiempo
    if (millis() - lastInternetCheck > INTERNET_CHECK_PERIOD) {
      lastInternetCheck = millis();
      if (!internetOK()) {
        Serial.println(F("⚠️  Sin salida a internet (DNS falló). WiFi.reconnect() suave..."));
        WiFi.reconnect();
      } else {
        nextRetryDelayMs = BACKOFF_BASE_MS; // reset backoff
      }
    }
    return;
  }

  // ===========================
  //  CASO DESCONECTADO (STA)
  // ===========================
  if (!apMode) {
    Serial.println(F("WiFi desconectado. Intentando conexión (STA)..."));

    // Intento de conexión principal (NO abre AP si falla)
    connectToWiFi(ssid.c_str(), password.c_str());

    // Pequeña espera para ver si quedó conectado
    uint32_t t0 = millis();
    while ((millis() - t0) < CONNECT_SPIN_MS) {
      if (WiFi.status() == WL_CONNECTED) {
        Serial.println(F("¡Conectado!"));
        ensureWebServer();
        nextRetryDelayMs = BACKOFF_BASE_MS;
        return;
      }
      delay(150);
      yield();
    }

    // Falló conexión rápida → activar AP aquí (único lugar)
    Serial.println(F("No se pudo conectar. Activando modo AP..."));
    stopWebServerFlag();
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_AP);
    startAccessPoint();
    startWebServer();
    webServerUp = true;
    apMode = true;
    lastRetryTime = millis();   // (reutiliza tu variable global existente)
    return;
  }

  // ===========================
  //     CASO AP ACTIVO
  // ===========================
  // Reintentar salir de AP con backoff + jitter
  uint32_t elapsed = millis() - lastRetryTime;
  if (elapsed >= nextRetryDelayMs && nextRetryDelayMs >= AP_RETRY_MIN_MS) {
    Serial.printf("Reintentando conexión desde AP... (backoff %lus)\n", nextRetryDelayMs / 1000UL);
    lastRetryTime = millis();

    // Cerrar AP temporalmente para intentar STA
    WiFi.softAPdisconnect(true);
    delay(150);
    WiFi.mode(WIFI_STA);

    connectToWiFi(ssid.c_str(), password.c_str());

    // Espera corta para confirmar
    uint32_t t0 = millis();
    while ((millis() - t0) < CONNECT_SPIN_MS) {
      if (WiFi.status() == WL_CONNECTED) {
        Serial.println(F("¡Conectado desde AP! Cerrando AP y continuando en STA."));
        apMode = false;
        stopWebServerFlag();
        ensureWebServer();
        nextRetryDelayMs = BACKOFF_BASE_MS;
        return;
      }
      delay(150);
      yield();
    }

    // Si aún no conecta, volver a AP y aumentar backoff con jitter (±10%)
    Serial.println(F("Sigue sin conexión. Volviendo a AP y aumentando backoff."));
    WiFi.mode(WIFI_AP);
    startAccessPoint();
    stopWebServerFlag();
    startWebServer();
    webServerUp = true;

    uint32_t doubled = nextRetryDelayMs << 1; // *2
    if (doubled > BACKOFF_MAX_MS) doubled = BACKOFF_MAX_MS;
    uint32_t jitter = (doubled / 10U);
    uint32_t rnd = millis() % (jitter + 1U);
    nextRetryDelayMs = doubled - (jitter / 2U) + rnd;
    if (nextRetryDelayMs < AP_RETRY_MIN_MS) nextRetryDelayMs = AP_RETRY_MIN_MS;
  } else {
    // Aún no toca reintentar, asegurar server en AP
    ensureWebServer();
  }
}




/*void modificarValoresArray(bool manual) {
  if (manual) {
    // Modo manual (usando web)
  } else {
    // Modo automático (captura IR)
    while (!irrecv.decode(&results)) {
      delay(100);
    }

    // Conversión a formato rawData
    rawDataLen = results.rawlen - 1;
    if (rawDataLen > 150) {
      rawDataLen = 150;
    }

    for (uint16_t i = 1; i <= rawDataLen; i++) {
      rawData[i - 1] = results.rawbuf[i] * kRawTick;
    }

    Serial.println("Señal capturada:");
    serialPrintUint64(results.value, HEX);
    Serial.println("");
    irrecv.resume();
  }
  
  mostrarArray();
  Guardado_General();
}





void mostrarArray() {
  Serial.println("Raw Data (" + String(rawDataLen) + " elementos):");
  for (uint16_t i = 0; i < rawDataLen; i++) {
    Serial.print(rawData[i]);
    if (i < rawDataLen - 1) Serial.print(", ");
    if ((i + 1) % 10 == 0) Serial.println(); // Nueva línea cada 10 elementos
  }
  Serial.println("\n---");
}

void emitIRSignal() {
  if (rawDataLen == 0) {
    Serial.println("Error: No hay datos IR almacenados para emitir");
    return;
  }

  Serial.println("Enviando señal IR capturada...");
  
  // Configurar el pin del emisor IR
  irsend.begin();
  
  // Enviar los datos RAW capturados
  irsend.sendRaw(rawData, rawDataLen, 38); // 38 kHz es la frecuencia común para controles IR
  
  Serial.println("Señal IR enviada correctamente");
  
  // Pequeña pausa para no saturar
  delay(100);
}*/


void encenderRele3PorTiempo(int tiempoSegundos) {
  digitalWrite(RELAY3, LOW); // Enciende el relé
  delay(tiempoSegundos * 1000); // Mantiene encendido por el tiempo indicado
  digitalWrite(RELAY3, HIGH); // Apaga el relé
  bot.sendMessage(chat_id, "Rele apagado después de " + String(tiempoSegundos) + " segundos", "");
  modoR3 = MANUAL;
  estadoR3 = 0;
  R3estado = HIGH;
  Guardado_General();
}


void startAccessPoint() {
    WiFi.mode(WIFI_AP);
    WiFi.softAP(ssid_AP, password_AP);
    Serial.println("Red WiFi creada con éxito");

    IPAddress IP = WiFi.softAPIP();
    Serial.print("IP del Access Point: ");
    Serial.println(IP);

    startWebServer();  // Reutilizás la misma función
}


void startWebServer() {
    server.on("/", handleRoot);
    server.on("/config", handleConfig);
    server.on("/saveConfig", handleSaveConfig);
    server.on("/control", handleControl);

    // R1
    server.on("/controlR1", handleControlR1);
    server.on("/controlR1On", handleControlR1On);
    server.on("/controlR1Off", handleControlR1Off);
    server.on("/controlR1Auto", handleControlR1Auto);

    // R2
    server.on("/controlR2", handleControlR2);
    server.on("/controlR2On", handleControlR2On);
    server.on("/controlR2Off", handleControlR2Off);
    server.on("/controlR2Auto", handleControlR2Auto);

    // R3
    server.on("/controlR3", handleControlR3);
    server.on("/controlR3On", handleControlR3On);
    server.on("/controlR3Off", handleControlR3Off);
    server.on("/controlR3Auto", handleControlR3Auto);
    server.on("/controlR3OnFor", handleControlR3OnFor);

    // R4
    server.on("/controlR4", handleControlR4);
    server.on("/controlR4On", handleControlR4On);
    server.on("/controlR4Off", handleControlR4Off);
    server.on("/controlR4Auto", handleControlR4Auto);
    server.on("/controlR4Superciclo", handleControlR4Superciclo);
    server.on("/controlR4Nube", HTTP_POST, handleControlR4Nube);
    server.on("/controlR4Mediodia", HTTP_POST, handleControlR4Mediodia);
    server.on("/startVege",  HTTP_POST, handleStartVege);
    server.on("/startFlora", HTTP_POST, handleStartFlora);
    server.on("/resetVege",  HTTP_POST, handleResetVege);
    server.on("/resetFlora", HTTP_POST, handleResetFlora);
    server.on("/startFloraSuper", HTTP_POST, handleStartFloraSuper);


    // R5 (clonado de R1)
    server.on("/controlR5", handleControlR5);
    server.on("/controlR5On", handleControlR5On);
    server.on("/controlR5Off", handleControlR5Off);
    server.on("/controlR5Auto", handleControlR5Auto);

    // Configs
    server.on("/configR1", handleConfigR1);
    server.on("/configR2", handleConfigR2);
    server.on("/configR3", handleConfigR3);
    server.on("/configR4", handleConfigR4);
    server.on("/configR5", handleConfigR5);          // <-- nuevo

    server.on("/configWiFi", handleConfigWiFi);

    // Saves
    server.on("/saveConfigR1", saveConfigR1);
    server.on("/saveConfigR2", saveConfigR2);
    server.on("/saveConfigR3", saveConfigR3);
    server.on("/saveConfigR4", saveConfigR4);
    server.on("/saveConfigR5", saveConfigR5);        // <-- nuevo
    server.on("/saveConfigWiFi", saveConfigWiFi);

    server.on("/connectWiFi", connectWiFi);
    server.on("/disconnectWiFi", HTTP_POST, []() {
      // --- Lógica original ---
      modoWiFi = 0;
      Guardado_General();
      WiFi.disconnect(true);   // corta y borra configuración actual de conexión
      Serial.println("WiFi desconectado. modoWiFi = 0");

      // --- UI: misma estética que connectWiFi, pero “Desconectando WiFi…” ---
      String mensaje     = "Desconectando WiFi…";
      String redireccion = "/";

      String html =
      "<!DOCTYPE html><html lang='es'><head>"
      "<meta charset='UTF-8'/>"
      "<meta name='viewport' content='width=device-width, initial-scale=1'/>"
      "<title>Desconectando WiFi</title>"
      "<meta http-equiv='refresh' content='3; url=" + redireccion + "'>"
      "<link href='https://fonts.googleapis.com/css2?family=Press+Start+2P&display=swap' rel='stylesheet'>"
      "<style>"
      /* ===== Tokens responsive ===== */
      ":root{--gap:clamp(12px,2.5vw,20px);--pad:clamp(16px,3vw,24px);--radius:16px;"
      "--fs-h1:clamp(18px,5vw,36px);--fs-body:clamp(14px,3.5vw,18px);--fs-small:clamp(11px,2.8vw,14px);"
      "--c-bg:#0f172a;--c-card:#1e40af;--c-ink:#e0e0e0;--c-cyan:#00f0ff;--c-border:#00ffcc}"

      "*,*::before,*::after{box-sizing:border-box;-webkit-tap-highlight-color:transparent}"
      "html,body{height:100%}"

      /* ===== Layout ===== */
      "body{margin:0;background:var(--c-bg);color:var(--c-ink);font-family:'Press Start 2P',monospace;"
      "display:flex;flex-direction:column;min-height:100dvh}"
      "main{flex:1;display:flex;flex-direction:column;align-items:center;justify-content:center;gap:var(--gap);padding:var(--pad)}"

      /* Logo + glitch sutil */
      ".logo{font-size:var(--fs-h1);margin:0;color:var(--c-cyan);"
      "text-shadow:0 0 6px var(--c-cyan),0 0 14px var(--c-cyan);animation:glow 3s ease-in-out infinite alternate}"
      ".glitch{position:relative;display:inline-block}"
      ".glitch::before,.glitch::after{content:attr(data-text);position:absolute;left:0;top:0;inset:0;clip:rect(0,0,0,0)}"
      ".glitch::before{color:#ff00ff;text-shadow:1px 0 #ff00ff;animation:gt 2s infinite linear alternate-reverse}"
      ".glitch::after{color:#00ffff;text-shadow:-1px 0 #00ffff;animation:gb 2s infinite linear alternate-reverse}"
      "@keyframes glow{from{text-shadow:0 0 4px var(--c-cyan),0 0 10px var(--c-cyan)}to{text-shadow:0 0 10px var(--c-cyan),0 0 22px var(--c-cyan)}}"
      "@keyframes gt{0%{clip:rect(0,9999px,0,0)}10%{clip:rect(0,9999px,16px,0);transform:translate(-1px,-1px)}20%{clip:rect(0,9999px,8px,0)}100%{clip:rect(0,0,0,0)}}"
      "@keyframes gb{0%{clip:rect(0,0,0,0)}10%{clip:rect(10px,9999px,24px,0);transform:translate(1px,1px)}20%{clip:rect(4px,9999px,14px,0)}100%{clip:rect(0,0,0,0)}}"

      /* Caja de mensaje */
      ".box{background:var(--c-card);border:2px solid var(--c-border);border-radius:var(--radius);padding:var(--pad);"
      "box-shadow:0 0 15px rgba(0,255,255,0.25);max-width:min(92vw,640px);width:100%;text-align:center;animation:fadeIn .9s ease-out}"
      ".box h1{font-size:var(--fs-body);margin:0;color:var(--c-ink)}"

      /* Progreso continuo con sheen + cambio de color al final */
      ".progress{height:12px;border:1px solid var(--c-cyan);border-radius:10px;overflow:hidden;"
      "background:rgba(0,240,255,0.08);margin-top:var(--gap);position:relative}"
      ".bar{height:100%;width:0%;background:var(--c-cyan);position:relative;"
      "animation:fill 3s linear forwards, barColor 3s linear forwards}"
      ".bar::after{content:'';position:absolute;top:0;bottom:0;width:120px;"
      "background:linear-gradient(90deg,rgba(255,255,255,0) 0%,rgba(255,255,255,.6) 50%,rgba(255,255,255,0) 100%);"
      "filter:blur(1px);transform:translateX(-120%);animation:sheen 3s linear forwards}"
      "@keyframes fill{from{width:0%}to{width:100%}}"
      "@keyframes barColor{0%{background:var(--c-cyan)}80%{background:var(--c-cyan)}100%{background:#ffffff}}"
      "@keyframes sheen{from{transform:translateX(-120%)}to{transform:translateX(100%)}}"

      ".count{font-size:var(--fs-small);opacity:.85;margin-top:8px}"

      /* Botón */
      ".actions{margin-top:var(--gap);display:flex;flex-direction:column;gap:10px}"
      "a{text-decoration:none}"
      "button{background:var(--c-cyan);color:#0b1020;border:none;border-radius:12px;padding:12px 14px;font-size:var(--fs-body);"
      "cursor:pointer;transition:transform .15s ease,filter .2s ease}"
      "button:hover{transform:translateY(-1px);filter:brightness(.92)}"

      /* Reduce motion */
      "@media(prefers-reduced-motion:reduce){"
      ".logo{animation:none}.glitch::before,.glitch::after{animation:none}"
      ".bar{animation:fill 3s linear forwards}"
      ".bar::after{animation:none;display:none}"
      "}"

      /* Animación entrada */
      "@keyframes fadeIn{from{opacity:0;transform:translateY(8px)}to{opacity:1;transform:translateY(0)}}"
      "</style></head><body>"
      "<main>"
      "<h1 class='logo glitch' data-text='DATA DRUIDA'>DATA DRUIDA</h1>"
      "<div class='box'>"
      "<h1>" + mensaje + "</h1>"
      "<div class='progress' aria-hidden='true'><div class='bar'></div></div>"
      "<div class='count' aria-live='polite'>Redirigiendo en <span id='count'>3</span>…</div>"
      "<div class='actions'><a href='" + redireccion + "'><button type='button'>IR AHORA</button></a></div>"
      "</div>"
      "</main>";

      html += "<script>"
              "let t=3;const c=document.getElementById('count');"
              "function tick(){t--; if(c) c.textContent=t; if(t<=0){location.replace('" + redireccion + "');} else {setTimeout(tick,1000);} }"
              "setTimeout(tick,1000);"
              "</script>";

      html += "</body></html>";

      server.send(200, "text/html", html);

      // Pequeña espera para que el cliente reciba la respuesta y se vea la animación,
      // y luego reinicio del ESP32.
      delay(2000);
      ESP.restart();
    });

    // ===== Endpoints masivos con confirmación =====
    server.on("/allOn", HTTP_POST, []() {
      setAllRelays(true);
      Serial.println("[ALL] ENCENDER TODO");
      handleConfirmation("Encendiendo todo...", "/control");
    });

    server.on("/allOff", HTTP_POST, []() {
      setAllRelays(false);
      Serial.println("[ALL] APAGAR TODO");
      handleConfirmation("Apagando todo...", "/control");
    });

    server.on("/setFloraDay", HTTP_POST, handleSetFloraDay);
    server.on("/setVegeDay", HTTP_POST, handleSetVegeDay);



    //server.on("/configIR", handleConfigIR);
    //server.on("/captureIR", handleCaptureIR);
    //server.on("/saveIRConfig", handleSaveIRConfig);
    //server.on("/controlR2ir", handleControlR2ir);
    //server.on("/controlR2irOn", HTTP_POST, handleControlR2irOn);
    //server.on("/controlR2irOff", HTTP_POST, handleControlR2irOff);
    //server.on("/controlR2irAuto", HTTP_POST, handleControlR2irAuto);
    //server.on("/checkIRCapture", handleCheckIRCapture);
    //server.on("/emitIR", handleEmitIR);

    server.begin();
    Serial.println("Servidor web iniciado");
}



//CONFIG WIFI IPLOCAL

void handleRoot() {
  // ===== Lectura de sensores =====
  sensors_event_t humidity, temp;
  aht.getEvent(&humidity, &temp);

  float temperature = temp.temperature;
  float humedad = humidity.relative_humidity;

  DateTime now = rtc.now();
  int horaBot = now.hour();

  // Ajuste horario local (si tu RTC está en UTC)
  horaBot -= 3;
  if (horaBot < 0) horaBot = 24 + horaBot;

  // Fecha/Hora formateada
  String dateTime = String(now.day()) + "/" + String(now.month()) + "/" + String(now.year()) + " ";
  dateTime += (horaBot < 10 ? "0" : "") + String(horaBot) + ":";
  dateTime += (now.minute() < 10 ? "0" : "") + String(now.minute()) + ":";
  dateTime += (now.second() < 10 ? "0" : "") + String(now.second());

  // ===== Detección de sensor offline (robusta) =====
  bool zeroishBoth = (fabsf(temperature) < 0.05f && fabsf(humedad) < 0.05f);
  bool looksOfflinePattern =
      (fabsf(temperature) < 0.1f) && (humedad >= 1.0f && humedad <= 3.5f) && (fabsf(DPV) < 0.1f);
  bool impossibleValues =
      !isfinite(temperature) || !isfinite(humedad) || !isfinite(DPV) ||
      (temperature < -40.0f || temperature > 85.0f) ||
      (humedad < 0.1f || humedad > 100.0f);

  bool sensorOffline = zeroishBoth || looksOfflinePattern || impossibleValues;

  // ===== Status principal =====
  String statusMessage  = "<div class='line' id='temp'>Temp: ";
  statusMessage        += sensorOffline ? String("--") : (String(temperature, 1) + " C");
  statusMessage        += "</div>";

  statusMessage        += "<div class='line' id='hum'>Hum: ";
  statusMessage        += sensorOffline ? String("--") : (String(humedad, 1) + " %");
  statusMessage        += "</div>";

  statusMessage        += "<div class='line' id='dpv'>VPD: ";
  statusMessage        += sensorOffline ? String("--") : (String(DPV, 1) + " hPa");
  statusMessage        += "</div>";

  statusMessage        += "<div class='line' id='fechaHora' data-hora='" 
                       + String(horaBot) + ":" + String(now.minute()) + ":" + String(now.second()) 
                       + "'>" + dateTime + "</div>";

  // ===== Contadores de ciclo =====
  int vDaysVege = (vegeStartEpoch > 0 && vegeActive)
    ? realDaysSince(vegeStartEpoch, -3 * 3600)
    : 0;

  statusMessage += "<div class='line' id='diaVege'>DIA VEGE: " 
                 + String(vDaysVege > 0 ? String(vDaysVege) : String("--")) 
                 + "</div>";

  int vDaysFloraReal = (floraStartEpoch > 0 && floraActive)
    ? realDaysSince(floraStartEpoch, -3 * 3600)
    : 0;

  statusMessage += "<div class='line' id='diaFlora'>DIA FLORA: " 
                 + String(vDaysFloraReal > 0 ? String(vDaysFloraReal) : String("--")) 
                 + "</div>";

  // SUPERCICLOS (calcular, pero mostrar SOLO si corresponde)
  int vDaysSuperDisplay = 0;
  if (modoR4 == SUPERCICLO1313 && vDaysFloraReal > 0) {
    vDaysSuperDisplay = vDaysFloraReal - ((vDaysFloraReal - 1) / 12);
  } else {
    vDaysSuperDisplay = (floraStartEpoch > 0 && floraActive)
      ? virtualDaysSince(floraStartEpoch, horasLuz, horasOscuridad, -3 * 3600)
      : 0;
  }
  if (modoR4 == SUPERCICLO || modoR4 == SUPERCICLO1313) {
    statusMessage += "<div class='line' id='diaSuper'>SUPERCICLOS: "
                   + String(vDaysSuperDisplay > 0 ? String(vDaysSuperDisplay) : String("--"))
                   + "</div>";
  }

  // ===== Texto de MODO =====
  auto modeText = [](int modo, int relayIndex)->String {
    switch (relayIndex) {
      case 2: // R2: 1=Manual, 2=Auto, 9=Auto Inteligente
        if (modo == 1) return "MANUAL";
        if (modo == 2) return "AUTO";
        if (modo == 9) return "AUTO IA";
        return "—";
      case 4: // R4: 1=Manual, 2=Auto, 4=Superciclo, 13=Superciclo 13/13
        if (modo == 1) return "MANUAL";
        if (modo == 2) return "AUTO";
        if (modo == 4) return "SUP CIC";
        if (modo == SUPERCICLO1313) return "13/13";
        return "—";
      default: // R1, R5, R3: 1=Manual, 2=Auto, 6=Timer (según caso)
        if (modo == 1) return "MANUAL";
        if (modo == 2) return "AUTO";
        if (modo == 6) return "TIMER";
        return "—";
    }
  };

  String mR5 = modeText(modoR5, 5);
  String mR1 = modeText(modoR1, 1);
  String mR2 = modeText(modoR2, 2);
  String mR3 = modeText(modoR3, 3);
  String mR4 = modeText(modoR4, 4);

  // ===== Estado REAL de cada relé por pin =====
  // R1..R4: activos en BAJO  -> LOW = ON
  // R5    : activo en ALTO   -> HIGH = ON
  bool r5On = (digitalRead(RELAY5) == HIGH); // <- único invertido
  bool r1On = (digitalRead(RELAY1) == LOW);
  bool r2On = (digitalRead(RELAY2) == LOW);
  bool r3On = (digitalRead(RELAY3) == LOW);
  bool r4On = (digitalRead(RELAY4) == LOW);

  // ===== Lista de relés (Nombre + Modo + Punto) =====
  statusMessage += "<div class='relays-list' aria-label='Estado de relés'>";
  statusMessage +=   "<div class='relay-row'><span class='relay-name'>" + getRelayName(R5name) + "</span><span class='relay-right'><span class='mode-badge'>" + mR5 + "</span><span class='dot " + String(r5On ? "on" : "off") + "'></span></span></div>";
  statusMessage +=   "<div class='relay-row'><span class='relay-name'>" + getRelayName(R1name) + "</span><span class='relay-right'><span class='mode-badge'>" + mR1 + "</span><span class='dot " + String(r1On ? "on" : "off") + "'></span></span></div>";
  statusMessage +=   "<div class='relay-row'><span class='relay-name'>" + getRelayName(R2name) + "</span><span class='relay-right'><span class='mode-badge'>" + mR2 + "</span><span class='dot " + String(r2On ? "on" : "off") + "'></span></span></div>";
  statusMessage +=   "<div class='relay-row'><span class='relay-name'>" + getRelayName(R3name) + "</span><span class='relay-right'><span class='mode-badge'>" + mR3 + "</span><span class='dot " + String(r3On ? "on" : "off") + "'></span></span></div>";
  statusMessage +=   "<div class='relay-row'><span class='relay-name'>" + getRelayName(R4name) + "</span><span class='relay-right'><span class='mode-badge'>" + mR4 + "</span><span class='dot " + String(r4On ? "on" : "off") + "'></span></span></div>";
  statusMessage += "</div>";

  // ===== HTML =====
  String html =
    "<!DOCTYPE html><html lang='es'><head><meta charset='UTF-8'/>"
    "<meta name='viewport' content='width=device-width, initial-scale=1'/>"
    "<title>Estado General</title>"
    "<link href='https://fonts.googleapis.com/css2?family=Press+Start+2P&display=swap' rel='stylesheet'>"
    "<style>"
      ":root{--radius:16px;--pad:16px;--gap:12px;--pad-lg:24px;"
      "--fs-body:clamp(16px,3.5vw,18px);--fs-small:clamp(12px,2.6vw,14px);--fs-h1:clamp(22px,6vw,56px)}"
      "*,*::before,*::after{box-sizing:border-box;-webkit-tap-highlight-color:transparent}"
      "html,body{height:100%}"
      /* Layout base: el footer no flota ni tapa contenido */
      "body{margin:0;padding:0;font-family:'Press Start 2P',monospace;background:linear-gradient(to bottom,#0a0f1e,#111927);"
        "color:#e0e0e0;min-height:100dvh;display:flex;flex-direction:column;align-items:stretch;line-height:1.25}"
      ".main{flex:1;display:flex;flex-direction:column;align-items:center;gap:16px;padding:10px;"
        "padding-bottom:calc(env(safe-area-inset-bottom) + 12px)}"
      ".cloud{position:absolute;top:-40px;left:50%;transform:translateX(-50%);width:300px;height:150px;"
        "background:radial-gradient(ellipse at center,rgba(0,240,255,.2),transparent 70%);border-radius:50%;filter:blur(40px);"
        "animation:pulse 6s ease-in-out infinite;z-index:-1}"
      "@keyframes pulse{0%,100%{transform:translateX(-50%) scale(1);opacity:.3}50%{transform:translateX(-50%) scale(1.1);opacity:.5}}"
      /* logo clickeable */
      ".logo-container{margin:0 0 4px 0;position:relative;text-align:center;animation:fadeIn 1.2s ease-out;"
        "display:inline-block;text-decoration:none;color:inherit;cursor:pointer}"
      ".logo-container:hover .logo-text{filter:brightness(1.1)}"
      ".logo-text{font-size:var(--fs-h1);font-weight:bold;line-height:1.15;color:#00f0ff;"
        "text-shadow:0 0 12px #00f0ff,0 0 28px #00f0ff;animation:glow 3s infinite alternate}"
      "@keyframes glow{from{text-shadow:0 0 8px #00f0ff,0 0 18px #00f0ff}to{text-shadow:0 0 18px #00f0ff,0 0 36px #00f0ff}}"
      ".glitch{position:relative;display:inline-block}"
      ".glitch::before,.glitch::after{content:attr(data-text);position:absolute;left:0;top:0;width:100%;overflow:hidden;color:#00f0ff;background:transparent;clip:rect(0,0,0,0)}"
      ".glitch::before{text-shadow:2px 0 red;animation:glitchTop 2s infinite linear alternate-reverse}"
      ".glitch::after{text-shadow:-2px 0 blue;animation:glitchBottom 2s infinite linear alternate-reverse}"
      "@keyframes glitchTop{0%{clip:rect(0,9999px,0,0)}5%{clip:rect(0,9999px,20px,0);transform:translate(-2px,-2px)}10%{clip:rect(0,9999px,10px,0);transform:translate(2px,0)}15%{clip:rect(0,9999px,5px,0);transform:translate(-1px,1px)}20%,100%{clip:rect(0,0,0,0);transform:none}}"
      "@keyframes glitchBottom{0%{clip:rect(0,0,0,0)}5%{clip:rect(20px,9999px,40px,0);transform:translate(1px,1px)}10%{clip:rect(10px,9999px,30px,0);transform:translate(-1px,-1px)}15%{clip:rect(5px,9999px,25px,0);transform:translate(1px,-1px)}20%,100%{clip:rect(0,0,0,0);transform:none}}"
      ".container{margin-top:0;background-color:#1e293b;border:1px solid #00f0ff;border-radius:var(--radius);padding:var(--pad-lg);"
        "width:min(92vw,560px);max-width:560px;box-shadow:0 0 20px rgba(0,240,255,.2);animation:fadeInUp .9s ease-out;"
        "text-align:center;position:relative;display:flex;flex-direction:column;gap:var(--gap)}"
      ".info-box{margin-bottom:2px;text-align:center;font-size:var(--fs-body);color:#00f0ff}"
      ".info-box .line{margin:10px 0}"
      ".relays-list{display:flex;flex-direction:column;gap:6px;margin-top:8px}"
      ".relay-row{display:flex;align-items:center;justify-content:space-between;padding:6px 10px;border:1px solid rgba(0,240,255,.18);"
        "border-radius:8px;background:rgba(0,240,255,.06);font-size:var(--fs-small);color:#cfe}"
      ".relay-name{text-align:left}"
      ".relay-right{display:flex;align-items:center;gap:8px}"
      ".mode-badge{font-size:var(--fs-small);letter-spacing:.4px;border:1px solid rgba(0,240,255,.25);padding:4px 8px;border-radius:999px;"
        "background:rgba(0,240,255,.08);color:#dff;white-space:nowrap}"
      ".dot{width:10px;height:10px;border-radius:50%;background:currentColor;box-shadow:0 0 6px currentColor}"
      ".on{color:#4ade80}.off{color:#ef4444}"
      ".button-group-vertical{display:flex;flex-wrap:wrap;gap:10px;justify-content:center;margin-top:18px}"
      "button{background-color:#00f0ff;color:#0f172a;border:none;padding:12px 14px;font-size:var(--fs-body);font-weight:bold;"
        "cursor:pointer;border-radius:10px;transition:background-color .2s ease,transform .15s ease;font-family:'Press Start 2P',monospace;"
        "min-height:48px;flex:1 1 100%;max-width:260px}"
      "button:hover{background-color:#00c0dd;transform:translateY(-1px)}"
      "@media(min-width:600px){.button-group-vertical{flex-wrap:nowrap}button{flex:1 1 auto;max-width:none}}"
      /* Footer estático (no tapa botones) */
      "footer{position:static;text-align:center;font-size:var(--fs-small);color:#8aa;opacity:.9;margin:0;"
        "padding:8px 12px;padding-bottom:calc(env(safe-area-inset-bottom) + 8px)}"
      "footer p{margin:0}"
      "@keyframes fadeIn{from{opacity:0;transform:translateY(-20px)}to{opacity:1;transform:translateY(0)}}"
      "@keyframes fadeInUp{from{opacity:0;transform:translateY(40px)}to{opacity:1;transform:translateY(0)}}"
    "</style></head><body>";

  // MAIN (crece y centra el contenido sin afectar al footer)
  html += "<main class='main'>";
  html += "<div class='cloud'></div>";

  // Logo: enlace a handleRoot (ruta '/')
  html += "<a href='/' class='logo-container' aria-label='Ir a inicio'>";
  html += "  <h1 class='logo-text glitch' data-text='DRUIDA BOT'>DRUIDA<br>BOT</h1>";
  html += "</a>";

  html += "<div class='container'>";
  html += "  <div class='info-box'>";
  html +=        statusMessage;
  html += "  </div>";

  html += "  <div class='button-group-vertical'>";
  html += "    <a href='/control'><button>CONTROL</button></a>";
  html += "    <a href='/config'><button>CONFIG</button></a>";
  html += "  </div>";
  html += "</div>";
  html += "</main>";

  // Footer estático (no fijo)
  html += "<footer><p>druidadata@gmail.com<br>DataDruida</p></footer>";

  // Reloj en vivo
  html += "<script>";
  html += "function startClock(){";
  html += " const div=document.getElementById('fechaHora');";
  html += " if(!div) return;";
  html += " let [h,m,s]=div.getAttribute('data-hora').split(':').map(Number);";
  html += " setInterval(()=>{";
  html += "  s++; if(s>=60){s=0;m++;} if(m>=60){m=0;h++;} if(h>=24)h=0;";
  html += "  const pad=n=>n.toString().padStart(2,'0');";
  html += "  const now=new Date();";
  html += "  const fecha=pad(now.getDate())+'/'+pad(now.getMonth()+1)+'/'+now.getFullYear();";
  html += "  div.innerHTML=fecha+' '+pad(h)+':'+pad(m)+':'+pad(s);";
  html += " },1000);";
  html += "}";
  html += "window.onload=startClock;";
  html += "</script>";

  html += "</body></html>";

  server.send(200, "text/html", html);
}












void handleControl() {
  String html =
    "<!DOCTYPE html><html lang='es'><head>"
    "<meta charset='UTF-8'/>"
    "<meta name='viewport' content='width=device-width, initial-scale=1'/>"
    "<title>Control</title>"
    "<link href='https://fonts.googleapis.com/css2?family=Press+Start+2P&display=swap' rel='stylesheet'>"
    "<style>"
      /* ===== Tokens responsive ===== */
      ":root{--radius:16px;--pad:16px;--gap:12px;--pad-lg:24px;"
      "--fs-body:clamp(16px,3.5vw,18px);--fs-small:clamp(12px,2.6vw,14px);--fs-h1:clamp(22px,6vw,56px)}"
      "*,*::before,*::after{box-sizing:border-box;-webkit-tap-highlight-color:transparent}"
      "html,body{height:100%}"

      /* ===== Layout base: footer NO fijo ===== */
      "body{margin:0;padding:0;font-family:'Press Start 2P',monospace;"
      "background:linear-gradient(to bottom,#0a0f1e,#111927);color:#e0e0e0;"
      "min-height:100dvh;display:flex;flex-direction:column;align-items:stretch;line-height:1.25}"

      ".main{flex:1;display:flex;flex-direction:column;align-items:center;gap:16px;padding:10px;"
      "padding-bottom:calc(env(safe-area-inset-bottom) + 12px);position:relative}"

      /* ===== Glow de fondo sutil ===== */
      ".cloud{position:absolute;top:-40px;left:50%;transform:translateX(-50%);width:300px;height:150px;"
      "background:radial-gradient(ellipse at center,rgba(0,240,255,.2),transparent 70%);border-radius:50%;"
      "filter:blur(40px);animation:pulse 6s ease-in-out infinite;z-index:-1}"
      "@keyframes pulse{0%,100%{transform:translateX(-50%) scale(1);opacity:.3}"
      "50%{transform:translateX(-50%) scale(1.1);opacity:.5}}"

      /* ===== Logo / título (clickeable) ===== */
      ".logo-container{margin:0 0 4px 0;position:relative;text-align:center;animation:fadeIn 1.2s ease-out;"
      "display:inline-block;text-decoration:none;color:inherit;cursor:pointer}"
      ".logo-container:hover .logo-text{filter:brightness(1.1)}"
      ".logo-text{font-size:var(--fs-h1);font-weight:bold;line-height:1.15;color:#00f0ff;"
      "text-shadow:0 0 12px #00f0ff,0 0 28px #00f0ff;animation:glow 3s infinite alternate}"
      "@keyframes glow{from{text-shadow:0 0 8px #00f0ff,0 0 18px #00f0ff}"
      "to{text-shadow:0 0 18px #00f0ff,0 0 36px #00f0ff}}"

      /* ===== Glitch ===== */
      ".glitch{position:relative;display:inline-block}"
      ".glitch::before,.glitch::after{content:attr(data-text);position:absolute;left:0;top:0;width:100%;overflow:hidden;"
      "color:#00f0ff;background:transparent;clip:rect(0,0,0,0)}"
      ".glitch::before{text-shadow:2px 0 red;animation:glitchTop 2s infinite linear alternate-reverse}"
      ".glitch::after{text-shadow:-2px 0 blue;animation:glitchBottom 2s infinite linear alternate-reverse}"
      "@keyframes glitchTop{0%{clip:rect(0,9999px,0,0)}5%{clip:rect(0,9999px,20px,0);transform:translate(-2px,-2px)}"
      "10%{clip:rect(0,9999px,10px,0);transform:translate(2px,0)}15%{clip:rect(0,9999px,5px,0);transform:translate(-1px,1px)}"
      "20%,100%{clip:rect(0,0,0,0);transform:none}}"
      "@keyframes glitchBottom{0%{clip:rect(0,0,0,0)}5%{clip:rect(20px,9999px,40px,0);transform:translate(1px,1px)}"
      "10%{clip:rect(10px,9999px,30px,0);transform:translate(-1px,-1px)}15%{clip:rect(5px,9999px,25px,0);transform:translate(1px,-1px)}"
      "20%,100%{clip:rect(0,0,0,0);transform:none}}"

      /* ===== Card contenedor ===== */
      ".container{margin-top:0;background-color:#1e293b;border:1px solid #00f0ff;border-radius:var(--radius);"
      "padding:var(--pad-lg);width:min(92vw,560px);max-width:560px;box-shadow:0 0 20px rgba(0,240,255,.2);"
      "animation:fadeInUp .9s ease-out;text-align:center;position:relative;display:flex;flex-direction:column;gap:var(--gap)}"
      ".card-title{font-size:var(--fs-body);margin:4px 0 6px;color:#00f0ff}"

      /* ===== Botones apilados ===== */
      ".button-group-vertical{display:flex;flex-direction:column;gap:10px;justify-content:center;margin-top:10px}"
      "a{text-decoration:none}"
      "button{background-color:#00f0ff;color:#0f172a;border:none;padding:12px 14px;font-size:var(--fs-body);font-weight:bold;"
      "cursor:pointer;border-radius:10px;transition:background-color .2s ease,transform .15s ease;"
      "font-family:'Press Start 2P',monospace;min-height:48px;width:100%}"
      "button:hover{background-color:#00c0dd;transform:translateY(-1px)}"
      "@media(min-width:600px){.button-group-vertical{align-items:stretch}button{width:100%}}"

      /* ===== Botonera masiva (debajo de Iluminación) ===== */
      ".bulk-actions{display:flex;flex-direction:column;gap:10px;margin-top:6px}"
      ".btn-on,.btn-off{background:#00f0ff;color:#0b1220;border:1px solid #00f0ff}"
      ".btn-on:hover,.btn-off:hover{background:#00c0dd}"
      ".hint{font-size:var(--fs-small);opacity:.8;margin-top:-2px}"


      /* ===== Footer estático ===== */
      "footer{position:static;text-align:center;font-size:var(--fs-small);color:#8aa;opacity:.9;margin:0;"
      "padding:8px 12px;padding-bottom:calc(env(safe-area-inset-bottom) + 8px)}"
      "footer p{margin:0}"

      /* ===== Animaciones ===== */
      "@keyframes fadeIn{from{opacity:0;transform:translateY(-20px)}to{opacity:1;transform:translateY(0)}}"
      "@keyframes fadeInUp{from{opacity:0;transform:translateY(40px)}to{opacity:1;transform:translateY(0)}}"
    "</style></head><body>";

  /* MAIN */
  html += "<main class='main'>";
  html += "<div class='cloud'></div>";

  /* Logo: enlace a handleRoot ('/') */
  html += "<a href='/' class='logo-container' aria-label='Ir a inicio'>"
          "<h1 class='logo-text glitch' data-text='DRUIDA BOT'>DRUIDA<br>BOT</h1>"
          "</a>";

  /* Card de control */
  html += "<div class='container'>";
  html += "  <div class='card-title'>Control</div>";
  html += "  <div class='button-group-vertical'>";

  /* Botones individuales */
  html += "    <a href='/controlR5'><button>" + getRelayName(R5name) + "</button></a>";
  html += "    <a href='/controlR1'><button>" + getRelayName(R1name) + "</button></a>";
  html += "    <a href='/controlR2'><button>" + getRelayName(R2name) + "</button></a>";
  html += "    <a href='/controlR3'><button>" + getRelayName(R3name) + "</button></a>";
  html += "    <a href='/controlR4'><button>" + getRelayName(R4name) + "</button></a>";

  /* —— NUEVO: Acciones masivas debajo de Iluminación —— */
  html += "    <div class='bulk-actions'>";
  html += "      <form action='/allOn' method='POST'><button class='btn-on' type='submit'>ENCENDER TODO</button></form>";
  html += "      <form action='/allOff' method='POST'><button class='btn-off' type='submit'>APAGAR TODO</button></form>";
  //html += "      <div class='hint'>R1–R4 ON=LOW · R5 ON=HIGH</div>";
  html += "    </div>";

  /* Volver */
  html += "    <a href='/'><button>VOLVER</button></a>";

  html += "  </div>"; // .button-group-vertical
  html += "</div>";   // .container

  html += "</main>";

  /* Footer */
  html += "<footer><p>druidadata@gmail.com<br>DataDruida</p></footer>";

  html += "</body></html>";

  server.send(200, "text/html", html);
}












void handleConfirmation(const String& mensaje, const String& redireccion) {
  String html =
  "<!DOCTYPE html><html lang='es'><head>"
  "<meta charset='UTF-8'/>"
  "<meta name='viewport' content='width=device-width, initial-scale=1'/>"
  "<title>Confirmación</title>"
  "<meta http-equiv='refresh' content='3; url=" + redireccion + "'>"
  "<link href='https://fonts.googleapis.com/css2?family=Press+Start+2P&display=swap' rel='stylesheet'>"
  "<style>"
  /* ===== Tokens responsive ===== */
  ":root{--gap:clamp(12px,2.5vw,20px);--pad:clamp(16px,3vw,24px);--radius:16px;"
  "--fs-h1:clamp(18px,5vw,36px);--fs-body:clamp(14px,3.5vw,18px);--fs-small:clamp(11px,2.8vw,14px);"
  "--c-bg:#0f172a;--c-card:#1e40af;--c-ink:#e0e0e0;--c-cyan:#00f0ff;--c-border:#00ffcc}"

  "*,*::before,*::after{box-sizing:border-box;-webkit-tap-highlight-color:transparent}"
  "html,body{height:100%}"

  /* ===== Layout ===== */
  "body{margin:0;background:var(--c-bg);color:var(--c-ink);font-family:'Press Start 2P',monospace;"
  "display:flex;flex-direction:column;min-height:100dvh}"
  "main{flex:1;display:flex;flex-direction:column;align-items:center;justify-content:center;gap:var(--gap);padding:var(--pad)}"

  /* Logo + glitch sutil */
  ".logo{font-size:var(--fs-h1);margin:0;color:var(--c-cyan);"
  "text-shadow:0 0 6px var(--c-cyan),0 0 14px var(--c-cyan);animation:glow 3s ease-in-out infinite alternate}"
  ".glitch{position:relative;display:inline-block}"
  ".glitch::before,.glitch::after{content:attr(data-text);position:absolute;left:0;top:0;inset:0;clip:rect(0,0,0,0)}"
  ".glitch::before{color:#ff00ff;text-shadow:1px 0 #ff00ff;animation:gt 2s infinite linear alternate-reverse}"
  ".glitch::after{color:#00ffff;text-shadow:-1px 0 #00ffff;animation:gb 2s infinite linear alternate-reverse}"
  "@keyframes glow{from{text-shadow:0 0 4px var(--c-cyan),0 0 10px var(--c-cyan)}to{text-shadow:0 0 10px var(--c-cyan),0 0 22px var(--c-cyan)}}"
  "@keyframes gt{0%{clip:rect(0,9999px,0,0)}10%{clip:rect(0,9999px,16px,0);transform:translate(-1px,-1px)}20%{clip:rect(0,9999px,8px,0)}100%{clip:rect(0,0,0,0)}}"
  "@keyframes gb{0%{clip:rect(0,0,0,0)}10%{clip:rect(10px,9999px,24px,0);transform:translate(1px,1px)}20%{clip:rect(4px,9999px,14px,0)}100%{clip:rect(0,0,0,0)}}"

  /* Caja de mensaje */
  ".box{background:var(--c-card);border:2px solid var(--c-border);border-radius:var(--radius);padding:var(--pad);"
  "box-shadow:0 0 15px rgba(0,255,255,0.25);max-width:min(92vw,640px);width:100%;text-align:center;animation:fadeIn .9s ease-out}"
  ".box h1{font-size:var(--fs-body);margin:0;color:var(--c-ink)}"

  /* Progreso continuo con sheen + cambio de color al final */
  ".progress{height:12px;border:1px solid var(--c-cyan);border-radius:10px;overflow:hidden;"
  "background:rgba(0,240,255,0.08);margin-top:var(--gap);position:relative}"
  ".bar{height:100%;width:0%;background:var(--c-cyan);position:relative;"
  "animation:fill 3s linear forwards, barColor 3s linear forwards}"
  ".bar::after{content:'';position:absolute;top:0;bottom:0;width:120px;"
  "background:linear-gradient(90deg,rgba(255,255,255,0) 0%,rgba(255,255,255,.6) 50%,rgba(255,255,255,0) 100%);"
  "filter:blur(1px);transform:translateX(-120%);"
  "animation:sheen 3s linear forwards}"
  "@keyframes fill{from{width:0%}to{width:100%}}"
  /* Mantiene cian la mayor parte y transiciona a blanco en el tramo final */
  "@keyframes barColor{0%{background:var(--c-cyan)}80%{background:var(--c-cyan)}100%{background:#ffffff}}"
  "@keyframes sheen{from{transform:translateX(-120%)}to{transform:translateX(100%)}}"

  ".count{font-size:var(--fs-small);opacity:.85;margin-top:8px}"

  /* Botón */
  ".actions{margin-top:var(--gap);display:flex;flex-direction:column;gap:10px}"
  "a{text-decoration:none}"
  "button{background:var(--c-cyan);color:#0b1020;border:none;border-radius:12px;padding:12px 14px;font-size:var(--fs-body);"
  "cursor:pointer;transition:transform .15s ease,filter .2s ease}"
  "button:hover{transform:translateY(-1px);filter:brightness(.92)}"

  /* Reduce motion */
  "@media(prefers-reduced-motion:reduce){"
  ".logo{animation:none}.glitch::before,.glitch::after{animation:none}"
  ".bar{animation:fill 3s linear forwards}"
  ".bar::after{animation:none;display:none}"
  "}"

  /* Animación entrada */
  "@keyframes fadeIn{from{opacity:0;transform:translateY(8px)}to{opacity:1;transform:translateY(0)}}"
  "</style></head><body>"
  "<main>"
  "<h1 class='logo glitch' data-text='DATA DRUIDA'>DATA DRUIDA</h1>"
  "<div class='box'>"
  "<h1>" + mensaje + "</h1>"
  "<div class='progress' aria-hidden='true'><div class='bar'></div></div>"
  "<div class='count' aria-live='polite'>Redirigiendo en <span id='count'>3</span>…</div>"
  "<div class='actions'><a href='" + redireccion + "'><button type='button'>IR AHORA</button></a></div>"
  "</div>"
  "</main>";

  // JS: solo contador y redirección; la barra se anima por CSS (continuo + sheen + color final).
  html += "<script>"
          "let t=3;const c=document.getElementById('count');"
          "function tick(){t--; if(c) c.textContent=t; if(t<=0){location.replace('" + redireccion + "');} else {setTimeout(tick,1000);}}"
          "setTimeout(tick,1000);"
          "</script>";

  html += "</body></html>";

  server.send(200, "text/html", html);
}







/*void handleControlR2ir() {
    String html = "<html><head><style>";
    html += "body { background-color: #00264d; color: white; font-family: Arial, sans-serif; margin: 0; height: 100vh; display: flex; justify-content: center; align-items: center; flex-direction: column; text-align: center; }";
    html += "h1 { color: #00bfff; margin-bottom: 30px; }";
    html += "button, input[type='submit'] { background-color: #1c75bc; color: white; border: 2px solid #00ff00; padding: 20px 60px; font-size: 36px; border-radius: 20px; cursor: pointer; margin: 20px; display: inline-block; }";
    html += "button:hover, input[type='submit']:hover { background-color: #004080; border-color: #00cc00; }";
    html += "</style></head><body>";
    html += "<h1>Control de " + getRelayName(R2irname) + "</h1>";

    // Botón de Encender
    html += "<form action=\"/controlR2irOn\" method=\"POST\">";
    html += "<input type=\"submit\" value=\"Encender\">";
    html += "</form>";

    // Botón de Apagar
    html += "<form action=\"/controlR2irOff\" method=\"POST\">";
    html += "<input type=\"submit\" value=\"Apagar\">";
    html += "</form>";

    // Modo automático
    html += "<form action=\"/controlR2irAuto\" method=\"POST\">";
    html += "<input type=\"submit\" value=\"Automatico\">";
    html += "</form>";

    // Botón para volver al panel de control
    html += "<a href=\"/control\"><button>Volver</button></a>";
    html += "</body></html>";

    server.send(200, "text/html", html);
}

void handleControlR2irOn() {
    estadoR2ir = 1; // Cambiar el estado a encendido
    modoR2ir = 1;   // Modo manual
    Guardado_General();
    handleConfirmation(getRelayName(R2irname) + " encendida", "/controlR2ir");
}

void handleControlR2irOff() {
    estadoR2ir = 0; // Cambiar el estado a apagado
    modoR2ir = 1;   // Modo manual
    Guardado_General();
    handleConfirmation(getRelayName(R2irname) + " apagada", "/controlR2ir");
}

void handleControlR2irAuto() {
    modoR2ir = 2;   // Modo automático
    Guardado_General();
    handleConfirmation(getRelayName(R2irname) + " en modo automatico", "/controlR2ir");
}*/



void handleControlR1() {
  String html = "<!DOCTYPE html><html lang='es'><head>"
                "<meta charset='UTF-8'/>"
                "<meta name='viewport' content='width=device-width, initial-scale=1'/>"
                "<title>Control R1</title>"
                "<link href='https://fonts.googleapis.com/css2?family=Press+Start+2P&display=swap' rel='stylesheet'>"
                "<style>"
                /* ===== Tokens responsive ===== */
                ":root{--radius:16px;--pad:16px;--gap:12px;--pad-lg:24px;"
                "--fs-body:clamp(16px,3.5vw,18px);--fs-small:clamp(12px,2.6vw,14px);--fs-h1:clamp(22px,6vw,56px)}"
                "*,*::before,*::after{box-sizing:border-box;-webkit-tap-highlight-color:transparent}"
                "html,body{height:100%}"
                /* BODY */
                "body{margin:0;padding:10px;font-family:'Press Start 2P',monospace;"
                "background:linear-gradient(to bottom,#0a0f1e,#111927);color:#e0e0e0;"
                "min-height:100dvh;display:flex;flex-direction:column;align-items:center;justify-content:center;"
                "line-height:1.25;gap:16px;position:relative}"
                /* Glow de fondo sutil */
                ".cloud{position:absolute;top:-40px;left:50%;transform:translateX(-50%);width:300px;height:150px;"
                "background:radial-gradient(ellipse at center,rgba(0,240,255,.2),transparent 70%);border-radius:50%;"
                "filter:blur(40px);animation:pulse 6s ease-in-out infinite;z-index:-1}"
                "@keyframes pulse{0%,100%{transform:translateX(-50%) scale(1);opacity:.3}"
                "50%{transform:translateX(-50%) scale(1.1);opacity:.5}}"
                /* Logo / título */
                ".logo-container{margin:0 0 4px 0;position:relative;text-align:center;animation:fadeIn 1.2s ease-out}"
                ".logo-text{font-size:var(--fs-h1);font-weight:bold;line-height:1.15;color:#00f0ff;"
                "text-shadow:0 0 12px #00f0ff,0 0 28px #00f0ff;animation:glow 3s infinite alternate}"
                "@keyframes glow{from{text-shadow:0 0 8px #00f0ff,0 0 18px #00f0ff}"
                "to{text-shadow:0 0 18px #00f0ff,0 0 36px #00f0ff}}"
                /* Glitch */
                ".glitch{position:relative;display:inline-block}"
                ".glitch::before,.glitch::after{content:attr(data-text);position:absolute;left:0;top:0;width:100%;overflow:hidden;"
                "color:#00f0ff;background:transparent;clip:rect(0,0,0,0)}"
                ".glitch::before{text-shadow:2px 0 red;animation:glitchTop 2s infinite linear alternate-reverse}"
                ".glitch::after{text-shadow:-2px 0 blue;animation:glitchBottom 2s infinite linear alternate-reverse}"
                "@keyframes glitchTop{0%{clip:rect(0,9999px,0,0)}5%{clip:rect(0,9999px,20px,0);transform:translate(-2px,-2px)}"
                "10%{clip:rect(0,9999px,10px,0);transform:translate(2px,0)}15%{clip:rect(0,9999px,5px,0);transform:translate(-1px,1px)}"
                "20%,100%{clip:rect(0,0,0,0);transform:none}}"
                "@keyframes glitchBottom{0%{clip:rect(0,0,0,0)}5%{clip:rect(20px,9999px,40px,0);transform:translate(1px,1px)}"
                "10%{clip:rect(10px,9999px,30px,0);transform:translate(-1px,-1px)}15%{clip:rect(5px,9999px,25px,0);transform:translate(1px,-1px)}"
                "20%,100%{clip:rect(0,0,0,0);transform:none}}"
                /* Card contenedor */
                ".container{margin-top:0;background-color:#1e293b;border:1px solid #00f0ff;border-radius:var(--radius);"
                "padding:var(--pad-lg);width:min(92vw,560px);max-width:560px;box-shadow:0 0 20px rgba(0,240,255,.2);"
                "animation:fadeInUp .9s ease-out;text-align:center;position:relative;display:flex;flex-direction:column;gap:var(--gap)}"
                ".card-title{font-size:var(--fs-body);margin:4px 0 6px;color:#00f0ff}"
                /* Botones apilados SIEMPRE */
                ".button-group-vertical{display:flex;flex-direction:column;gap:10px;justify-content:center;margin-top:10px}"
                "a{text-decoration:none}"
                "form{margin:0;width:100%}"
                "input[type='submit'],button{background-color:#00f0ff;color:#0f172a;border:none;padding:12px 14px;"
                "font-size:var(--fs-body);font-weight:bold;cursor:pointer;border-radius:10px;transition:background-color .2s ease,transform .15s ease;"
                "font-family:'Press Start 2P',monospace;min-height:48px;width:100%}"
                "input[type='submit']:hover,button:hover{background-color:#00c0dd;transform:translateY(-1px)}"
                /* Desktop: seguimos apilados y estirados al ancho del card */
                "@media(min-width:600px){.button-group-vertical{align-items:stretch}input[type='submit'],button{width:100%}}"
                /* Footer fijo (igual a handleControl) */
                "footer{position:fixed;left:0;right:0;bottom:8px;text-align:center;font-size:var(--fs-small);color:#8aa;opacity:.9;"
                "margin:0;padding:0 12px;padding-bottom:calc(env(safe-area-inset-bottom) + 4px);z-index:10}"
                "footer p{margin:0}"
                /* Animaciones */
                "@keyframes fadeIn{from{opacity:0;transform:translateY(-20px)}to{opacity:1;transform:translateY(0)}}"
                "@keyframes fadeInUp{from{opacity:0;transform:translateY(40px)}to{opacity:1;transform:translateY(0)}}"
                "</style></head><body>";

  html += "<div class='cloud'></div>";
  html += "<div class='logo-container'><h1 class='logo-text glitch' data-text='DRUIDA BOT'>DRUIDA<br>BOT</h1></div>";

  html += "<div class='container'>";
  html += "  <div class='card-title'>Control de " + getRelayName(R1name) + "</div>";
  html += "  <div class='button-group-vertical'>";
  html += "    <form action='/controlR1On'  method='POST'><input type='submit' value='ENCENDER'></form>";
  html += "    <form action='/controlR1Off' method='POST'><input type='submit' value='APAGAR'></form>";
  html += "    <form action='/controlR1Auto' method='POST'><input type='submit' value='AUTOMÁTICO'></form>";
  html += "    <a href='/control'><button>VOLVER</button></a>";
  html += "  </div>";
  html += "</div>";

  html += "<footer class='footer-fixed'><p>druidadata@gmail.com<br>DataDruida</p></footer>";
  html += "</body></html>";

  server.send(200, "text/html", html);
}








  void handleControlR1On() {
      estadoR1 = 1; // Cambiar el estado a encendido
      modoR1 = 1; // Modo manual
      Guardado_General();
      handleConfirmation(getRelayName(R1name) + " encendida", "/controlR1"); // Mostrar confirmación y redirigir
  }

void handleControlR1Off() {
    estadoR1 = 0; // Cambiar el estado a apagado
    modoR1 = 1; // Modo manual
    Guardado_General();
    handleConfirmation(getRelayName(R1name) + " apagada", "/controlR1"); // Mostrar confirmación y redirigir
}

void handleControlR1Auto() {
    modoR1 = 2; // Modo automático
    Guardado_General();
    handleConfirmation(getRelayName(R1name) + " en modo automatico", "/controlR1"); // Mostrar confirmación y redirigir
}

void handleControlR5() {
  String html = "<!DOCTYPE html><html lang='es'><head>"
                "<meta charset='UTF-8'/>"
                "<meta name='viewport' content='width=device-width, initial-scale=1'/>"
                "<title>Control R5</title>"
                "<link href='https://fonts.googleapis.com/css2?family=Press+Start+2P&display=swap' rel='stylesheet'>"
                "<style>"
                /* ===== Tokens responsive ===== */
                ":root{--radius:16px;--pad:16px;--gap:12px;--pad-lg:24px;"
                "--fs-body:clamp(16px,3.5vw,18px);--fs-small:clamp(12px,2.6vw,14px);--fs-h1:clamp(22px,6vw,56px)}"
                "*,*::before,*::after{box-sizing:border-box;-webkit-tap-highlight-color:transparent}"
                "html,body{height:100%}"
                /* BODY */
                "body{margin:0;padding:10px;font-family:'Press Start 2P',monospace;"
                "background:linear-gradient(to bottom,#0a0f1e,#111927);color:#e0e0e0;"
                "min-height:100dvh;display:flex;flex-direction:column;align-items:center;justify-content:center;"
                "line-height:1.25;gap:16px;position:relative}"
                /* Glow de fondo sutil */
                ".cloud{position:absolute;top:-40px;left:50%;transform:translateX(-50%);width:300px;height:150px;"
                "background:radial-gradient(ellipse at center,rgba(0,240,255,.2),transparent 70%);border-radius:50%;"
                "filter:blur(40px);animation:pulse 6s ease-in-out infinite;z-index:-1}"
                "@keyframes pulse{0%,100%{transform:translateX(-50%) scale(1);opacity:.3}"
                "50%{transform:translateX(-50%) scale(1.1);opacity:.5}}"
                /* Logo / título */
                ".logo-container{margin:0 0 4px 0;position:relative;text-align:center;animation:fadeIn 1.2s ease-out}"
                ".logo-text{font-size:var(--fs-h1);font-weight:bold;line-height:1.15;color:#00f0ff;"
                "text-shadow:0 0 12px #00f0ff,0 0 28px #00f0ff;animation:glow 3s infinite alternate}"
                "@keyframes glow{from{text-shadow:0 0 8px #00f0ff,0 0 18px #00f0ff}"
                "to{text-shadow:0 0 18px #00f0ff,0 0 36px #00f0ff}}"
                /* Glitch */
                ".glitch{position:relative;display:inline-block}"
                ".glitch::before,.glitch::after{content:attr(data-text);position:absolute;left:0;top:0;width:100%;overflow:hidden;"
                "color:#00f0ff;background:transparent;clip:rect(0,0,0,0)}"
                ".glitch::before{text-shadow:2px 0 red;animation:glitchTop 2s infinite linear alternate-reverse}"
                ".glitch::after{text-shadow:-2px 0 blue;animation:glitchBottom 2s infinite linear alternate-reverse}"
                "@keyframes glitchTop{0%{clip:rect(0,9999px,0,0)}5%{clip:rect(0,9999px,20px,0);transform:translate(-2px,-2px)}"
                "10%{clip:rect(0,9999px,10px,0);transform:translate(2px,0)}15%{clip:rect(0,9999px,5px,0);transform:translate(-1px,1px)}"
                "20%,100%{clip:rect(0,0,0,0);transform:none}}"
                "@keyframes glitchBottom{0%{clip:rect(0,0,0,0)}5%{clip:rect(20px,9999px,40px,0);transform:translate(1px,1px)}"
                "10%{clip:rect(10px,9999px,30px,0);transform:translate(-1px,-1px)}15%{clip:rect(5px,9999px,25px,0);transform:translate(1px,-1px)}"
                "20%,100%{clip:rect(0,0,0,0);transform:none}}"
                /* Card contenedor */
                ".container{margin-top:0;background-color:#1e293b;border:1px solid #00f0ff;border-radius:var(--radius);"
                "padding:var(--pad-lg);width:min(92vw,560px);max-width:560px;box-shadow:0 0 20px rgba(0,240,255,.2);"
                "animation:fadeInUp .9s ease-out;text-align:center;position:relative;display:flex;flex-direction:column;gap:var(--gap)}"
                ".card-title{font-size:var(--fs-body);margin:4px 0 6px;color:#00f0ff}"
                /* Botones apilados SIEMPRE */
                ".button-group-vertical{display:flex;flex-direction:column;gap:10px;justify-content:center;margin-top:10px}"
                "a{text-decoration:none}"
                "form{margin:0;width:100%}"
                "input[type='submit'],button{background-color:#00f0ff;color:#0f172a;border:none;padding:12px 14px;"
                "font-size:var(--fs-body);font-weight:bold;cursor:pointer;border-radius:10px;transition:background-color .2s ease,transform .15s ease;"
                "font-family:'Press Start 2P',monospace;min-height:48px;width:100%}"
                "input[type='submit']:hover,button:hover{background-color:#00c0dd;transform:translateY(-1px)}"
                /* Desktop: seguimos apilados y estirados al ancho del card */
                "@media(min-width:600px){.button-group-vertical{align-items:stretch}input[type='submit'],button{width:100%}}"
                /* Footer fijo (igual a handleControl) */
                "footer{position:fixed;left:0;right:0;bottom:8px;text-align:center;font-size:var(--fs-small);color:#8aa;opacity:.9;"
                "margin:0;padding:0 12px;padding-bottom:calc(env(safe-area-inset-bottom) + 4px);z-index:10}"
                "footer p{margin:0}"
                /* Animaciones */
                "@keyframes fadeIn{from{opacity:0;transform:translateY(-20px)}to{opacity:1;transform:translateY(0)}}"
                "@keyframes fadeInUp{from{opacity:0;transform:translateY(40px)}to{opacity:1;transform:translateY(0)}}"
                "</style></head><body>";

  html += "<div class='cloud'></div>";
  html += "<div class='logo-container'><h1 class='logo-text glitch' data-text='DRUIDA BOT'>DRUIDA<br>BOT</h1></div>";

  html += "<div class='container'>";
  html += "  <div class='card-title'>Control de " + getRelayName(R5name) + "</div>";
  html += "  <div class='button-group-vertical'>";
  html += "    <form action='/controlR5On'  method='POST'><input type='submit' value='ENCENDER'></form>";
  html += "    <form action='/controlR5Off' method='POST'><input type='submit' value='APAGAR'></form>";
  html += "    <form action='/controlR5Auto' method='POST'><input type='submit' value='AUTOMÁTICO'></form>";
  html += "    <a href='/control'><button>VOLVER</button></a>";
  html += "  </div>";
  html += "</div>";

  html += "<footer class='footer-fixed'><p>druidadata@gmail.com<br>DataDruida</p></footer>";
  html += "</body></html>";

  server.send(200, "text/html", html);
}


void handleControlR5On() {
    estadoR5 = 1;        // estado lógico ON
    modoR5   = MANUAL;   // forzamos manual
    setRelay5(true);     // impactar hardware YA (activo en HIGH)
    Guardado_General();  // persistir
    handleConfirmation(getRelayName(R5name) + " encendida", "/controlR5");
}

void handleControlR5Off() {
    estadoR5 = 0;        // estado lógico OFF
    modoR5   = MANUAL;
    setRelay5(false);    // impactar hardware YA
    Guardado_General();
    handleConfirmation(getRelayName(R5name) + " apagada", "/controlR5");
}

void handleControlR5Auto() {
    modoR5 = AUTO;       // modo automático
    Guardado_General();
    handleConfirmation(getRelayName(R5name) + " en modo automático", "/controlR5");
}





void handleControlR2() {
  String html = "<!DOCTYPE html><html lang='es'><head>"
                "<meta charset='UTF-8'/>"
                "<meta name='viewport' content='width=device-width, initial-scale=1'/>"
                "<title>Control R2</title>"
                "<link href='https://fonts.googleapis.com/css2?family=Press+Start+2P&display=swap' rel='stylesheet'>"
                "<style>"
                /* ===== Tokens responsive ===== */
                ":root{--radius:16px;--pad:16px;--gap:12px;--pad-lg:24px;"
                "--fs-body:clamp(16px,3.5vw,18px);--fs-small:clamp(12px,2.6vw,14px);--fs-h1:clamp(22px,6vw,56px)}"
                "*,*::before,*::after{box-sizing:border-box;-webkit-tap-highlight-color:transparent}"
                "html,body{height:100%}"
                /* BODY */
                "body{margin:0;padding:10px;font-family:'Press Start 2P',monospace;"
                "background:linear-gradient(to bottom,#0a0f1e,#111927);color:#e0e0e0;"
                "min-height:100dvh;display:flex;flex-direction:column;align-items:center;justify-content:center;"
                "line-height:1.25;gap:16px;position:relative}"
                /* Glow de fondo sutil */
                ".cloud{position:absolute;top:-40px;left:50%;transform:translateX(-50%);width:300px;height:150px;"
                "background:radial-gradient(ellipse at center,rgba(0,240,255,.2),transparent 70%);border-radius:50%;"
                "filter:blur(40px);animation:pulse 6s ease-in-out infinite;z-index:-1}"
                "@keyframes pulse{0%,100%{transform:translateX(-50%) scale(1);opacity:.3}"
                "50%{transform:translateX(-50%) scale(1.1);opacity:.5}}"
                /* Logo / título */
                ".logo-container{margin:0 0 4px 0;position:relative;text-align:center;animation:fadeIn 1.2s ease-out}"
                ".logo-text{font-size:var(--fs-h1);font-weight:bold;line-height:1.15;color:#00f0ff;"
                "text-shadow:0 0 12px #00f0ff,0 0 28px #00f0ff;animation:glow 3s infinite alternate}"
                "@keyframes glow{from{text-shadow:0 0 8px #00f0ff,0 0 18px #00f0ff}"
                "to{text-shadow:0 0 18px #00f0ff,0 0 36px #00f0ff}}"
                /* Glitch */
                ".glitch{position:relative;display:inline-block}"
                ".glitch::before,.glitch::after{content:attr(data-text);position:absolute;left:0;top:0;width:100%;overflow:hidden;"
                "color:#00f0ff;background:transparent;clip:rect(0,0,0,0)}"
                ".glitch::before{text-shadow:2px 0 red;animation:glitchTop 2s infinite linear alternate-reverse}"
                ".glitch::after{text-shadow:-2px 0 blue;animation:glitchBottom 2s infinite linear alternate-reverse}"
                "@keyframes glitchTop{0%{clip:rect(0,9999px,0,0)}5%{clip:rect(0,9999px,20px,0);transform:translate(-2px,-2px)}"
                "10%{clip:rect(0,9999px,10px,0);transform:translate(2px,0)}15%{clip:rect(0,9999px,5px,0);transform:translate(-1px,1px)}"
                "20%,100%{clip:rect(0,0,0,0);transform:none}}"
                "@keyframes glitchBottom{0%{clip:rect(0,0,0,0)}5%{clip:rect(20px,9999px,40px,0);transform:translate(1px,1px)}"
                "10%{clip:rect(10px,9999px,30px,0);transform:translate(-1px,-1px)}15%{clip:rect(5px,9999px,25px,0);transform:translate(1px,-1px)}"
                "20%,100%{clip:rect(0,0,0,0);transform:none}}"
                /* Card contenedor */
                ".container{margin-top:0;background-color:#1e293b;border:1px solid #00f0ff;border-radius:var(--radius);"
                "padding:var(--pad-lg);width:min(92vw,560px);max-width:560px;box-shadow:0 0 20px rgba(0,240,255,.2);"
                "animation:fadeInUp .9s ease-out;text-align:center;position:relative;display:flex;flex-direction:column;gap:var(--gap)}"
                ".card-title{font-size:var(--fs-body);margin:4px 0 6px;color:#00f0ff}"
                /* Botones apilados SIEMPRE */
                ".button-group-vertical{display:flex;flex-direction:column;gap:10px;justify-content:center;margin-top:10px}"
                "a{text-decoration:none}"
                "form{margin:0;width:100%}"
                "input[type='submit'],button{background-color:#00f0ff;color:#0f172a;border:none;padding:12px 14px;"
                "font-size:var(--fs-body);font-weight:bold;cursor:pointer;border-radius:10px;transition:background-color .2s ease,transform .15s ease;"
                "font-family:'Press Start 2P',monospace;min-height:48px;width:100%}"
                "input[type='submit']:hover,button:hover{background-color:#00c0dd;transform:translateY(-1px)}"
                /* Desktop: seguimos apilados y estirados al ancho del card */
                "@media(min-width:600px){.button-group-vertical{align-items:stretch}input[type='submit'],button{width:100%}}"
                /* Footer fijo (igual a handleControl) */
                "footer{position:fixed;left:0;right:0;bottom:8px;text-align:center;font-size:var(--fs-small);color:#8aa;opacity:.9;"
                "margin:0;padding:0 12px;padding-bottom:calc(env(safe-area-inset-bottom) + 4px);z-index:10}"
                "footer p{margin:0}"
                /* Animaciones */
                "@keyframes fadeIn{from{opacity:0;transform:translateY(-20px)}to{opacity:1;transform:translateY(0)}}"
                "@keyframes fadeInUp{from{opacity:0;transform:translateY(40px)}to{opacity:1;transform:translateY(0)}}"
                "</style></head><body>";

  html += "<div class='cloud'></div>";
  html += "<div class='logo-container'><h1 class='logo-text glitch' data-text='DRUIDA BOT'>DRUIDA<br>BOT</h1></div>";

  html += "<div class='container'>";
  html += "  <div class='card-title'>Control de " + getRelayName(R2name) + "</div>";
  html += "  <div class='button-group-vertical'>";
  html += "    <form action='/controlR2On'  method='POST'><input type='submit' value='ENCENDER'></form>";
  html += "    <form action='/controlR2Off' method='POST'><input type='submit' value='APAGAR'></form>";
  html += "    <form action='/controlR2Auto' method='POST'><input type='submit' value='AUTOMÁTICO'></form>";
  html += "    <a href='/control'><button>VOLVER</button></a>";
  html += "  </div>";
  html += "</div>";

  html += "<footer class='footer-fixed'><p>druidadata@gmail.com<br>DataDruida</p></footer>";
  html += "</body></html>";

  server.send(200, "text/html", html);
}





void handleControlR2On() {
    estadoR2 = 1; // Cambiar el estado a encendido
    modoR2 = 1; // Modo manual
    Guardado_General();
    handleConfirmation(getRelayName(R2name) + " encendida", "/controlR2"); // Mostrar confirmación y redirigir
}

void handleControlR2Off() {
    estadoR2 = 0; // Cambiar el estado a apagado
    modoR2 = 1; // Modo manual
    Guardado_General();
    handleConfirmation(getRelayName(R2name) + " apagada", "/controlR2"); // Mostrar confirmación y redirigir
}

void handleControlR2Auto() {
    modoR2 = 2; // Modo automático
    Guardado_General();
    handleConfirmation(getRelayName(R2name) + " en modo automatico", "/controlR2"); // Mostrar confirmación y redirigir
}


void handleControlR3() {
  String html = "<!DOCTYPE html><html lang='es'><head>"
                "<meta charset='UTF-8'/>"
                "<meta name='viewport' content='width=device-width, initial-scale=1'/>"
                "<title>Control R3</title>"
                "<link href='https://fonts.googleapis.com/css2?family=Press+Start+2P&display=swap' rel='stylesheet'>"
                "<style>"
                /* ===== Tokens responsive ===== */
                ":root{--radius:16px;--pad:16px;--gap:12px;--pad-lg:24px;"
                "--fs-body:clamp(16px,3.5vw,18px);--fs-small:clamp(12px,2.6vw,14px);--fs-h1:clamp(22px,6vw,56px)}"
                "*,*::before,*::after{box-sizing:border-box;-webkit-tap-highlight-color:transparent}"
                "html,body{height:100%}"
                /* BODY */
                "body{margin:0;padding:10px;font-family:'Press Start 2P',monospace;"
                "background:linear-gradient(to bottom,#0a0f1e,#111927);color:#e0e0e0;"
                "min-height:100dvh;display:flex;flex-direction:column;align-items:center;justify-content:center;"
                "line-height:1.25;gap:16px;position:relative}"
                /* Glow de fondo sutil */
                ".cloud{position:absolute;top:-40px;left:50%;transform:translateX(-50%);width:300px;height:150px;"
                "background:radial-gradient(ellipse at center,rgba(0,240,255,.2),transparent 70%);border-radius:50%;"
                "filter:blur(40px);animation:pulse 6s ease-in-out infinite;z-index:-1}"
                "@keyframes pulse{0%,100%{transform:translateX(-50%) scale(1);opacity:.3}"
                "50%{transform:translateX(-50%) scale(1.1);opacity:.5}}"
                /* Logo / título */
                ".logo-container{margin:0 0 4px 0;position:relative;text-align:center;animation:fadeIn 1.2s ease-out}"
                ".logo-text{font-size:var(--fs-h1);font-weight:bold;line-height:1.15;color:#00f0ff;"
                "text-shadow:0 0 12px #00f0ff,0 0 28px #00f0ff;animation:glow 3s infinite alternate}"
                "@keyframes glow{from{text-shadow:0 0 8px #00f0ff,0 0 18px #00f0ff}"
                "to{text-shadow:0 0 18px #00f0ff,0 0 36px #00f0ff}}"
                /* Glitch */
                ".glitch{position:relative;display:inline-block}"
                ".glitch::before,.glitch::after{content:attr(data-text);position:absolute;left:0;top:0;width:100%;overflow:hidden;"
                "color:#00f0ff;background:transparent;clip:rect(0,0,0,0)}"
                ".glitch::before{text-shadow:2px 0 red;animation:glitchTop 2s infinite linear alternate-reverse}"
                ".glitch::after{text-shadow:-2px 0 blue;animation:glitchBottom 2s infinite linear alternate-reverse}"
                "@keyframes glitchTop{0%{clip:rect(0,9999px,0,0)}5%{clip:rect(0,9999px,20px,0);transform:translate(-2px,-2px)}"
                "10%{clip:rect(0,9999px,10px,0);transform:translate(2px,0)}15%{clip:rect(0,9999px,5px,0);transform:translate(-1px,1px)}"
                "20%,100%{clip:rect(0,0,0,0);transform:none}}"
                "@keyframes glitchBottom{0%{clip:rect(0,0,0,0)}5%{clip:rect(20px,9999px,40px,0);transform:translate(1px,1px)}"
                "10%{clip:rect(10px,9999px,30px,0);transform:translate(-1px,-1px)}15%{clip:rect(5px,9999px,25px,0);transform:translate(1px,-1px)}"
                "20%,100%{clip:rect(0,0,0,0);transform:none}}"
                /* Card contenedor */
                ".container{margin-top:0;background-color:#1e293b;border:1px solid #00f0ff;border-radius:var(--radius);"
                "padding:var(--pad-lg);width:min(92vw,560px);max-width:560px;box-shadow:0 0 20px rgba(0,240,255,.2);"
                "animation:fadeInUp .9s ease-out;text-align:center;position:relative;display:flex;flex-direction:column;gap:var(--gap)}"
                ".card-title{font-size:var(--fs-body);margin:4px 0 6px;color:#00f0ff}"
                /* Grupo vertical (botones + formularios) */
                ".button-group-vertical{display:flex;flex-direction:column;gap:10px;justify-content:center;margin-top:10px}"
                "a{text-decoration:none}"
                "form{margin:0;width:100%;display:flex;flex-direction:column;gap:8px}"
                "input[type='submit'],button{background-color:#00f0ff;color:#0f172a;border:none;padding:12px 14px;"
                "font-size:var(--fs-body);font-weight:bold;cursor:pointer;border-radius:10px;transition:background-color .2s ease,transform .15s ease;"
                "font-family:'Press Start 2P',monospace;min-height:48px;width:100%}"
                "input[type='submit']:hover,button:hover{background-color:#00c0dd;transform:translateY(-1px)}"
                /* Campo numérico */
                ".inline-row{display:flex;flex-wrap:wrap;align-items:center;justify-content:center;gap:10px;font-size:var(--fs-body);color:#e0f7ff}"
                "input[type='number']{padding:10px 12px;font-size:var(--fs-body);min-width:110px;text-align:center;"
                "border:2px solid #00f0ff;border-radius:10px;background:#0f172a;color:#e0e0e0;font-family:'Press Start 2P',monospace}"
                /* Desktop: seguimos apilados y estirados al ancho del card */
                "@media(min-width:600px){.button-group-vertical{align-items:stretch}input[type='submit'],button{width:100%}}"
                /* Footer fijo (igual a handleControl) */
                "footer{position:fixed;left:0;right:0;bottom:8px;text-align:center;font-size:var(--fs-small);color:#8aa;opacity:.9;"
                "margin:0;padding:0 12px;padding-bottom:calc(env(safe-area-inset-bottom) + 4px);z-index:10}"
                "footer p{margin:0}"
                /* Animaciones */
                "@keyframes fadeIn{from{opacity:0;transform:translateY(-20px)}to{opacity:1;transform:translateY(0)}}"
                "@keyframes fadeInUp{from{opacity:0;transform:translateY(40px)}to{opacity:1;transform:translateY(0)}}"
                "</style></head><body>";

  html += "<div class='cloud'></div>";
  html += "<div class='logo-container'><h1 class='logo-text glitch' data-text='DRUIDA BOT'>DRUIDA<br>BOT</h1></div>";

  html += "<div class='container'>";
  html += "  <div class='card-title'>Control de " + getRelayName(R3name) + "</div>";
  html += "  <div class='button-group-vertical'>";
  html += "    <form action='/controlR3On'  method='POST'><input type='submit' value='ENCENDER'></form>";
  html += "    <form action='/controlR3Off' method='POST'><input type='submit' value='APAGAR'></form>";
  html += "    <form action='/controlR3Auto' method='POST'><input type='submit' value='AUTOMÁTICO'></form>";
  html += "    <form action='/controlR3OnFor' method='POST'>"
          "      <div class='inline-row'>Encender " + getRelayName(R3name) + " por "
          "      <input type='number' name='duration' min='1' step='1' required> segundos</div>"
          "      <input type='submit' value='RIEGO TEST'>"
          "    </form>";
  html += "    <a href='/control'><button>VOLVER</button></a>";
  html += "  </div>";
  html += "</div>";

  html += "<footer class='footer-fixed'><p>druidadata@gmail.com<br>DataDruida</p></footer>";
  html += "</body></html>";

  server.send(200, "text/html", html);
}





void handleControlR3On() {
    estadoR3 = 1; // Cambiar el estado a encendido
    modoR3 = 1; // Modo manual
    Guardado_General();
    handleConfirmation(getRelayName(R3name) + " encendida", "/controlR3"); // Mostrar confirmación y redirigir
}

void handleControlR3Off() {
    estadoR3 = 0; // Cambiar el estado a apagado
    modoR3 = 1; // Modo manual
    Guardado_General();
    handleConfirmation(getRelayName(R3name) + " apagada", "/controlR3"); // Mostrar confirmación y redirigir
}

void handleControlR3Auto() {
    modoR3 = 2; // Modo automático
    Guardado_General();
    handleConfirmation(getRelayName(R3name) + " en modo automatico", "/controlR3"); // Mostrar confirmación y redirigir
}

void handleControlR3OnFor() {
    if (server.hasArg("duration")) {
        int duration = server.arg("duration").toInt();
        encenderRele3PorTiempo(duration); // Llama a la función que enciende el relé por tiempo
        handleConfirmation(getRelayName(R3name) + " encendida por " + String(duration) + " segundos", "/controlR3");
    } else {
        handleConfirmation("Duración no especificada", "/controlR3"); // Mensaje de error en caso de falta de datos
    }
}


void handleControlR4() {
  // ------ Cálculos determinísticos de días ------
  int vDaysVege = (vegeStartEpoch > 0 && vegeActive)
                    ? realDaysSince(vegeStartEpoch, -3 * 3600)
                    : 0;

  int vDaysFloraReal = (floraStartEpoch > 0 && floraActive)
                         ? realDaysSince(floraStartEpoch, -3 * 3600)
                         : 0;

  int vDaysFloraSuper = (floraStartEpoch > 0 && floraActive)
                          ? virtualDaysSince(floraStartEpoch, horasLuz, horasOscuridad, -3 * 3600)
                          : 0;

  // DÍA SUPERCICLO (desde FLORA, desfase +1 cada 12 días)
  int superFromFlora = -1;
  if (modoR4 == SUPERCICLO1313 && vDaysFloraReal > 0) {
    superFromFlora = vDaysFloraReal - ((vDaysFloraReal - 1) / 12);
    if (superFromFlora < 1) superFromFlora = 1;
  }

  // ------ HTML ------
  String html = "<!DOCTYPE html><html lang='es'><head>"
                "<meta charset='UTF-8'/>"
                "<meta name='viewport' content='width=device-width, initial-scale=1'/>"
                "<title>Control R4</title>"
                "<link href='https://fonts.googleapis.com/css2?family=Press+Start+2P&display=swap' rel='stylesheet'>"
                "<style>"
                /* ===== Tokens responsive ===== */
                ":root{--radius:16px;--pad:16px;--gap:12px;--pad-lg:24px;"
                "--fs-body:clamp(16px,3.5vw,18px);--fs-small:clamp(12px,2.6vw,14px);--fs-h1:clamp(22px,6vw,56px)}"
                "*,*::before,*::after{box-sizing:border-box;-webkit-tap-highlight-color:transparent}"
                "html,body{height:100%}"
                /* BODY (flujo normal para que el footer no tape nada) */
                "body{margin:0;padding:10px;font-family:'Press Start 2P',monospace;"
                "background:linear-gradient(to bottom,#0a0f1e,#111927);color:#e0e0e0;"
                "min-height:100dvh;display:flex;flex-direction:column;align-items:center;justify-content:flex-start;"
                "line-height:1.25;gap:16px;position:relative}"
                /* Glow */
                ".cloud{position:absolute;top:-40px;left:50%;transform:translateX(-50%);width:300px;height:150px;"
                "background:radial-gradient(ellipse at center,rgba(0,240,255,.2),transparent 70%);border-radius:50%;"
                "filter:blur(40px);animation:pulse 6s ease-in-out infinite;z-index:-1}"
                "@keyframes pulse{0%,100%{transform:translateX(-50%) scale(1);opacity:.3}"
                "50%{transform:translateX(-50%) scale(1.1);opacity:.5}}"
                /* Logo */
                ".logo-container{margin:0 0 4px 0;position:relative;text-align:center;animation:fadeIn 1.2s ease-out}"
                ".logo-text{font-size:var(--fs-h1);font-weight:bold;line-height:1.15;color:#00f0ff;"
                "text-shadow:0 0 12px #00f0ff,0 0 28px #00f0ff;animation:glow 3s infinite alternate}"
                "@keyframes glow{from{text-shadow:0 0 8px #00f0ff,0 0 18px #00f0ff}"
                "to{text-shadow:0 0 18px #00f0ff,0 0 36px #00f0ff}}"
                /* Glitch */
                ".glitch{position:relative;display:inline-block}"
                ".glitch::before,.glitch::after{content:attr(data-text);position:absolute;left:0;top:0;width:100%;overflow:hidden;"
                "color:#00f0ff;background:transparent;clip:rect(0,0,0,0)}"
                ".glitch::before{text-shadow:2px 0 red;animation:glitchTop 2s infinite linear alternate-reverse}"
                ".glitch::after{text-shadow:-2px 0 blue;animation:glitchBottom 2s infinite linear alternate-reverse}"
                "@keyframes glitchTop{0%{clip:rect(0,9999px,0,0)}5%{clip:rect(0,9999px,20px,0);transform:translate(-2px,-2px)}"
                "10%{clip:rect(0,9999px,10px,0);transform:translate(2px,0)}15%{clip:rect(0,9999px,5px,0);transform:translate(-1px,1px)}"
                "20%,100%{clip:rect(0,0,0,0);transform:none}}"
                "@keyframes glitchBottom{0%{clip:rect(0,0,0,0)}5%{clip:rect(20px,9999px,40px,0);transform:translate(1px,1px)}"
                "10%{clip:rect(10px,9999px,30px,0);transform:translate(-1px,-1px)}15%{clip:rect(5px,9999px,25px,0);transform:translate(1px,-1px)}"
                "20%,100%{clip:rect(0,0,0,0);transform:none}}"
                /* Card */
                ".container{margin-top:0;background-color:#1e293b;border:1px solid #00f0ff;border-radius:var(--radius);"
                "padding:var(--pad-lg);width:min(92vw,560px);max-width:560px;box-shadow:0 0 20px rgba(0,240,255,.2);"
                "animation:fadeInUp .9s ease-out;text-align:center;position:relative;display:flex;flex-direction:column;gap:var(--gap)}"
                ".card-title{font-size:var(--fs-body);margin:4px 0 6px;color:#00f0ff}"
                /* Grupo vertical */
                ".button-group-vertical{display:flex;flex-direction:column;gap:10px;justify-content:center;margin-top:10px}"
                "a{text-decoration:none}"
                "form{margin:0;width:100%;display:flex;flex-direction:column;gap:8px}"
                "input[type='submit'],button{background-color:#00f0ff;color:#0f172a;border:none;padding:12px 14px;"
                "font-size:var(--fs-body);font-weight:bold;cursor:pointer;border-radius:10px;transition:background-color .2s ease,transform .15s ease;"
                "font-family:'Press Start 2P',monospace;min-height:48px;width:100%}"
                "input[type='submit']:hover,button:hover{background-color:#00c0dd;transform:translateY(-1px)}"
                /* Separadores, pills y campos */
                ".sep{width:100%;height:1px;background:#00f0ff22;margin:12px 0}"
                ".pill{font-size:var(--fs-body);color:#00f0ff;opacity:.95}"
                ".inline-row{display:flex;flex-wrap:wrap;align-items:center;justify-content:center;gap:10px;font-size:var(--fs-body);color:#e0f7ff}"
                "input[type='number']{padding:10px 12px;font-size:var(--fs-body);min-width:110px;text-align:center;"
                "border:2px solid #00f0ff;border-radius:10px;background:#0f172a;color:#e0e0e0;font-family:'Press Start 2P',monospace}"
                /* Desktop: seguimos apilados */
                "@media(min-width:600px){.button-group-vertical{align-items:stretch}input[type='submit'],button{width:100%}}"
                /* Footer NO fijo: aparece al final al hacer scroll */
                "footer{margin-top:auto;text-align:center;font-size:var(--fs-small);color:#8aa;opacity:.9;"
                "padding:12px 12px 16px}"
                "footer p{margin:0}"
                /* Animaciones */
                "@keyframes fadeIn{from{opacity:0;transform:translateY(-20px)}to{opacity:1;transform:translateY(0)}}"
                "@keyframes fadeInUp{from{opacity:0;transform:translateY(40px)}to{opacity:1;transform:translateY(0)}}"
                "</style></head><body>";

  html += "<div class='cloud'></div>";
  html += "<div class='logo-container'><h1 class='logo-text glitch' data-text='DRUIDA BOT'>DRUIDA<br>BOT</h1></div>";

  html += "<div class='container'>";
  html += "  <div class='card-title'>Control de " + getRelayName(R4name) + "</div>";
  html += "  <div class='button-group-vertical'>";

  // Botones existentes
  html += "    <form action='/controlR4On'  method='POST'><input type='submit' value='ENCENDER'></form>";
  html += "    <form action='/controlR4Off' method='POST'><input type='submit' value='APAGAR'></form>";
  html += "    <form action='/controlR4Auto' method='POST'><input type='submit' value='AUTOMÁTICO'></form>";
  html += "    <form action='/controlR4Superciclo' method='POST'><input type='submit' value='SUPERCICLO 13/13'></form>";

  // Contadores
  html += "    <div class='sep'></div>";
  html += "    <div class='card-title'>Contadores de Ciclo</div>";
  html += "    <div class='pill'>DÍA VEGE: " + String(vDaysVege > 0 ? vDaysVege : 0) + "</div>";
  html += "    <div class='pill'>DÍA FLORA: " + String(vDaysFloraReal > 0 ? vDaysFloraReal : 0) + "</div>";
  html += "    <div class='pill'>DÍA SUPERCICLO: "
                    + String((modoR4 == SUPERCICLO1313 && superFromFlora > 0) ? superFromFlora
                              : (vDaysFloraSuper > 0 ? vDaysFloraSuper : 0))
                    + "</div>";

  // Fijar día VEGE
  html += "    <div class='sep'></div>";
  html += "    <div class='card-title'>Fijar Día de VEGE</div>";
  int defaultVege = (vDaysVege > 0) ? vDaysVege : 1;
  html += "    <form action='/setVegeDay' method='POST' onsubmit=\"return confirm('¿Fijar día actual de VEGETATIVO?');\">";
  html += "      <div class='inline-row'><input type='number' name='vegeDay' min='1' max='200' value='" + String(defaultVege) + "'></div>";
  html += "      <input type='submit' value='FIJAR DÍA VEGE'>";
  html += "    </form>";

  // Fijar día FLORA
  html += "    <div class='sep'></div>";
  html += "    <div class='card-title'>Fijar Día de FLORA</div>";
  int defaultFlora = (vDaysFloraReal > 0) ? vDaysFloraReal : 1;
  html += "    <form action='/setFloraDay' method='POST' onsubmit=\"return confirm('¿Fijar día actual de FLORACIÓN?');\">";
  html += "      <div class='inline-row'><input type='number' name='floraDay' min='1' max='200' value='" + String(defaultFlora) + "'></div>";
  html += "      <input type='submit' value='FIJAR DÍA FLORA'>";
  html += "    </form>";

  // Inicio / reset
  html += "    <div class='sep'></div>";
  html += "    <form action='/startVege' method='POST' onsubmit=\"return confirm('¿Iniciar VEGETATIVO? Pondrá Día=1.');\"><input type='submit' value='INICIAR VEGE'></form>";
  html += "    <form action='/startFlora' method='POST' onsubmit=\"return confirm('¿Iniciar FLORACIÓN? Pondrá Día=1.');\"><input type='submit' value='INICIAR FLORA'></form>";
  html += "    <form action='/resetVege' method='POST' onsubmit=\"return confirm('¿Reiniciar VEGETATIVO a 0?');\"><input type='submit' value='REINICIAR VEGE'></form>";
  html += "    <form action='/resetFlora' method='POST' onsubmit=\"return confirm('¿Reiniciar FLORACIÓN a 0?');\"><input type='submit' value='REINICIAR FLORA'></form>";
  html += "    <form action='/startFloraSuper' method='POST' onsubmit=\"return confirm('¿Iniciar FLORACIÓN + SUPERCICLO? Pondrá Día Real=1 y Día Virtual=1.');\"><input type='submit' value='FLORA SUPERCICLO'></form>";

  html += "    <a href='/control'><button>VOLVER</button></a>";
  html += "  </div>";
  html += "</div>";

  // Footer no fijo
  html += "<footer><p>druidadata@gmail.com<br>DataDruida</p></footer>";
  html += "</body></html>";

  server.send(200, "text/html", html);
}







void handleControlR4Nube() {
    moveServoSlowly(0);  // Posición Nube
    handleConfirmation("Posicion: Nube", "/controlR4");
    currentPosition = 0;
    Guardado_General();
}

void handleControlR4Mediodia() {
    moveServoSlowly(180);  // Posición Medio Día
    handleConfirmation("Posicion: Medio Dia", "/controlR4");
    currentPosition = 180;
    Guardado_General();
}




void handleControlR4On() {
    estadoR4 = 1; // Cambiar el estado a encendido
    modoR4 = 1; // Modo manual
    Guardado_General();
    handleConfirmation(getRelayName(R4name) + " encendida", "/controlR4"); // Mostrar confirmación y redirigir
}

void handleControlR4Off() {
    estadoR4 = 0; // Cambiar el estado a apagado
    modoR4 = 1; // Modo manual
    Guardado_General();
    handleConfirmation(getRelayName(R4name) + " apagada", "/controlR4"); // Mostrar confirmación y redirigir
}

void handleControlR4Auto() {
    modoR4 = 2; // Modo automático
    Guardado_General();
    handleConfirmation(getRelayName(R4name) + " en modo automatico", "/controlR4"); // Mostrar confirmación y redirigir
}

void handleControlR4Superciclo() {
    modoR4 = 4; // Modo superciclo
    Guardado_General();
    handleConfirmation(getRelayName(R4name) + " en modo Superciclo", "/controlR4"); // Mostrar confirmación y redirigir
}


void handleStartVege() {
  DateTime now = rtc.now();
  vegeStartEpoch = now.unixtime();  // UTC
  vegeActive     = true;
  vegeDays       = 1;               // opcional (UI inmediata)

  // Alinear referencia de día local (si seguís usando tickDaily/lastDateKey)
  lastDateKey    = localDateKey(now);

  Guardado_General();
  handleConfirmation("VEGETATIVO iniciado · Día 1", "/controlR4");
}

void handleStartFlora() {
  DateTime now = rtc.now();
  floraStartEpoch = now.unixtime(); // UTC
  floraActive     = true;
  floraDays       = 1;              // opcional (UI inmediata)

  lastDateKey     = localDateKey(now);

  Guardado_General();
  handleConfirmation("FLORACIÓN iniciada · Día 1", "/controlR4");
}

// Si “SUPERCICLO” no requiere un epoch distinto, no dupliques estado.
// Usá el mismo floraStartEpoch y calculá virtualDaysSince() al mostrar.
void handleStartFloraSuper() {
  DateTime now = rtc.now();

  floraStartEpoch = now.unixtime();
  floraActive     = true;
  floraDays       = 1;               // opcional

  lastDateKey     = localDateKey(now);

  // Podés marcar un flag para UI si querés:
  superEnabled    = true;            // <-- si no existe, crealo como bool

  Guardado_General();
  handleConfirmation("FLORACIÓN + SUPERCICLO iniciados · Día 1", "/controlR4");
}

void handleResetVege() {
  vegeStartEpoch = 0;
  vegeActive     = false;
  vegeDays       = 0;
  Guardado_General();
  handleConfirmation("VEGETATIVO reiniciado a 0", "/controlR4");
}

void handleResetFlora() {
  floraStartEpoch = 0;
  floraActive     = false;
  floraDays       = 0;
  superEnabled    = false;  // si usás el flag
  Guardado_General();
  handleConfirmation("FLORACIÓN reiniciada a 0", "/controlR4");
}





void handleConfig() {
  String html =
    "<!DOCTYPE html><html lang='es'><head>"
    "<meta charset='UTF-8'/>"
    "<meta name='viewport' content='width=device-width, initial-scale=1'/>"
    "<title>Configuración Druida BOT</title>"
    "<link href='https://fonts.googleapis.com/css2?family=Press+Start+2P&display=swap' rel='stylesheet'>"
    "<style>"
      /* ===== Tokens responsive ===== */
      ":root{--radius:16px;--pad:16px;--gap:12px;--pad-lg:24px;"
      "--fs-body:clamp(16px,3.5vw,18px);--fs-small:clamp(12px,2.6vw,14px);--fs-h1:clamp(22px,6vw,56px)}"
      "*,*::before,*::after{box-sizing:border-box;-webkit-tap-highlight-color:transparent}"
      "html,body{height:100%}"

      /* ===== Layout base: footer NO fijo ===== */
      "body{margin:0;padding:0;font-family:'Press Start 2P',monospace;"
      "background:linear-gradient(to bottom,#0a0f1e,#111927);color:#e0e0e0;"
      "min-height:100dvh;display:flex;flex-direction:column;align-items:stretch;line-height:1.25}"

      ".main{flex:1;display:flex;flex-direction:column;align-items:center;gap:16px;padding:10px;"
      "padding-bottom:calc(env(safe-area-inset-bottom) + 12px);position:relative}"

      /* ===== Glow decorativo ===== */
      ".cloud{position:absolute;top:-40px;left:50%;transform:translateX(-50%);width:300px;height:150px;"
      "background:radial-gradient(ellipse at center,rgba(0,240,255,.2),transparent 70%);border-radius:50%;"
      "filter:blur(40px);animation:pulse 6s ease-in-out infinite;z-index:-1}"
      "@keyframes pulse{0%,100%{transform:translateX(-50%) scale(1);opacity:.3}"
      "50%{transform:translateX(-50%) scale(1.1);opacity:.5}}"

      /* ===== Logo (clickeable) ===== */
      ".logo-container{margin:0 0 4px 0;position:relative;text-align:center;animation:fadeIn 1.2s ease-out;"
      "display:inline-block;text-decoration:none;color:inherit;cursor:pointer}"
      ".logo-container:hover .logo-text{filter:brightness(1.1)}"
      ".logo-text{font-size:var(--fs-h1);font-weight:bold;line-height:1.15;color:#00f0ff;"
      "text-shadow:0 0 12px #00f0ff,0 0 28px #00f0ff;animation:glow 3s infinite alternate}"
      "@keyframes glow{from{text-shadow:0 0 8px #00f0ff,0 0 18px #00f0ff}"
      "to{text-shadow:0 0 18px #00f0ff,0 0 36px #00f0ff}}"

      /* ===== Glitch ===== */
      ".glitch{position:relative;display:inline-block}"
      ".glitch::before,.glitch::after{content:attr(data-text);position:absolute;left:0;top:0;width:100%;overflow:hidden;"
      "color:#00f0ff;background:transparent;clip:rect(0,0,0,0)}"
      ".glitch::before{text-shadow:2px 0 red;animation:glitchTop 2s infinite linear alternate-reverse}"
      ".glitch::after{text-shadow:-2px 0 blue;animation:glitchBottom 2s infinite linear alternate-reverse}"
      "@keyframes glitchTop{0%{clip:rect(0,9999px,0,0)}5%{clip:rect(0,9999px,20px,0);transform:translate(-2px,-2px)}"
      "10%{clip:rect(0,9999px,10px,0);transform:translate(2px,0)}15%{clip:rect(0,9999px,5px,0);transform:translate(-1px,1px)}"
      "20%,100%{clip:rect(0,0,0,0);transform:none}}"
      "@keyframes glitchBottom{0%{clip:rect(0,0,0,0)}5%{clip:rect(20px,9999px,40px,0);transform:translate(1px,1px)}"
      "10%{clip:rect(10px,9999px,30px,0);transform:translate(-1px,-1px)}15%{clip:rect(5px,9999px,25px,0);transform:translate(1px,-1px)}"
      "20%,100%{clip:rect(0,0,0,0);transform:none}}"

      /* ===== Card contenedor ===== */
      ".container{margin-top:0;background-color:#1e293b;border:1px solid #00f0ff;border-radius:var(--radius);"
      "padding:var(--pad-lg);width:min(92vw,560px);max-width:560px;box-shadow:0 0 20px rgba(0,240,255,.2);"
      "animation:fadeInUp .9s ease-out;text-align:center;position:relative;display:flex;flex-direction:column;gap:var(--gap)}"
      ".card-title{font-size:var(--fs-body);margin:4px 0 6px;color:#00f0ff}"

      /* ===== Grupo de botones ===== */
      ".button-group-vertical{display:flex;flex-direction:column;gap:10px;justify-content:center;margin-top:10px}"
      "a{text-decoration:none}"
      "button{background-color:#00f0ff;color:#0f172a;border:none;padding:12px 14px;font-size:var(--fs-body);font-weight:bold;"
      "cursor:pointer;border-radius:10px;transition:background-color .2s ease,transform .15s ease;"
      "font-family:'Press Start 2P',monospace;min-height:48px;width:100%}"
      "button:hover{background-color:#00c0dd;transform:translateY(-1px)}"

      /* ===== Footer estático (no tapa botones) ===== */
      "footer{position:static;text-align:center;font-size:var(--fs-small);color:#8aa;opacity:.9;margin:0;"
      "padding:8px 12px;padding-bottom:calc(env(safe-area-inset-bottom) + 8px)}"
      "footer p{margin:0}"

      /* ===== Animaciones ===== */
      "@keyframes fadeIn{from{opacity:0;transform:translateY(-20px)}to{opacity:1;transform:translateY(0)}}"
      "@keyframes fadeInUp{from{opacity:0;transform:translateY(40px)}to{opacity:1;transform:translateY(0)}}"
    "</style></head><body>";

  /* MAIN (crece y separa del footer) */
  html += "<main class='main'>";
  html += "<div class='cloud'></div>";

  /* Logo: enlace a handleRoot (ruta '/') */
  html += "<a href='/' class='logo-container' aria-label='Ir a inicio'>";
  html += "  <h1 class='logo-text glitch' data-text='DRUIDA BOT'>DRUIDA<br>BOT</h1>";
  html += "</a>";

  html += "<div class='container'>";
  html += "  <div class='card-title'>Configuración de Druida BOT</div>";
  html += "  <div class='button-group-vertical'>";
  html += "    <a href='/configR5'><button>" + getRelayName(R5name) + "</button></a>";
  html += "    <a href='/configR1'><button>" + getRelayName(R1name) + "</button></a>";
  html += "    <a href='/configR2'><button>" + getRelayName(R2name) + "</button></a>";
  html += "    <a href='/configR3'><button>" + getRelayName(R3name) + "</button></a>";
  html += "    <a href='/configR4'><button>" + getRelayName(R4name) + "</button></a>";
  html += "    <a href='/configWiFi'><button>WiFi</button></a>";
  html += "    <a href='/'><button>VOLVER</button></a>";
  html += "  </div>";
  html += "</div>";

  html += "</main>";

  /* Footer NO fijo */
  html += "<footer><p>druidadata@gmail.com<br>DataDruida</p></footer>";

  html += "</body></html>";

  server.send(200, "text/html", html);
}





/*void handleConfigIR() {
    String html = "<html><head><style>";
    html += "body { background-color: #00264d; color: white; font-family: Arial, sans-serif; margin: 0; padding: 20px; }";
    html += "h1 { color: #00bfff; text-align: center; }";
    html += ".container { max-width: 800px; margin: 0 auto; }";
    html += "button { background-color: #1c75bc; color: white; border: 2px solid #00ff00; padding: 15px 30px; font-size: 18px; border-radius: 10px; cursor: pointer; margin: 10px; }";
    html += "button:hover { background-color: #004080; }";
    html += "textarea { width: 100%; height: 200px; margin: 20px 0; padding: 10px; font-family: monospace; }";
    html += ".form-group { margin: 20px 0; }";
    html += "label { display: block; margin-bottom: 5px; }";
    html += ".success { color: #00ff00; font-weight: bold; margin: 20px 0; }";
    html += ".error { color: #ff0000; font-weight: bold; margin: 20px 0; }";
    html += "</style></head><body>";
    
    html += "<div class=\"container\">";
    html += "<h1>Configuración de Señal IR</h1>";
    
    // Mostrar mensaje de éxito si viene de una captura
    if (server.hasArg("success")) {
        html += "<div class=\"success\">¡Señal IR capturada correctamente!</div>";
    } else if (server.hasArg("error")) {
        html += "<div class=\"error\">Error al capturar señal IR. Intente nuevamente.</div>";
    }

    if (server.hasArg("emitted")) {
    html += "<div class=\"success\">¡Señal IR emitida correctamente!</div>";
}
    
    // Formulario para editar manualmente el array
    html += "<form action=\"/saveIRConfig\" method=\"post\">";
    html += "<div class=\"form-group\">";
    html += "<label for=\"irArray\">Array de Señal IR (separado por comas):</label>";
    html += "<textarea id=\"irArray\" name=\"irArray\">";
    
    // Mostrar los valores actuales del array
    for (int i = 0; i < rawDataLen; i++) {
        html += String(rawData[i]);
        if (i < rawDataLen - 1) {
            html += ",";
        }
    }
    html += "</textarea>";
    html += "</div>";
    html += "<button type=\"submit\">Guardar Configuración</button>";
    html += "</form>";
    
    // Botón para capturar automáticamente una señal IR
    html += "<a href=\"/captureIR\"><button>Capturar Señal IR Automaticamente</button></a>";

    // Boton Emitir IR
    html += "<a href=\"/emitIR\"><button style=\"background-color: #4CAF50;\">Emitir Señal IR</button></a>";
    
    // Botón para volver
    html += "<a href=\"/config\"><button>Volver</button></a>";
    html += "</div>";
    html += "</body></html>";
    
    server.send(200, "text/html", html);
}

void handleCaptureIR() {
    // Mostrar página de espera para captura IR con JavaScript para verificar
    String html = "<html><head><style>";
    html += "body { background-color: #00264d; color: white; font-family: Arial, sans-serif; text-align: center; padding: 50px; }";
    html += "h1 { color: #00bfff; }";
    html += ".spinner { border: 8px solid #f3f3f3; border-top: 8px solid #00ff00; border-radius: 50%; width: 60px; height: 60px; animation: spin 2s linear infinite; margin: 30px auto; }";
    html += "@keyframes spin { 0% { transform: rotate(0deg); } 100% { transform: rotate(360deg); } }";
    html += ".status { margin: 20px 0; font-size: 18px; }";
    html += "</style>";
    html += "<script>";
    html += "function checkCaptureStatus() {";
    html += "  fetch('/checkIRCapture').then(response => response.json()).then(data => {";
    html += "    if (data.captured) {";
    html += "      document.getElementById('status').innerHTML = '¡Señal capturada con éxito! Redirigiendo...';";
    html += "      setTimeout(() => { window.location.href = '/configIR?success=1'; }, 2000);";
    html += "    } else {";
    html += "      setTimeout(checkCaptureStatus, 1000); // Reintentar después de 1 segundo";
    html += "    }";
    html += "  }).catch(error => {";
    html += "    console.error('Error:', error);";
    html += "    setTimeout(checkCaptureStatus, 1000); // Reintentar después de 1 segundo";
    html += "  });";
    html += "}";
    html += "window.onload = function() { checkCaptureStatus(); };";
    html += "</script>";
    html += "</head><body>";
    html += "<h1>Preparado para capturar señal IR</h1>";
    html += "<p>Apunte el control remoto al sensor IR y presione un botón...</p>";
    html += "<div class=\"spinner\"></div>";
    html += "<div id=\"status\" class=\"status\">Capturando...</div>";
    html += "</body></html>";
    
    server.send(200, "text/html", html);
    
    // Iniciar la captura (si hay datos disponibles)
    if (irrecv.decode(&results)) {
        modificarValoresArray(false); // false para modo automático
        irCaptureDone = true; // Marcar como capturado
    } else {
        irrecv.resume(); // Prepararse para recibir la siguiente señal
    }
}

// Nueva función para verificar el estado de captura
void handleCheckIRCapture() {
    StaticJsonDocument<200> doc;
    doc["captured"] = irCaptureDone;
    
    String response;
    serializeJson(doc, response);
    
    server.send(200, "application/json", response);
    
    if (irCaptureDone) {
        irCaptureDone = false; // Resetear el estado para la próxima captura
    }
}

void handleSaveIRConfig() {
    if (server.hasArg("irArray")) {
        String arrayStr = server.arg("irArray");
        
        // Procesar el string
        int index = 0;
        int startPos = 0;
        int endPos = arrayStr.indexOf(',');
        
        while (endPos != -1 && index < 150) {
            rawData[index++] = arrayStr.substring(startPos, endPos).toInt();
            startPos = endPos + 1;
            endPos = arrayStr.indexOf(',', startPos);
        }
        
        if (startPos < arrayStr.length() && index < 150) {
            rawData[index++] = arrayStr.substring(startPos).toInt();
        }
        
        rawDataLen = index;
        Guardado_General();
        
        server.sendHeader("Location", "/configIR?success=1", true);
        server.send(302, "text/plain", "");
    }
}

void handleEmitIR() {
  emitIRSignal();
  
  // Redirigir de vuelta a la página de configuración con mensaje de éxito
  server.sendHeader("Location", "/configIR?emitted=1", true);
  server.send(302, "text/plain", "");
}*/

void handleConfigWiFi() {
  String html = "<!DOCTYPE html><html lang='es'><head>"
                "<meta charset='UTF-8'/>"
                "<meta name='viewport' content='width=device-width, initial-scale=1'/>"
                "<title>Configuración WiFi</title>"
                "<link href='https://fonts.googleapis.com/css2?family=Press+Start+2P&display=swap' rel='stylesheet'>"
                "<style>"
                /* ===== Tokens responsive ===== */
                ":root{--radius:16px;--pad:16px;--gap:12px;--pad-lg:24px;"
                "--fs-body:clamp(16px,3.5vw,18px);--fs-small:clamp(12px,2.6vw,14px);--fs-h1:clamp(22px,6vw,56px)}"
                "*,*::before,*::after{box-sizing:border-box;-webkit-tap-highlight-color:transparent}"
                "html,body{height:100%}"
                /* BODY como flex-col (sticky footer) */
                "body{margin:0;padding:10px;font-family:'Press Start 2P',monospace;"
                "background:linear-gradient(to bottom,#0a0f1e,#111927);color:#e0e0e0;"
                "min-height:100dvh;display:flex;flex-direction:column;gap:16px;position:relative}"
                /* Área principal que ocupa el alto disponible */
                "main{flex:1;display:flex;flex-direction:column;align-items:center;gap:16px}"
                /* Glow de fondo sutil */
                ".cloud{position:absolute;top:-40px;left:50%;transform:translateX(-50%);width:300px;height:150px;"
                "background:radial-gradient(ellipse at center,rgba(0,240,255,.2),transparent 70%);border-radius:50%;"
                "filter:blur(40px);animation:pulse 6s ease-in-out infinite;z-index:-1}"
                "@keyframes pulse{0%,100%{transform:translateX(-50%) scale(1);opacity:.3}"
                "50%{transform:translateX(-50%) scale(1.1);opacity:.5}}"
                /* Logo / título */
                ".logo-container{margin:0 0 4px 0;position:relative;text-align:center;animation:fadeIn 1.2s ease-out;"
                "display:inline-block;text-decoration:none;color:inherit;cursor:pointer}"
                ".logo-container:hover .logo-text{filter:brightness(1.1)}"
                ".logo-text{font-size:var(--fs-h1);font-weight:bold;line-height:1.15;color:#00f0ff;"
                "text-shadow:0 0 12px #00f0ff,0 0 28px #00f0ff;animation:glow 3s infinite alternate}"
                "@keyframes glow{from{text-shadow:0 0 8px #00f0ff,0 0 18px #00f0ff}"
                "to{text-shadow:0 0 18px #00f0ff,0 0 36px #00f0ff}}"
                /* Glitch */
                ".glitch{position:relative;display:inline-block}"
                ".glitch::before,.glitch::after{content:attr(data-text);position:absolute;left:0;top:0;width:100%;overflow:hidden;"
                "color:#00f0ff;background:transparent;clip:rect(0,0,0,0)}"
                ".glitch::before{text-shadow:2px 0 red;animation:glitchTop 2s infinite linear alternate-reverse}"
                ".glitch::after{text-shadow:-2px 0 blue;animation:glitchBottom 2s infinite linear alternate-reverse}"
                "@keyframes glitchTop{0%{clip:rect(0,9999px,0,0)}5%{clip:rect(0,9999px,20px,0);transform:translate(-2px,-2px)}"
                "10%{clip:rect(0,9999px,10px,0);transform:translate(2px,0)}15%{clip:rect(0,9999px,5px,0);transform:translate(-1px,1px)}"
                "20%,100%{clip:rect(0,0,0,0);transform:none}}"
                "@keyframes glitchBottom{0%{clip:rect(0,0,0,0)}5%{clip:rect(20px,9999px,40px,0);transform:translate(1px,1px)}"
                "10%{clip:rect(10px,9999px,30px,0);transform:translate(-1px,-1px)}15%{clip:rect(5px,9999px,25px,0);transform:translate(1px,-1px)}"
                "20%,100%{clip:rect(0,0,0,0);transform:none}}"
                /* Card contenedor */
                ".container{margin-top:0;background-color:#1e293b;border:1px solid #00f0ff;border-radius:var(--radius);"
                "padding:var(--pad-lg);width:min(92vw,560px);max-width:560px;box-shadow:0 0 20px rgba(0,240,255,.2);"
                "animation:fadeInUp .9s ease-out;text-align:center;display:flex;flex-direction:column;gap:var(--gap)}"
                ".card-title{font-size:var(--fs-body);margin:4px 0 6px;color:#00f0ff}"
                /* Form */
                "form{width:100%;display:flex;flex-direction:column;gap:10px}"
                "label{font-size:var(--fs-small);text-align:left;color:#e0e0e0;opacity:.9}"
                "input[type='text'],input[type='password'],input[type='number'],select{"
                "font-family:'Press Start 2P',monospace;font-size:var(--fs-small);width:100%;padding:10px;border-radius:10px;"
                "background:#0f172a;color:#e0e0e0;border:2px solid #00f0ff;text-align:center;box-sizing:border-box;min-height:48px}"
                "select{appearance:none;-webkit-appearance:none;-moz-appearance:none}"
                /* Botones apilados */
                ".button-group-vertical{display:flex;flex-direction:column;gap:10px;justify-content:center;margin-top:10px}"
                "a{text-decoration:none}"
                "button,.btn,input[type='submit']{background-color:#00f0ff;color:#0f172a;border:none;padding:12px 14px;"
                "font-size:var(--fs-body);font-weight:bold;cursor:pointer;border-radius:10px;transition:background-color .2s ease,transform .15s ease;"
                "font-family:'Press Start 2P',monospace;min-height:48px;width:100%}"
                "button:hover,.btn:hover,input[type='submit']:hover{background-color:#00c0dd;transform:translateY(-1px)}"
                "@media(min-width:600px){.button-group-vertical{align-items:stretch}button,.btn,input[type='submit']{width:100%}}"
                /* Footer STICKY (no fixed) */
                "footer{margin-top:auto;text-align:center;font-size:var(--fs-small);color:#8aa;opacity:.9;"
                "padding:0 12px 8px}"
                "footer p{margin:0}"
                /* Animaciones */
                "@keyframes fadeIn{from{opacity:0;transform:translateY(-20px)}to{opacity:1;transform:translateY(0)}}"
                "@keyframes fadeInUp{from{opacity:0;transform:translateY(40px)}to{opacity:1;transform:translateY(0)}}"
                "</style></head><body>";

  html += "<div class='cloud'></div>";

  html += "<main>";
  // Logo
  html += "<a href='/' class='logo-container' aria-label='Ir a inicio'>"
          "<h1 class='logo-text glitch' data-text='DRUIDA BOT'>DRUIDA<br>BOT</h1>"
          "</a>";

  html += "<div class='container'>";
  html += "  <div class='card-title'>Configuración WiFi</div>";

  // === Form principal: guardar config ===
  html += "  <form action='/saveConfigWiFi' method='POST'>";

  // SSID editable
  html += "    <label for='ssidInput'>Nombre de red (SSID)</label>";
  html += "    <input type='text' id='ssidInput' name='ssid' value='" + ssid + "' maxlength='32'>";

  // Botón BUSCAR REDES
  html += "    <div class='button-group-vertical'>";
  html += "      <a href='?scan=1'><button type='button'>BUSCAR REDES</button></a>";
  html += "    </div>";

  // Escaneo de redes solo si el usuario pidió
  int n = 0;
  if (server.hasArg("scan") && server.arg("scan") == "1") {
    n = WiFi.scanNetworks();
    html += "    <label for='ssidSelect'>Redes disponibles</label>";
    html += "    <select id='ssidSelect'>";
    if (n == 0) {
      html += "      <option>No se encontraron redes</option>";
    } else {
      for (int i = 0; i < n; ++i) {
        String ssidFound = WiFi.SSID(i);
        html += "      <option value='" + ssidFound + "'>" + ssidFound + "</option>";
      }
    }
    html += "    </select>";
  }

  html += "    <label for='pwd'>Contraseña WiFi</label>";
  html += "    <input id='pwd' type='password' name='password' placeholder='Contraseña WiFi' value='" + password + "'>";

  html += "    <label for='chatid'>Chat ID Telegram</label>";
  html += "    <input id='chatid' type='text' name='chat_id' placeholder='Chat ID Telegram' value='" + String(chat_id) + "'>";

  html += "    <label for='gmin'>GOOGLE (minutos)</label>";
  html += "    <input id='gmin' type='number' name='tiempoGoogle' placeholder='GOOGLE (minutos)' value='" + String(tiempoGoogle) + "'>";

  html += "    <label for='tmin'>TELEGRAM (minutos)</label>";
  html += "    <input id='tmin' type='number' name='tiempoTelegram' placeholder='TELEGRAM (minutos)' value='" + String(tiempoTelegram) + "'>";

  // Botones GUARDAR / VOLVER
  html += "    <div class='button-group-vertical'>";
  html += "      <input type='submit' value='GUARDAR'>";
  html += "      <a href='/'><button type='button'>VOLVER</button></a>";
  html += "    </div>";
  html += "  </form>";

  // === Form CONECTAR WiFi ===
  html += "  <form action='/connectWiFi' method='POST'>";
  html += "    <div class='button-group-vertical'>";
  html += "      <input type='submit' value='CONECTAR WiFi'>";
  html += "    </div>";
  html += "  </form>";

  // === Form DESCONECTAR WiFi ===
  html += "  <form action='/disconnectWiFi' method='POST'>";
  html += "    <div class='button-group-vertical'>";
  html += "      <input type='submit' value='DESCONECTAR WiFi'>";
  html += "    </div>";
  html += "  </form>";

  html += "</div>"; // .container
  html += "</main>";

  html += "<footer><p>druidadata@gmail.com<br>DataDruida</p></footer>";

  // Sincronizar selector con campo de texto (si hubo escaneo)
  html += "<script>"
          "const sel=document.getElementById('ssidSelect');"
          "if(sel){sel.addEventListener('change',()=>{"
          "document.getElementById('ssidInput').value=sel.value;});}"
          "</script>";

  html += "</body></html>";

  server.send(200, "text/html", html);
}









void connectWiFi() {
  // Lógica original
  modoWiFi = 1;
  reset    = 1;
  Guardado_General();

  String mensaje     = "Conectando a WiFi…";
  String redireccion = "/config";

  String html =
  "<!DOCTYPE html><html lang='es'><head>"
  "<meta charset='UTF-8'/>"
  "<meta name='viewport' content='width=device-width, initial-scale=1'/>"
  "<title>Conectando WiFi</title>"
  "<meta http-equiv='refresh' content='3; url=" + redireccion + "'>"
  "<link href='https://fonts.googleapis.com/css2?family=Press+Start+2P&display=swap' rel='stylesheet'>"
  "<style>"
  /* ===== Tokens responsive ===== */
  ":root{--gap:clamp(12px,2.5vw,20px);--pad:clamp(16px,3vw,24px);--radius:16px;"
  "--fs-h1:clamp(18px,5vw,36px);--fs-body:clamp(14px,3.5vw,18px);--fs-small:clamp(11px,2.8vw,14px);"
  "--c-bg:#0f172a;--c-card:#1e40af;--c-ink:#e0e0e0;--c-cyan:#00f0ff;--c-border:#00ffcc}"

  "*,*::before,*::after{box-sizing:border-box;-webkit-tap-highlight-color:transparent}"
  "html,body{height:100%}"

  /* ===== Layout ===== */
  "body{margin:0;background:var(--c-bg);color:var(--c-ink);font-family:'Press Start 2P',monospace;"
  "display:flex;flex-direction:column;min-height:100dvh}"
  "main{flex:1;display:flex;flex-direction:column;align-items:center;justify-content:center;gap:var(--gap);padding:var(--pad)}"

  /* Logo + glitch sutil */
  ".logo{font-size:var(--fs-h1);margin:0;color:var(--c-cyan);"
  "text-shadow:0 0 6px var(--c-cyan),0 0 14px var(--c-cyan);animation:glow 3s ease-in-out infinite alternate}"
  ".glitch{position:relative;display:inline-block}"
  ".glitch::before,.glitch::after{content:attr(data-text);position:absolute;left:0;top:0;inset:0;clip:rect(0,0,0,0)}"
  ".glitch::before{color:#ff00ff;text-shadow:1px 0 #ff00ff;animation:gt 2s infinite linear alternate-reverse}"
  ".glitch::after{color:#00ffff;text-shadow:-1px 0 #00ffff;animation:gb 2s infinite linear alternate-reverse}"
  "@keyframes glow{from{text-shadow:0 0 4px var(--c-cyan),0 0 10px var(--c-cyan)}to{text-shadow:0 0 10px var(--c-cyan),0 0 22px var(--c-cyan)}}"
  "@keyframes gt{0%{clip:rect(0,9999px,0,0)}10%{clip:rect(0,9999px,16px,0);transform:translate(-1px,-1px)}20%{clip:rect(0,9999px,8px,0)}100%{clip:rect(0,0,0,0)}}"
  "@keyframes gb{0%{clip:rect(0,0,0,0)}10%{clip:rect(10px,9999px,24px,0);transform:translate(1px,1px)}20%{clip:rect(4px,9999px,14px,0)}100%{clip:rect(0,0,0,0)}}"

  /* Caja de mensaje */
  ".box{background:var(--c-card);border:2px solid var(--c-border);border-radius:var(--radius);padding:var(--pad);"
  "box-shadow:0 0 15px rgba(0,255,255,0.25);max-width:min(92vw,640px);width:100%;text-align:center;animation:fadeIn .9s ease-out}"
  ".box h1{font-size:var(--fs-body);margin:0;color:var(--c-ink)}"

  /* Progreso continuo con sheen + cambio de color al final */
  ".progress{height:12px;border:1px solid var(--c-cyan);border-radius:10px;overflow:hidden;"
  "background:rgba(0,240,255,0.08);margin-top:var(--gap);position:relative}"
  ".bar{height:100%;width:0%;background:var(--c-cyan);position:relative;"
  "animation:fill 3s linear forwards, barColor 3s linear forwards}"
  ".bar::after{content:'';position:absolute;top:0;bottom:0;width:120px;"
  "background:linear-gradient(90deg,rgba(255,255,255,0) 0%,rgba(255,255,255,.6) 50%,rgba(255,255,255,0) 100%);"
  "filter:blur(1px);transform:translateX(-120%);animation:sheen 3s linear forwards}"
  "@keyframes fill{from{width:0%}to{width:100%}}"
  "@keyframes barColor{0%{background:var(--c-cyan)}80%{background:var(--c-cyan)}100%{background:#ffffff}}"
  "@keyframes sheen{from{transform:translateX(-120%)}to{transform:translateX(100%)}}"

  ".count{font-size:var(--fs-small);opacity:.85;margin-top:8px}"

  /* Botón */
  ".actions{margin-top:var(--gap);display:flex;flex-direction:column;gap:10px}"
  "a{text-decoration:none}"
  "button{background:var(--c-cyan);color:#0b1020;border:none;border-radius:12px;padding:12px 14px;font-size:var(--fs-body);"
  "cursor:pointer;transition:transform .15s ease,filter .2s ease}"
  "button:hover{transform:translateY(-1px);filter:brightness(.92)}"

  /* Reduce motion */
  "@media(prefers-reduced-motion:reduce){"
  ".logo{animation:none}.glitch::before,.glitch::after{animation:none}"
  ".bar{animation:fill 3s linear forwards}"
  ".bar::after{animation:none;display:none}"
  "}"

  /* Animación entrada */
  "@keyframes fadeIn{from{opacity:0;transform:translateY(8px)}to{opacity:1;transform:translateY(0)}}"
  "</style></head><body>"
  "<main>"
  "<h1 class='logo glitch' data-text='DATA DRUIDA'>DATA DRUIDA</h1>"
  "<div class='box'>"
  "<h1>" + mensaje + "</h1>"
  "<div class='progress' aria-hidden='true'><div class='bar'></div></div>"
  "<div class='count' aria-live='polite'>Redirigiendo en <span id='count'>3</span>…</div>"
  "<div class='actions'><a href='" + redireccion + "'><button type='button'>IR AHORA</button></a></div>"
  "</div>"
  "</main>";

  // JS: contador + redirección; la barra se anima por CSS.
  html += "<script>"
          "let t=3;const c=document.getElementById('count');"
          "function tick(){t--; if(c) c.textContent=t; if(t<=0){location.replace('" + redireccion + "');} else {setTimeout(tick,1000);} }"
          "setTimeout(tick,1000);"
          "</script>";

  html += "</body></html>";

  server.send(200, "text/html", html);
}




void saveConfigWiFi() {
    if (server.method() == HTTP_POST) {
        if (server.hasArg("ssid")) {
            ssid = server.arg("ssid");
        }
        if (server.hasArg("password")) {
            password = server.arg("password");
        }
        if (server.hasArg("chat_id")) {
            chat_id = server.arg("chat_id");
        }
        if (server.hasArg("tiempoGoogle")) {
            tiempoGoogle = server.arg("tiempoGoogle").toInt();
            //intervaloGoogle = tiempoGoogle * 60 * 1000UL;
        }
        if (server.hasArg("tiempoTelegram")) {
            tiempoTelegram = server.arg("tiempoTelegram").toInt();
            //intervaloTelegram = tiempoTelegram * 60 * 1000UL;
        }
        if (server.hasArg("modoWiFi")) {
            modoWiFi = server.arg("modoWiFi").toInt();
        }

        handleSaveConfig(); // Función que guarda los datos en EEPROM, archivo, etc.
    } else {
        server.send(405, "text/plain", "Método no permitido");
    }
}







String formatTwoDigits(int number) {
    if (number < 10) {
        return "0" + String(number);
    }
    return String(number);
}



// Cambios en handleConfig

void handleConfigR1() {
  String html =
    "<!DOCTYPE html><html lang='es'><head>"
    "<meta charset='UTF-8'/>"
    "<meta name='viewport' content='width=device-width, initial-scale=1'/>"
    "<title>Config R1</title>"
    "<link href='https://fonts.googleapis.com/css2?family=Press+Start+2P&display=swap' rel='stylesheet'>"
    "<style>"
      /* ===== Tokens responsive (igual que ConfigR5) ===== */
      ":root{--radius:16px;--pad:16px;--gap:12px;--pad-lg:24px;"
      "--fs-body:clamp(16px,3.5vw,18px);--fs-small:clamp(12px,2.6vw,14px);--fs-h1:clamp(22px,6vw,56px)}"
      "*,*::before,*::after{box-sizing:border-box;-webkit-tap-highlight-color:transparent}"
      "html,body{height:100%}"

      /* BODY (scroll largo) */
      "body{margin:0;padding:10px;font-family:'Press Start 2P',monospace;"
        "background:linear-gradient(to bottom,#0a0f1e,#111927);color:#e0e0e0;"
        "min-height:100dvh;display:flex;flex-direction:column;align-items:center;justify-content:flex-start;"
        "line-height:1.25;gap:16px;position:relative}"

      /* Glow + Logo (igual a otras vistas) */
      ".cloud{position:absolute;top:-40px;left:50%;transform:translateX(-50%);width:300px;height:150px;"
        "background:radial-gradient(ellipse at center,rgba(0,240,255,.2),transparent 70%);border-radius:50%;"
        "filter:blur(40px);animation:pulse 6s ease-in-out infinite;z-index:-1}"
      "@keyframes pulse{0%,100%{transform:translateX(-50%) scale(1);opacity:.3}50%{transform:translateX(-50%) scale(1.1);opacity:.5}}"

      ".logo-container{margin:0 0 4px 0;position:relative;text-align:center;animation:fadeIn 1.2s ease-out}"
      ".logo-text{font-size:var(--fs-h1);font-weight:bold;line-height:1.15;color:#00f0ff;"
        "text-shadow:0 0 12px #00f0ff,0 0 28px #00f0ff;animation:glow 3s infinite alternate}"
      "@keyframes glow{from{text-shadow:0 0 8px #00f0ff,0 0 18px #00f0ff}to{text-shadow:0 0 18px #00f0ff,0 0 36px #00f0ff}}"

      ".glitch{position:relative;display:inline-block}"
      ".glitch::before,.glitch::after{content:attr(data-text);position:absolute;left:0;top:0;width:100%;overflow:hidden;color:#00f0ff;background:transparent;clip:rect(0,0,0,0)}"
      ".glitch::before{text-shadow:2px 0 red;animation:glitchTop 2s infinite linear alternate-reverse}"
      ".glitch::after{text-shadow:-2px 0 blue;animation:glitchBottom 2s infinite linear alternate-reverse}"
      "@keyframes glitchTop{0%{clip:rect(0,9999px,0,0)}5%{clip:rect(0,9999px,20px,0);transform:translate(-2px,-2px)}10%{clip:rect(0,9999px,10px,0);transform:translate(2px,0)}15%{clip:rect(0,9999px,5px,0);transform:translate(-1px,1px)}20%,100%{clip:rect(0,0,0,0);transform:none}}"
      "@keyframes glitchBottom{0%{clip:rect(0,0,0,0)}5%{clip:rect(20px,9999px,40px,0);transform:translate(1px,1px)}10%{clip:rect(10px,9999px,30px,0);transform:translate(-1px,-1px)}15%{clip:rect(5px,9999px,25px,0);transform:translate(1px,-1px)}20%,100%{clip:rect(0,0,0,0);transform:none}}"

      /* Card contenedor (igual que ConfigR5) */
      ".container{margin-top:0;background-color:#1e293b;border:1px solid #00f0ff;border-radius:var(--radius);"
        "padding:var(--pad-lg);width:min(92vw,560px);max-width:560px;box-shadow:0 0 20px rgba(0,240,255,.2);"
        "animation:fadeInUp .9s ease-out;text-align:center;position:relative;display:flex;flex-direction:column;gap:var(--gap)}"
      ".card-title{font-size:var(--fs-body);margin:4px 0 6px;color:#00f0ff}"

      /* Labels y campos (centrados como R5) */
      "label{font-size:var(--fs-body);width:100%;color:#e0e0e0;text-align:center;display:block;margin-top:10px;margin-bottom:6px}"
      "select,input[type='time'],input[type='number'],input[type='text']{font-family:'Press Start 2P',monospace;font-size:var(--fs-body);"
        "width:100%;padding:10px 12px;border-radius:10px;background:#0f172a;color:#e0e0e0;border:2px solid #00f0ff;text-align:center;box-sizing:border-box}"

      /* Sliders (mismo formato que ConfigR5) */
      ".slider-row{display:flex;align-items:center;justify-content:center;width:100%;gap:10px}"
      ".slider-row input[type='range']{flex:1}"
      ".slider-row .btn-adjust{min-width:44px}"
      ".btn-adjust{font-family:'Press Start 2P',monospace;font-size:var(--fs-body);background:#0f172a;color:#e0e0e0;"
        "border:2px solid #00f0ff;border-radius:10px;padding:10px 12px;cursor:pointer;text-align:center;transition:.2s;box-shadow:0 0 6px rgba(0,240,255,.3)}"
      ".btn-adjust:hover{background-color:#112031}"

      /* Botones (igual que R5) */
      ".button-group-vertical{display:flex;flex-direction:column;gap:10px;justify-content:center;margin-top:10px;width:100%}"
      ".btn,button{background-color:#00f0ff;color:#0f172a;border:none;padding:12px 14px;font-size:var(--fs-body);font-weight:bold;"
        "cursor:pointer;border-radius:10px;transition:background-color .2s ease,transform .15s ease;font-family:'Press Start 2P',monospace;min-height:48px;width:100%}"
      ".btn:hover,button:hover{background-color:#00c0dd;transform:translateY(-1px)}"
      ".hidden{display:none!important}"

      /* Footer no fijo (para evitar solaparse) */
      "footer{margin-top:16px;text-align:center;font-size:var(--fs-small);color:#8aa;opacity:.9;padding:12px}"
      "footer p{margin:0}"

      /* Animaciones */
      "@keyframes fadeInUp{from{opacity:0;transform:translateY(40px)}to{opacity:1;transform:translateY(0)}}"
    "</style></head>";

  html += "<body>";
  html += "<div class='cloud'></div>";
  html += "<div class='logo-container'><h1 class='logo-text glitch' data-text='DRUIDA BOT'>DRUIDA<br>BOT</h1></div>";

  html += "<div class='container'>";
  html += "  <div class='card-title'>Config de " + getRelayName(R1name) + "</div>";
  html += "  <form action='/saveConfigR1' method='POST' id='formConfigR1'>";

  /* ===== MODO (siempre visible) ===== */
  html +=
    "    <label for='modoR1'>MODO</label>"
    "    <select id='modoR1' name='modoR1' onchange='toggleByMode()'>"
    "      <option value='1'" + String((modoR1==1) ? " selected" : "") + ">Manual</option>"
    "      <option value='2'" + String((modoR1==2) ? " selected" : "") + ">Automático</option>"
    "      <option value='6'" + String((modoR1==6) ? " selected" : "") + ">Timer</option>"
    "    </select>";

  /* ===== ETIQUETA (siempre visible, como en ConfigR5) ===== */
  html +=
    "    <label for='R1name'>ETIQUETA</label>"
    "    <select id='R1name' name='R1name'>";
  for (int i = 0; i < 7; i++) {
    html += "      <option value='" + String(i) + "'" + String((R1name==i) ? " selected" : "") + ">" + relayNames[i] + "</option>";
  }
  html += "    </select>";

  /* ===== SECCIÓN: MANUAL ===== */
  html +=
    "    <div id='secManual' class='mode-sec'>"
    "      <div class='button-group-vertical'>"
    "        <button class='btn' type='button' onclick='location.href=\"/controlR1On\"'>ENCENDER</button>"
    "        <button class='btn' type='button' onclick='location.href=\"/controlR1Off\"'>APAGAR</button>"
    "      </div>"
    "    </div>";

  /* ===== SECCIÓN: AUTO (sliders formato R5) ===== */
  html +=
    "    <div id='secAuto' class='mode-sec'>"
    "      <label for='paramR1'>PARÁMETRO</label>"
    "      <select id='paramR1' name='paramR1'>"
    "        <option value='1'" + String((paramR1==1) ? " selected" : "") + ">Humedad</option>"
    "        <option value='2'" + String((paramR1==2) ? " selected" : "") + ">Temperatura</option>"
    "        <option value='3'" + String((paramR1==3) ? " selected" : "") + ">VPD</option>"
    "      </select>"

    "      <label for='direccionR1'>OBJETIVO</label>"
    "      <select id='direccionR1' name='direccionR1'>"
    "        <option value='0'" + String((direccionR1==0) ? " selected" : "") + ">SUBIR</option>"
    "        <option value='1'" + String((direccionR1==1) ? " selected" : "") + ">BAJAR</option>"
    "      </select>"

    "      <label for='minR1'>MÍNIMO</label>"
    "      <div class='slider-row'>"
    "        <input type='range' min='0' max='100' step='0.1' id='minR1Range' value='" + String(minR1,1) + "' oninput='syncMinR1(this.value)'>"
    "        <input type='number' id='minR1' name='minR1' min='0' max='100' step='0.1' value='" + String(minR1,1) + "' oninput='syncMinR1(this.value)'>"
    "        <button type='button' class='btn-adjust' id='minR1minus' onclick='adjustValue(\"minR1\",-1)'>−</button>"
    "        <button type='button' class='btn-adjust' id='minR1plus'  onclick='adjustValue(\"minR1\",1)'>+</button>"
    "      </div>"

    "      <label for='maxR1'>MÁXIMO</label>"
    "      <div class='slider-row'>"
    "        <input type='range' min='0' max='100' step='0.1' id='maxR1Range' value='" + String(maxR1,1) + "' oninput='syncMaxR1(this.value)'>"
    "        <input type='number' id='maxR1' name='maxR1' step='0.1' value='" + String(maxR1,1) + "' oninput='syncMaxR1(this.value)'>"
    "        <button type='button' class='btn-adjust' id='maxR1minus' onclick='adjustValue(\"maxR1\",-1)'>−</button>"
    "        <button type='button' class='btn-adjust' id='maxR1plus'  onclick='adjustValue(\"maxR1\",1)'>+</button>"
    "      </div>"
    "    </div>";

  /* ===== SECCIÓN: TIMER ===== */
  html +=
    "    <div id='secTimer' class='mode-sec'>"
    "      <label for='horaOnR1'>HORA DE ENCENDIDO</label>"
    "      <input type='time' id='horaOnR1' name='horaOnR1' value='" + formatTwoDigits(horaOnR1) + ":" + formatTwoDigits(minOnR1) + "'>"
    "      <label for='horaOffR1'>HORA DE APAGADO</label>"
    "      <input type='time' id='horaOffR1' name='horaOffR1' value='" + formatTwoDigits(horaOffR1) + ":" + formatTwoDigits(minOffR1) + "'>"
    "    </div>";

  /* ===== Botonera común ===== */
  html +=
    "    <div class='button-group-vertical'>"
    "      <button class='btn' type='submit'>GUARDAR</button>"
    "      <button class='btn' type='button' onclick='location.href=\"/controlR1\"'>CONTROL</button>"
    "      <button class='btn' type='button' onclick='location.href=\"/config\"'>VOLVER</button>"
    "    </div>";

  html += "  </form>";
  html += "</div>";

  html += "<footer><p>druidadata@gmail.com<br>DataDruida</p></footer>";

  /* ===== Scripts (idénticos a ConfigR5, adaptados a R1) ===== */
  html +=
    "<script>"
    "function qs(id){return document.getElementById(id)}"
    "function setDisabled(containerId,dis){var el=qs(containerId); if(!el) return; var inputs=el.querySelectorAll('input,select,button,textarea'); for(var i=0;i<inputs.length;i++){inputs[i].disabled=dis}}"
    "function hideAll(){qs('secManual').classList.add('hidden');qs('secAuto').classList.add('hidden');qs('secTimer').classList.add('hidden');setDisabled('secManual',true);setDisabled('secAuto',true);setDisabled('secTimer',true)}"
    "function toggleByMode(){var m=qs('modoR1').value; hideAll(); if(m=='1'){qs('secManual').classList.remove('hidden');setDisabled('secManual',false);} else if(m=='2'){qs('secAuto').classList.remove('hidden');setDisabled('secAuto',false);} else if(m=='6'){qs('secTimer').classList.remove('hidden');setDisabled('secTimer',false);} updateButtons();}"
    "function adjustValue(id,delta){var input=qs(id);var v=parseFloat(input.value)||0;v=(v+delta).toFixed(1);input.value=v;input.dispatchEvent(new Event('input'))}"
    "function syncMinR1(val){val=parseFloat(val);let max=parseFloat(qs('maxR1').value);if(val>max)val=max;qs('minR1').value=val.toFixed(1);qs('minR1Range').value=val;updateButtons()}"
    "function syncMaxR1(val){val=parseFloat(val);let min=parseFloat(qs('minR1').value);if(val<min)val=min;qs('maxR1').value=val.toFixed(1);qs('maxR1Range').value=val;updateButtons()}"
    "function updateButtons(){if(!qs('minR1')||!qs('maxR1')) return;let min=parseFloat(qs('minR1').value);let max=parseFloat(qs('maxR1').value);qs('minR1minus').disabled=(min<=0);qs('minR1plus').disabled=(min+0.1>max);qs('maxR1minus').disabled=(max-0.1<min);qs('maxR1plus').disabled=(max>=100)}"
    "window.onload=function(){toggleByMode();updateButtons()};"
    "</script>";

  html += "</body></html>";

  server.send(200, "text/html", html);
}






// ⬇️ Pega esta versión completa de handleConfigR5 (mismos nombres de variables y rutas genéricas)
// Ajusta SOLO las rutas de Encender/Apagar si usas otras (ej: /controlR5On y /controlR5Off)
void handleConfigR5() {
  String html = "<!DOCTYPE html><html lang='es'><head>"
                "<meta charset='UTF-8'/>"
                "<meta name='viewport' content='width=device-width, initial-scale=1'/>"
                "<title>Config R5</title>"
                "<link href='https://fonts.googleapis.com/css2?family=Press+Start+2P&display=swap' rel='stylesheet'>"
                "<style>"
                /* ===== Tokens responsive ===== */
                ":root{--radius:16px;--pad:16px;--gap:12px;--pad-lg:24px;"
                "--fs-body:clamp(16px,3.5vw,18px);--fs-small:clamp(12px,2.6vw,14px);--fs-h1:clamp(22px,6vw,56px)}"
                "*,*::before,*::after{box-sizing:border-box;-webkit-tap-highlight-color:transparent}"
                "html,body{height:100%}"
                /* BODY (scroll largo) */
                "body{margin:0;padding:10px;font-family:'Press Start 2P',monospace;"
                "background:linear-gradient(to bottom,#0a0f1e,#111927);color:#e0e0e0;"
                "min-height:100dvh;display:flex;flex-direction:column;align-items:center;justify-content:flex-start;"
                "line-height:1.25;gap:16px;position:relative}"
                /* Glow */
                ".cloud{position:absolute;top:-40px;left:50%;transform:translateX(-50%);width:300px;height:150px;"
                "background:radial-gradient(ellipse at center,rgba(0,240,255,.2),transparent 70%);border-radius:50%;"
                "filter:blur(40px);animation:pulse 6s ease-in-out infinite;z-index:-1}"
                "@keyframes pulse{0%,100%{transform:translateX(-50%) scale(1);opacity:.3}50%{transform:translateX(-50%) scale(1.1);opacity:.5}}"
                /* Logo */
                ".logo-container{margin:0 0 4px 0;position:relative;text-align:center;animation:fadeIn 1.2s ease-out}"
                ".logo-text{font-size:var(--fs-h1);font-weight:bold;line-height:1.15;color:#00f0ff;"
                "text-shadow:0 0 12px #00f0ff,0 0 28px #00f0ff;animation:glow 3s infinite alternate}"
                "@keyframes glow{from{text-shadow:0 0 8px #00f0ff,0 0 18px #00f0ff}to{text-shadow:0 0 18px #00f0ff,0 0 36px #00f0ff}}"
                /* Glitch */
                ".glitch{position:relative;display:inline-block}"
                ".glitch::before,.glitch::after{content:attr(data-text);position:absolute;left:0;top:0;width:100%;overflow:hidden;color:#00f0ff;background:transparent;clip:rect(0,0,0,0)}"
                ".glitch::before{text-shadow:2px 0 red;animation:glitchTop 2s infinite linear alternate-reverse}"
                ".glitch::after{text-shadow:-2px 0 blue;animation:glitchBottom 2s infinite linear alternate-reverse}"
                "@keyframes glitchTop{0%{clip:rect(0,9999px,0,0)}5%{clip:rect(0,9999px,20px,0);transform:translate(-2px,-2px)}10%{clip:rect(0,9999px,10px,0);transform:translate(2px,0)}15%{clip:rect(0,9999px,5px,0);transform:translate(-1px,1px)}20%,100%{clip:rect(0,0,0,0);transform:none}}"
                "@keyframes glitchBottom{0%{clip:rect(0,0,0,0)}5%{clip:rect(20px,9999px,40px,0);transform:translate(1px,1px)}10%{clip:rect(10px,9999px,30px,0);transform:translate(-1px,-1px)}15%{clip:rect(5px,9999px,25px,0);transform:translate(1px,-1px)}20%,100%{clip:rect(0,0,0,0);transform:none}}"
                /* Card */
                ".container{margin-top:0;background-color:#1e293b;border:1px solid #00f0ff;border-radius:var(--radius);"
                "padding:var(--pad-lg);width:min(92vw,560px);max-width:560px;box-shadow:0 0 20px rgba(0,240,255,.2);"
                "animation:fadeInUp .9s ease-out;text-align:center;position:relative;display:flex;flex-direction:column;gap:var(--gap)}"
                ".card-title{font-size:var(--fs-body);margin:4px 0 6px;color:#00f0ff}"
                /* Labels / campos */
                "label{font-size:var(--fs-body);width:100%;color:#e0e0e0;text-align:center;display:block;margin-top:10px;margin-bottom:6px}"
                "select,input[type='time'],input[type='number'],input[type='text']{font-family:'Press Start 2P',monospace;font-size:var(--fs-body);"
                "width:100%;padding:10px 12px;border-radius:10px;background:#0f172a;color:#e0e0e0;border:2px solid #00f0ff;text-align:center;box-sizing:border-box}"
                /* Sliders */
                ".slider-row{display:flex;align-items:center;justify-content:center;width:100%;gap:10px}"
                ".slider-row input[type='range']{flex:1}"
                ".slider-row .btn-adjust{min-width:44px}"
                /* Botones */
                ".button-group-vertical{display:flex;flex-direction:column;gap:10px;justify-content:center;margin-top:10px;width:100%}"
                ".btn,button{background-color:#00f0ff;color:#0f172a;border:none;padding:12px 14px;font-size:var(--fs-body);font-weight:bold;"
                "cursor:pointer;border-radius:10px;transition:background-color .2s ease,transform .15s ease;font-family:'Press Start 2P',monospace;min-height:48px;width:100%}"
                ".btn:hover,button:hover{background-color:#00c0dd;transform:translateY(-1px)}"
                ".btn-adjust{font-family:'Press Start 2P',monospace;font-size:var(--fs-body);background:#0f172a;color:#e0e0e0;border:2px solid #00f0ff;"
                "border-radius:10px;padding:10px 12px;cursor:pointer;text-align:center;transition:.2s;box-shadow:0 0 6px rgba(0,240,255,.3)}"
                ".btn-adjust:hover{background-color:#112031}"
                ".hidden{display:none!important}"
                /* Separadores / Pills */
                ".sep{width:100%;height:1px;background:#00f0ff22;margin:12px 0}"
                ".pill{font-size:var(--fs-body);color:#00f0ff;opacity:.95}"
                /* Footer no fijo */
                "footer{margin-top:16px;text-align:center;font-size:var(--fs-small);color:#8aa;opacity:.9;padding:12px}"
                "footer p{margin:0}"
                /* Animaciones */
                "@keyframes fadeIn{from{opacity:0;transform:translateY(-20px)}to{opacity:1;transform:translateY(0)}}"
                "@keyframes fadeInUp{from{opacity:0;transform:translateY(40px)}to{opacity:1;transform:translateY(0)}}"
                "</style></head><body>";

  html += "<div class='cloud'></div>";
  html += "<div class='logo-container'><h1 class='logo-text glitch' data-text='DRUIDA BOT'>DRUIDA<br>BOT</h1></div>";

  html += "<div class='container'>";
  html += "  <div class='card-title'>Config de " + getRelayName(R5name) + "</div>";
  html += "  <form action='/saveConfigR5' method='POST'>";

  // ======== Selector de MODO (siempre visible) ========
  html += "    <label for='modoR5'>MODO</label>"
          "    <select id='modoR5' name='modoR5' onchange='toggleByMode()'>"
          "      <option value='1'" + String((modoR5==1)?" selected":"") + ">Manual</option>"
          "      <option value='2'" + String((modoR5==2)?" selected":"") + ">Automático</option>"
          "      <option value='6'" + String((modoR5==6)?" selected":"") + ">Timer</option>"
          "    </select>";

  // ======== ETIQUETA (siempre visible en todos los modos) ========
  html += "    <label for='R5name'>ETIQUETA</label>"
          "    <select id='R5name' name='R5name'>"
          "      <option value='0'" + String((R5name==0)?" selected":"") + ">" + relayNames[0] + "</option>"
          "      <option value='1'" + String((R5name==1)?" selected":"") + ">" + relayNames[1] + "</option>"
          "      <option value='2'" + String((R5name==2)?" selected":"") + ">" + relayNames[2] + "</option>"
          "      <option value='3'" + String((R5name==3)?" selected":"") + ">" + relayNames[3] + "</option>"
          "      <option value='4'" + String((R5name==4)?" selected":"") + ">" + relayNames[4] + "</option>"
          "      <option value='5'" + String((R5name==5)?" selected":"") + ">" + relayNames[5] + "</option>"
          "      <option value='6'" + String((R5name==6)?" selected":"") + ">" + relayNames[6] + "</option>"
          "    </select>";

  // ======== SECCIÓN: MANUAL ========
  html += "    <div id='secManual' class='mode-sec'>"
          "      <div class='button-group-vertical'>"
          "        <button class='btn' type='button' onclick=\"location.href='/controlR5On'\">ENCENDER</button>"
          "        <button class='btn' type='button' onclick=\"location.href='/controlR5Off'\">APAGAR</button>"
          "      </div>"
          "    </div>";

  // ======== SECCIÓN: AUTO ========
  html += "    <div id='secAuto' class='mode-sec'>"
          "      <label for='paramR5'>PARÁMETRO</label>"
          "      <select id='paramR5' name='paramR5'>"
          "        <option value='1'" + String((paramR5==1)?" selected":"") + ">Humedad</option>"
          "        <option value='2'" + String((paramR5==2)?" selected":"") + ">Temperatura</option>"
          "        <option value='3'" + String((paramR5==3)?" selected":"") + ">VPD</option>"
          "      </select>"
          "      <label for='direccionR5'>OBJETIVO</label>"
          "      <select id='direccionR5' name='direccionR5'>"
          "        <option value='0'" + String((direccionR5==0)?" selected":"") + ">SUBIR</option>"
          "        <option value='1'" + String((direccionR5==1)?" selected":"") + ">BAJAR</option>"
          "      </select>"
          "      <label for='minR5'>MÍNIMO</label>"
          "      <div class='slider-row'>"
          "        <input type='range' min='0' max='100' step='0.1' id='minR5Range' value='" + String(minR5,1) + "' oninput=\"syncMinR5(this.value)\">"
          "        <input type='number' id='minR5' name='minR5' min='0' max='100' step='0.1' value='" + String(minR5,1) + "' oninput=\"syncMinR5(this.value)\">"
          "        <button type='button' class='btn-adjust' id='minR5minus' onclick=\"adjustValue('minR5',-1)\">−</button>"
          "        <button type='button' class='btn-adjust' id='minR5plus'  onclick=\"adjustValue('minR5',1)\">+</button>"
          "      </div>"
          "      <label for='maxR5'>MÁXIMO</label>"
          "      <div class='slider-row'>"
          "        <input type='range' min='0' max='100' step='0.1' id='maxR5Range' value='" + String(maxR5,1) + "' oninput=\"syncMaxR5(this.value)\">"
          "        <input type='number' id='maxR5' name='maxR5' step='0.1' value='" + String(maxR5,1) + "' oninput=\"syncMaxR5(this.value)\">"
          "        <button type='button' class='btn-adjust' id='maxR5minus' onclick=\"adjustValue('maxR5',-1)\">−</button>"
          "        <button type='button' class='btn-adjust' id='maxR5plus'  onclick=\"adjustValue('maxR5',1)\">+</button>"
          "      </div>"
          "    </div>";

  // ======== SECCIÓN: TIMER ========
  html += "    <div id='secTimer' class='mode-sec'>"
          "      <label for='horaOnR5'>HORA DE ENCENDIDO</label>"
          "      <input type='time' id='horaOnR5' name='horaOnR5' value='" + formatTwoDigits(horaOnR5) + ":" + formatTwoDigits(minOnR5) + "'>"
          "      <label for='horaOffR5'>HORA DE APAGADO</label>"
          "      <input type='time' id='horaOffR5' name='horaOffR5' value='" + formatTwoDigits(horaOffR5) + ":" + formatTwoDigits(minOffR5) + "'>"
          "    </div>";

  // ======== Botonera común ========
  html += "    <div class='button-group-vertical'>"
          "      <button class='btn' type='submit'>GUARDAR</button>"
          "      <button class='btn' type='button' onclick=\"location.href='/controlR5'\">CONTROL</button>"
          "      <button class='btn' type='button' onclick=\"location.href='/config'\">VOLVER</button>"
          "    </div>";

  html += "  </form>";
  html += "</div>";

  html += "<footer><p>druidadata@gmail.com<br>DataDruida</p></footer>";

  // ======== Scripts ========
  html += "<script>"
          "function qs(id){return document.getElementById(id)}"
          "function setDisabled(containerId,dis){var el=qs(containerId); if(!el) return; var inputs=el.querySelectorAll('input,select,button,textarea'); for(var i=0;i<inputs.length;i++){inputs[i].disabled=dis}}"
          "function hideAll(){qs('secManual').classList.add('hidden');qs('secAuto').classList.add('hidden');qs('secTimer').classList.add('hidden');setDisabled('secManual',true);setDisabled('secAuto',true);setDisabled('secTimer',true)}"
          "function toggleByMode(){var m=qs('modoR5').value; hideAll(); if(m=='1'){qs('secManual').classList.remove('hidden');setDisabled('secManual',false);} else if(m=='2'){qs('secAuto').classList.remove('hidden');setDisabled('secAuto',false);} else if(m=='6'){qs('secTimer').classList.remove('hidden');setDisabled('secTimer',false);} updateButtons();}"
          "function adjustValue(id,delta){var input=qs(id);var v=parseFloat(input.value)||0;v=(v+delta).toFixed(1);input.value=v;input.dispatchEvent(new Event('input'))}"
          "function syncMinR5(val){val=parseFloat(val);let max=parseFloat(qs('maxR5').value);if(val>max)val=max;qs('minR5').value=val.toFixed(1);qs('minR5Range').value=val;updateButtons()}"
          "function syncMaxR5(val){val=parseFloat(val);let min=parseFloat(qs('minR5').value);if(val<min)val=min;qs('maxR5').value=val.toFixed(1);qs('maxR5Range').value=val;updateButtons()}"
          "function updateButtons(){if(!qs('minR5')||!qs('maxR5')) return;let min=parseFloat(qs('minR5').value);let max=parseFloat(qs('maxR5').value);qs('minR5minus').disabled=(min<=0);qs('minR5plus').disabled=(min+0.1>max);qs('maxR5minus').disabled=(max-0.1<min);qs('maxR5plus').disabled=(max>=100);}"
          "window.onload=function(){toggleByMode()};"
          "</script>";

  html += "</body></html>";

  server.send(200, "text/html", html);
}












void saveConfigR2() {
    if (server.method() == HTTP_POST) {
        // Verificar y asignar cada parámetro recibido
        if (server.hasArg("modoR2")) {
            modoR2 = server.arg("modoR2").toInt();
        }
        if (server.hasArg("minR2")) {
            minR2 = server.arg("minR2").toFloat();
        }
        if (server.hasArg("maxR2")) {
            maxR2 = server.arg("maxR2").toFloat();
        }
        if (server.hasArg("minTR2")) {
            minTR2 = server.arg("minTR2").toFloat();
        }
        if (server.hasArg("maxTR2")) {
            maxTR2 = server.arg("maxTR2").toFloat();
        }
        if (server.hasArg("paramR2")) {
            paramR2 = server.arg("paramR2").toInt();
        }
        if (server.hasArg("estadoR2")) {
            estadoR2 = server.arg("estadoR2").toInt();
        }

        // Guardar cambios y mostrar confirmación
        handleSaveConfig();
    } else {
        // Si no es un método POST, devolver error
        server.send(405, "text/plain", "Método no permitido");
    }
}




// ⬇️ Reemplazá por completo tu handleConfigR2.
// Muestra SOLO lo que corresponde según modoR2 y paramR2 (HT=4 destapa MIN/MAX TEMP).
// Mantengo tus estilos, sliders, +/- y validaciones.

void handleConfigR2() {
  String html =
    "<!DOCTYPE html><html lang='es'><head>"
    "<meta charset='UTF-8'/>"
    "<meta name='viewport' content='width=device-width, initial-scale=1'/>"
    "<title>Config R2</title>"
    "<link href='https://fonts.googleapis.com/css2?family=Press+Start+2P&display=swap' rel='stylesheet'>"
    "<style>"
      /* ===== Tokens responsive (alineado a ConfigR5/R1) ===== */
      ":root{--radius:16px;--pad:16px;--gap:12px;--pad-lg:24px;"
      "--fs-body:clamp(16px,3.5vw,18px);--fs-small:clamp(12px,2.6vw,14px);--fs-h1:clamp(22px,6vw,56px)}"
      "*,*::before,*::after{box-sizing:border-box;-webkit-tap-highlight-color:transparent}"
      "html,body{height:100%}"

      /* BODY (scroll largo) */
      "body{margin:0;padding:10px;font-family:'Press Start 2P',monospace;"
        "background:linear-gradient(to bottom,#0a0f1e,#111927);color:#e0e0e0;"
        "min-height:100dvh;display:flex;flex-direction:column;align-items:center;justify-content:flex-start;"
        "line-height:1.25;gap:16px;position:relative}"

      /* Glow + Logo */
      ".cloud{position:absolute;top:-40px;left:50%;transform:translateX(-50%);width:300px;height:150px;"
        "background:radial-gradient(ellipse at center,rgba(0,240,255,.2),transparent 70%);border-radius:50%;"
        "filter:blur(40px);animation:pulse 6s ease-in-out infinite;z-index:-1}"
      "@keyframes pulse{0%,100%{transform:translateX(-50%) scale(1);opacity:.3}50%{transform:translateX(-50%) scale(1.1);opacity:.5}}"

      ".logo-container{margin:0 0 4px 0;position:relative;text-align:center;animation:fadeIn 1.2s ease-out}"
      ".logo-text{font-size:var(--fs-h1);font-weight:bold;line-height:1.15;color:#00f0ff;"
        "text-shadow:0 0 12px #00f0ff,0 0 28px #00f0ff;animation:glow 3s infinite alternate}"
      "@keyframes glow{from{text-shadow:0 0 8px #00f0ff,0 0 18px #00f0ff}to{text-shadow:0 0 18px #00f0ff,0 0 36px #00f0ff}}"

      ".glitch{position:relative;display:inline-block}"
      ".glitch::before,.glitch::after{content:attr(data-text);position:absolute;left:0;top:0;width:100%;overflow:hidden;color:#00f0ff;background:transparent;clip:rect(0,0,0,0)}"
      ".glitch::before{text-shadow:2px 0 red;animation:glitchTop 2s infinite linear alternate-reverse}"
      ".glitch::after{text-shadow:-2px 0 blue;animation:glitchBottom 2s infinite linear alternate-reverse}"
      "@keyframes glitchTop{0%{clip:rect(0,9999px,0,0)}5%{clip:rect(0,9999px,20px,0);transform:translate(-2px,-2px)}10%{clip:rect(0,9999px,10px,0);transform:translate(2px,0)}15%{clip:rect(0,9999px,5px,0);transform:translate(-1px,1px)}20%,100%{clip:rect(0,0,0,0);transform:none}}"
      "@keyframes glitchBottom{0%{clip:rect(0,0,0,0)}5%{clip:rect(20px,9999px,40px,0);transform:translate(1px,1px)}10%{clip:rect(10px,9999px,30px,0);transform:translate(-1px,-1px)}15%{clip:rect(5px,9999px,25px,0);transform:translate(1px,-1px)}20%,100%{clip:rect(0,0,0,0);transform:none}}"

      /* Card contenedor */
      ".container{margin-top:0;background-color:#1e293b;border:1px solid #00f0ff;border-radius:var(--radius);"
        "padding:var(--pad-lg);width:min(92vw,560px);max-width:560px;box-shadow:0 0 20px rgba(0,240,255,.2);"
        "animation:fadeInUp .9s ease-out;text-align:center;position:relative;display:flex;flex-direction:column;gap:var(--gap)}"
      ".card-title{font-size:var(--fs-body);margin:4px 0 6px;color:#00f0ff}"

      /* Labels / campos centrados */
      "label{font-size:var(--fs-body);width:100%;color:#e0e0e0;text-align:center;display:block;margin-top:10px;margin-bottom:6px}"
      "select,input[type='time'],input[type='number'],input[type='text']{font-family:'Press Start 2P',monospace;font-size:var(--fs-body);"
        "width:100%;padding:10px 12px;border-radius:10px;background:#0f172a;color:#e0e0e0;border:2px solid #00f0ff;text-align:center;box-sizing:border-box}"

      /* Sliders (formato ConfigR5) */
      ".slider-row{display:flex;align-items:center;justify-content:center;width:100%;gap:10px}"
      ".slider-row input[type='range']{flex:1}"
      ".slider-row .btn-adjust{min-width:44px}"
      ".btn-adjust{font-family:'Press Start 2P',monospace;font-size:var(--fs-body);background:#0f172a;color:#e0e0e0;"
        "border:2px solid #00f0ff;border-radius:10px;padding:10px 12px;cursor:pointer;text-align:center;transition:.2s;box-shadow:0 0 6px rgba(0,240,255,.3)}"
      ".btn-adjust:hover{background-color:#112031}"

      /* Botones (como R5/R1) */
      ".button-group-vertical{display:flex;flex-direction:column;gap:10px;justify-content:center;margin-top:10px;width:100%}"
      ".btn,button{background-color:#00f0ff;color:#0f172a;border:none;padding:12px 14px;font-size:var(--fs-body);font-weight:bold;"
        "cursor:pointer;border-radius:10px;transition:background-color .2s ease,transform .15s ease;font-family:'Press Start 2P',monospace;min-height:48px;width:100%}"
      ".btn:hover,button:hover{background-color:#00c0dd;transform:translateY(-1px)}"
      ".hidden{display:none!important}"

      /* Footer no fijo */
      "footer{margin-top:16px;text-align:center;font-size:var(--fs-small);color:#8aa;opacity:.9;padding:12px}"
      "footer p{margin:0}"

      /* Animaciones */
      "@keyframes fadeInUp{from{opacity:0;transform:translateY(40px)}to{opacity:1;transform:translateY(0)}}"
    "</style></head>";

  html += "<body>";
  html += "<div class='cloud'></div>";
  html += "<div class='logo-container'><h1 class='logo-text glitch' data-text='DRUIDA BOT'>DRUIDA<br>BOT</h1></div>";

  html += "<div class='container'>";
  html += "  <div class='card-title'>Config de " + getRelayName(R2name) + "</div>";
  html += "  <form action='/saveConfigR2' method='POST' id='formConfigR2'>";

  /* ===== MODO (siempre visible) ===== */
  html +=
    "    <label for='modoR2'>MODO</label>"
    "    <select id='modoR2' name='modoR2' onchange='toggleByMode()'>"
    "      <option value='1'" + String((modoR2==1) ? " selected" : "") + ">Manual</option>"
    "      <option value='2'" + String((modoR2==2) ? " selected" : "") + ">Automático</option>"
    "      <option value='9'" + String((modoR2==9) ? " selected" : "") + ">Automático Inteligente</option>"
    "    </select>";

  /* ===== SECCIÓN: MANUAL ===== */
  html +=
    "    <div id='secManual' class='mode-sec'>"
    "      <div class='button-group-vertical'>"
    "        <button class='btn' type='button' onclick='location.href=\"/controlR2On\"'>ENCENDER</button>"
    "        <button class='btn' type='button' onclick='location.href=\"/controlR2Off\"'>APAGAR</button>"
    "      </div>"
    "    </div>";

  /* ===== SECCIÓN: AUTO / AUTO INT (comparten UI) ===== */
  html +=
    "    <div id='secAuto' class='mode-sec'>"
    "      <label for='paramR2'>PARÁMETRO</label>"
    "      <select id='paramR2' name='paramR2' onchange='toggleParam()'>"
    "        <option value='1'" + String((paramR2==1) ? " selected" : "") + ">Humedad</option>"
    "        <option value='2'" + String((paramR2==2) ? " selected" : "") + ">Temperatura</option>"
    "        <option value='3'" + String((paramR2==3) ? " selected" : "") + ">VPD</option>"
    "        <option value='4'" + String((paramR2==4) ? " selected" : "") + ">H + T</option>"
    "      </select>"

    "      <label for='minR2'>MÍNIMO</label>"
    "      <div class='slider-row'>"
    "        <input type='range' min='0' max='100' step='0.1' id='minR2Range' value='" + String(minR2,1) + "' oninput='syncMinR2(this.value)'>"
    "        <input type='number' id='minR2' name='minR2' min='0' max='100' step='0.1' value='" + String(minR2,1) + "' oninput='syncMinR2(this.value)'>"
    "        <button type='button' class='btn-adjust' id='minR2minus' onclick='adjustValue(\"minR2\",-1)'>−</button>"
    "        <button type='button' class='btn-adjust' id='minR2plus'  onclick='adjustValue(\"minR2\",1)'>+</button>"
    "      </div>"

    "      <label for='maxR2'>MÁXIMO</label>"
    "      <div class='slider-row'>"
    "        <input type='range' min='0' max='100' step='0.1' id='maxR2Range' value='" + String(maxR2,1) + "' oninput='syncMaxR2(this.value)'>"
    "        <input type='number' id='maxR2' name='maxR2' step='0.1' value='" + String(maxR2,1) + "' oninput='syncMaxR2(this.value)'>"
    "        <button type='button' class='btn-adjust' id='maxR2minus' onclick='adjustValue(\"maxR2\",-1)'>−</button>"
    "        <button type='button' class='btn-adjust' id='maxR2plus'  onclick='adjustValue(\"maxR2\",1)'>+</button>"
    "      </div>"

    /* --- Bloque extra para H+T: límites de Temperatura --- */
    "      <div id='blkHT' class='hidden'>"
    "        <label for='minTR2'>MIN TEMP</label>"
    "        <div class='slider-row'>"
    "          <input type='range' min='0' max='50' step='0.1' id='minTR2Range' value='" + String(minTR2,1) + "' oninput='syncMinTR2(this.value)'>"
    "          <input type='number' id='minTR2' name='minTR2' min='0' max='50' step='0.1' value='" + String(minTR2,1) + "' oninput='syncMinTR2(this.value)'>"
    "          <button type='button' class='btn-adjust' id='minTR2minus' onclick='adjustValue(\"minTR2\",-1)'>−</button>"
    "          <button type='button' class='btn-adjust' id='minTR2plus'  onclick='adjustValue(\"minTR2\",1)'>+</button>"
    "        </div>"

    "        <label for='maxTR2'>MAX TEMP</label>"
    "        <div class='slider-row'>"
    "          <input type='range' min='0' max='50' step='0.1' id='maxTR2Range' value='" + String(maxTR2,1) + "' oninput='syncMaxTR2(this.value)'>"
    "          <input type='number' id='maxTR2' name='maxTR2' min='0' max='50' step='0.1' value='" + String(maxTR2,1) + "' oninput='syncMaxTR2(this.value)'>"
    "          <button type='button' class='btn-adjust' id='maxTR2minus' onclick='adjustValue(\"maxTR2\",-1)'>−</button>"
    "          <button type='button' class='btn-adjust' id='maxTR2plus'  onclick='adjustValue(\"maxTR2\",1)'>+</button>"
    "        </div>"
    "      </div>"
    "    </div>";

  /* ===== Botonera común ===== */
  html +=
    "    <div class='button-group-vertical'>"
    "      <button class='btn' type='submit'>GUARDAR</button>"
    "      <button class='btn' type='button' onclick='location.href=\"/controlR2\"'>CONTROL</button>"
    "      <button class='btn' type='button' onclick='location.href=\"/config\"'>VOLVER</button>"
    "    </div>";

  html += "  </form>";
  html += "</div>";

  html += "<footer><p>druidadata@gmail.com<br>DataDruida</p></footer>";

  /* ===== Scripts ===== */
  html +=
    "<script>"
    "function qs(id){return document.getElementById(id)}"
    "function setDisabled(containerId,dis){var el=qs(containerId); if(!el) return; var inputs=el.querySelectorAll('input,select,button,textarea'); for(var i=0;i<inputs.length;i++){inputs[i].disabled=dis}}"
    "function hideAll(){qs('secManual').classList.add('hidden');qs('secAuto').classList.add('hidden');setDisabled('secManual',true);setDisabled('secAuto',true)}"
    "function toggleByMode(){var m=qs('modoR2').value; hideAll(); if(m=='1'){qs('secManual').classList.remove('hidden');setDisabled('secManual',false);} else if(m=='2' || m=='9'){qs('secAuto').classList.remove('hidden');setDisabled('secAuto',false);} toggleParam(); updateButtons();}"

    /* ---- Ajustes +/− genéricos ---- */
    "function adjustValue(id,delta){"
      "var input=qs(id); if(!input) return;"
      "var v=parseFloat(input.value)||0;"
      "var isTemp=id.indexOf('TR2')>-1; var limit=isTemp?50:100;"
      "var isMin=id.startsWith('min'); var pairId=isMin?id.replace('min','max'):id.replace('max','min'); var pair=qs(pairId);"
      "var pairVal=parseFloat(pair?pair.value:(isMin?limit:0));"
      "v=(v+delta*0.1); v=parseFloat(v.toFixed(1));"
      "if(isMin && v>pairVal) v=pairVal; if(!isMin && v<pairVal) v=pairVal;"
      "if(v<0) v=0; if(v>limit) v=limit;"
      "input.value=v.toFixed(1); var r=qs(id+'Range'); if(r) r.value=v.toFixed(1);"
      "updateButtons();"
    "}"

    /* ---- Sync de pares ---- */
    "function syncMinR2(val){val=parseFloat(val);var max=parseFloat(qs('maxR2').value);if(val>max)val=max;qs('minR2').value=val.toFixed(1);qs('minR2Range').value=val;updateButtons()}"
    "function syncMaxR2(val){val=parseFloat(val);var min=parseFloat(qs('minR2').value);if(val<min)val=min;qs('maxR2').value=val.toFixed(1);qs('maxR2Range').value=val;updateButtons()}"
    "function syncMinTR2(val){val=parseFloat(val);var max=parseFloat(qs('maxTR2').value);if(val>max)val=max;qs('minTR2').value=val.toFixed(1);qs('minTR2Range').value=val;updateButtons()}"
    "function syncMaxTR2(val){val=parseFloat(val);var min=parseFloat(qs('minTR2').value);if(val<min)val=min;qs('maxTR2').value=val.toFixed(1);qs('maxTR2Range').value=val;updateButtons()}"

    /* ---- Habilitar/Deshabilitar +/− según límites ---- */
    "function setPairState(minId,maxId,minLimit,maxLimit){"
      "var min=parseFloat(qs(minId).value), max=parseFloat(qs(maxId).value);"
      "var mn=qs(minId+'minus'), mp=qs(minId+'plus'), xn=qs(maxId+'minus'), xp=qs(maxId+'plus');"
      "if(mn) mn.disabled=(min<=minLimit);"
      "if(mp) mp.disabled=(min+0.1>max);"
      "if(xn) xn.disabled=(max-0.1<min);"
      "if(xp) xp.disabled=(max>=maxLimit);"
    "}"
    "function updateButtons(){"
      "if(qs('minR2')&&qs('maxR2')) setPairState('minR2','maxR2',0,100);"
      "if(!qs('blkHT').classList.contains('hidden') && qs('minTR2')&&qs('maxTR2')) setPairState('minTR2','maxTR2',0,50);"
    "}"

    /* ---- Mostrar por parámetro ---- */
    "function toggleParam(){var p=qs('paramR2').value; var ht=qs('blkHT'); if(p=='4'){ht.classList.remove('hidden');}else{ht.classList.add('hidden');} updateButtons()}"

    /* ---- Init + Validaciones ---- */
    "document.addEventListener('DOMContentLoaded',function(){"
      "toggleByMode();"
      "updateButtons();"
      "qs('formConfigR2').addEventListener('submit',function(e){"
        "var minG=parseFloat(qs('minR2').value), maxG=parseFloat(qs('maxR2').value);"
        "if(minG>maxG){alert('MÍNIMO no puede ser mayor que MÁXIMO.'); e.preventDefault(); return;}"
        "if(qs('paramR2').value=='4'){var minT=parseFloat(qs('minTR2').value), maxT=parseFloat(qs('maxTR2').value); if(minT>maxT){alert('MIN TEMP no puede ser mayor que MAX TEMP.'); e.preventDefault(); return;}}"
      "});"
    "});"
    "</script>";

  html += "</body></html>";

  server.send(200, "text/html", html);
}







void handleConfigR3() {
  // Preparar valores mostrados para Pulso/Pausa según unidad conveniente
  int tiempoRiegoSegundos = tiempoRiego;
  int tiempoNoRiegoSegundos = tiempoNoRiego;

  int unidadRiego = 1;
  if (tiempoRiego % 3600 == 0)      unidadRiego = 3600;
  else if (tiempoRiego % 60 == 0)   unidadRiego = 60;

  int unidadNoRiego = 1;
  if (tiempoNoRiego % 3600 == 0)      unidadNoRiego = 3600;
  else if (tiempoNoRiego % 60 == 0)   unidadNoRiego = 60;

  int tiempoRiegoMostrar   = tiempoRiegoSegundos   / unidadRiego;
  int tiempoNoRiegoMostrar = tiempoNoRiegoSegundos / unidadNoRiego;

  String html =
    "<!DOCTYPE html><html lang='es'><head>"
    "<meta charset='UTF-8'/>"
    "<meta name='viewport' content='width=device-width, initial-scale=1'/>"
    "<title>Config R3</title>"
    "<link href='https://fonts.googleapis.com/css2?family=Press+Start+2P&display=swap' rel='stylesheet'>"
    "<style>"
      /* ===== Tokens y base (alineado a las demás páginas) ===== */
      ":root{--radius:16px;--pad:16px;--gap:12px;--pad-lg:24px;"
      "--fs-body:clamp(16px,3.5vw,18px);--fs-small:clamp(12px,2.6vw,14px);--fs-h1:clamp(22px,6vw,56px)}"
      "*,*::before,*::after{box-sizing:border-box;-webkit-tap-highlight-color:transparent}"
      "html,body{height:100%}"
      "body{margin:0;padding:10px;font-family:'Press Start 2P',monospace;background:linear-gradient(to bottom,#0a0f1e,#111927);"
        "color:#e0e0e0;min-height:100dvh;display:flex;flex-direction:column;align-items:center;justify-content:flex-start;"
        "line-height:1.25;gap:16px;position:relative}"

      /* Glow + Logo */
      ".cloud{position:absolute;top:-40px;left:50%;transform:translateX(-50%);width:300px;height:150px;"
        "background:radial-gradient(ellipse at center,rgba(0,240,255,.2),transparent 70%);border-radius:50%;filter:blur(40px);"
        "animation:pulse 6s ease-in-out infinite;z-index:-1}"
      "@keyframes pulse{0%,100%{transform:translateX(-50%) scale(1);opacity:.3}50%{transform:translateX(-50%) scale(1.1);opacity:.5}}"
      ".logo-container{margin:0 0 4px 0;position:relative;text-align:center;animation:fadeIn 1.2s ease-out}"
      ".logo-text{font-size:var(--fs-h1);font-weight:bold;line-height:1.15;color:#00f0ff;text-shadow:0 0 12px #00f0ff,0 0 28px #00f0ff;animation:glow 3s infinite alternate}"
      "@keyframes glow{from{text-shadow:0 0 8px #00f0ff,0 0 18px #00f0ff}to{text-shadow:0 0 18px #00f0ff,0 0 36px #00f0ff}}"
      ".glitch{position:relative;display:inline-block}"
      ".glitch::before,.glitch::after{content:attr(data-text);position:absolute;left:0;top:0;width:100%;overflow:hidden;color:#00f0ff;background:transparent;clip:rect(0,0,0,0)}"
      ".glitch::before{text-shadow:2px 0 red;animation:glitchTop 2s infinite linear alternate-reverse}"
      ".glitch::after{text-shadow:-2px 0 blue;animation:glitchBottom 2s infinite linear alternate-reverse}"
      "@keyframes glitchTop{0%{clip:rect(0,9999px,0,0)}5%{clip:rect(0,9999px,20px,0);transform:translate(-2px,-2px)}10%{clip:rect(0,9999px,10px,0);transform:translate(2px,0)}15%{clip:rect(0,9999px,5px,0);transform:translate(-1px,1px)}20%,100%{clip:rect(0,0,0,0);transform:none}}"
      "@keyframes glitchBottom{0%{clip:rect(0,0,0,0)}5%{clip:rect(20px,9999px,40px,0);transform:translate(1px,1px)}10%{clip:rect(10px,9999px,30px,0);transform:translate(-1px,-1px)}15%{clip:rect(5px,9999px,25px,0);transform:translate(1px,-1px)}20%,100%{clip:rect(0,0,0,0);transform:none}}"

      /* Card contenedor */
      ".container{margin-top:0;background:#1e293b;border:1px solid #00f0ff;border-radius:var(--radius);padding:var(--pad-lg);"
        "width:min(92vw,560px);max-width:560px;box-shadow:0 0 20px rgba(0,240,255,.2);animation:fadeInUp .9s ease-out;"
        "text-align:center;display:flex;flex-direction:column;gap:var(--gap)}"
      ".card-title{font-size:var(--fs-body);margin:4px 0 6px;color:#00f0ff}"

      /* Formularios centrados */
      "label{font-size:var(--fs-body);width:100%;color:#e0e0e0;text-align:center;display:block;margin-top:10px;margin-bottom:6px}"
      "select,input[type='time'],input[type='number'],input[type='text']{font-family:'Press Start 2P',monospace;font-size:var(--fs-body);"
        "width:100%;padding:10px 12px;border-radius:10px;background:#0f172a;color:#e0e0e0;border:2px solid #00f0ff;text-align:center;box-sizing:border-box}"

      /* Filas de inputs (pulso/pausa/ciclos) */
      ".input-row{display:flex;gap:10px;align-items:center;width:100%}"
      ".input-row input,.input-row select{flex:1}"

      /* Días de riego */
      ".dias-title{font-size:var(--fs-body);color:#00f0ff;margin-top:8px}"
      ".dias-grid{display:grid;grid-template-columns:repeat(7,1fr);gap:6px;justify-items:center;align-items:center}"
      ".dias-grid span{font-size:var(--fs-small);color:#00f0ff;opacity:.9}"
      ".dias-grid input[type='checkbox']{transform:scale(1.1);accent-color:#00f0ff;cursor:pointer}"

      /* Botonera */
      ".button-group-vertical{display:flex;flex-direction:column;gap:10px;justify-content:center;margin-top:10px;width:100%}"
      ".btn,button{background-color:#00f0ff;color:#0f172a;border:none;padding:12px 14px;font-size:var(--fs-body);font-weight:bold;"
        "cursor:pointer;border-radius:10px;transition:background-color .2s ease,transform .15s ease;font-family:'Press Start 2P',monospace;min-height:48px;width:100%}"
      ".btn:hover,button:hover{background-color:#00c0dd;transform:translateY(-1px)}"

      /* Footer */
      "footer{margin-top:16px;text-align:center;font-size:var(--fs-small);color:#8aa;opacity:.9;padding:12px}"
      "footer p{margin:0}"

      /* Animaciones */
      "@keyframes fadeInUp{from{opacity:0;transform:translateY(40px)}to{opacity:1;transform:translateY(0)}}"
    "</style></head>";

  html += "<body>";
  html += "<div class='cloud'></div>";
  html += "<div class='logo-container'><h1 class='logo-text glitch' data-text='DRUIDA BOT'>DRUIDA<br>BOT</h1></div>";

  html += "<div class='container'>";
  html += "  <div class='card-title'>Config de " + getRelayName(R3name) + "</div>";
  html += "  <form action='/saveConfigR3' method='POST' id='formConfigR3'>";

  html += "    <label for='modoR3'>MODO</label>";
  html += "    <select id='modoR3' name='modoR3'>"
          "<option value='1'" + String((modoR3==1) ? " selected" : "") + ">Manual</option>"
          "<option value='2'" + String((modoR3==2) ? " selected" : "") + ">Automático</option>"
          "</select>";

  html += "    <label for='horaOnR3'>HORA ENCENDIDO</label>";
  html += "    <input type='time' id='horaOnR3' name='horaOnR3' value='" + formatTwoDigits(horaOnR3) + ":" + formatTwoDigits(minOnR3) + "'>";

  html += "    <label for='horaOffR3'>HORA APAGADO</label>";
  html += "    <input type='time' id='horaOffR3' name='horaOffR3' readonly style='pointer-events:none;background:#222;color:#ccc'>";

  html += "    <label for='tiempoRiego'>PULSO</label>";
  html += "    <div class='input-row'>"
            "<input type='number' id='tiempoRiego' name='tiempoRiego' min='0' step='1' value='" + String(tiempoRiegoMostrar) + "'>"
            "<select id='unidadRiego' name='unidadRiego'>"
              "<option value='1'"     + String((unidadRiego==1)     ? " selected" : "") + ">seg</option>"
              "<option value='60'"    + String((unidadRiego==60)    ? " selected" : "") + ">min</option>"
              "<option value='3600'"  + String((unidadRiego==3600)  ? " selected" : "") + ">h</option>"
            "</select>"
          "</div>";

  html += "    <label for='tiempoNoRiego'>PAUSA</label>";
  html += "    <div class='input-row'>"
            "<input type='number' id='tiempoNoRiego' name='tiempoNoRiego' min='0' step='1' value='" + String(tiempoNoRiegoMostrar) + "'>"
            "<select id='unidadNoRiego' name='unidadNoRiego'>"
              "<option value='1'"     + String((unidadNoRiego==1)     ? " selected" : "") + ">seg</option>"
              "<option value='60'"    + String((unidadNoRiego==60)    ? " selected" : "") + ">min</option>"
              "<option value='3600'"  + String((unidadNoRiego==3600)  ? " selected" : "") + ">h</option>"
            "</select>"
          "</div>";

  html += "    <label for='cantidad'>CICLOS</label>";
  html += "    <div class='input-row'>"
            "<input type='number' id='cantidad' name='cantidad' min='1' step='1' value='" + String(cantidadRiegos) + "'>"
            "<select id='unidadCantidad' name='unidadCantidad'>"
              "<option value='dia' selected>Pulsos/Día</option>"
            "</select>"
          "</div>";

  html += "    <div class='dias-title'>DÍAS DE RIEGO</div>";
  html += "    <div class='dias-grid'>"
            "<span>D</span><span>L</span><span>M</span><span>M</span><span>J</span><span>V</span><span>S</span>";
  for (int i = 0; i < 7; i++) {
    html += "<input type='checkbox' name='diaRiego" + String(i) + "'" + String(diasRiego[i] ? " checked" : "") + ">";
  }
  html += "    </div>";

  html += "    <div class='button-group-vertical'>"
            "<button type='submit' class='btn'>GUARDAR</button>"
            "<button type='button' class='btn' onclick='window.location.href=\"/controlR3\"'>CONTROL</button>"
            "<button type='button' class='btn' onclick='window.location.href=\"/config\"'>VOLVER</button>"
          "</div>";

  html += "  </form>";
  html += "</div>";

  html += "<footer><p>druidadata@gmail.com<br>DataDruida</p></footer>";

  /* ===== Scripts ===== */
  html += "<script>"
          "let bloqueado=false;"

          "function setupConversion(inputId,selectId,onValueChange){"
            "const input=document.getElementById(inputId);"
            "const select=document.getElementById(selectId);"
            "let lastUnit=parseInt(select.value);"
            "select.addEventListener('change',()=>{"
              "const newUnit=parseInt(select.value);"
              "const currentVal=parseFloat(input.value)||0;"
              "const converted=currentVal*lastUnit/newUnit;"
              "input.value=Math.round(converted*100)/100;"
              "lastUnit=newUnit;"
              "if(typeof onValueChange==='function') onValueChange();"
            "});"
            "input.addEventListener('input',()=>{if(typeof onValueChange==='function') onValueChange();});"
          "}"

          "function getSecondsDesdeHora(hora){if(!hora||hora.indexOf(':')<0) return 0; const p=hora.split(':'); const h=parseInt(p[0]); const m=parseInt(p[1]); return (isNaN(h)||isNaN(m))?0:h*3600+m*60;}"

          "function setHoraDesdeSegundos(id,total){let h=Math.floor(total/3600)%24; let m=Math.floor((total%3600)/60); let s=total%60; if(s>0){m+=1;if(m>=60){m=0;h=(h+1)%24;}} document.getElementById(id).value=(('0'+h).slice(-2))+':'+'0'.concat(m).slice(-2);}"

          "function calcularCantidad(){"
            "if(bloqueado) return; bloqueado=true;"
            "const inicio=getSecondsDesdeHora(document.getElementById('horaOnR3').value);"
            "const fin=getSecondsDesdeHora(document.getElementById('horaOffR3').value);"
            "const tR=(parseFloat(document.getElementById('tiempoRiego').value)||0)*parseInt(document.getElementById('unidadRiego').value);"
            "const tNR=(parseFloat(document.getElementById('tiempoNoRiego').value)||0)*parseInt(document.getElementById('unidadNoRiego').value);"
            "if(tR<=0){document.getElementById('cantidad').value=1; bloqueado=false; return;}"
            "const dur=(fin>=inicio)?(fin-inicio):(86400-inicio+fin);"
            "const cant=Math.max(1,Math.floor((dur+tNR)/(tR+tNR)));"
            "document.getElementById('cantidad').value=cant; bloqueado=false;"
          "}"

          "function actualizarHoraOffDesdeCantidad(){"
            "if(bloqueado) return; bloqueado=true;"
            "const inicio=getSecondsDesdeHora(document.getElementById('horaOnR3').value);"
            "const cant=parseInt(document.getElementById('cantidad').value)||1;"
            "const tR=(parseFloat(document.getElementById('tiempoRiego').value)||0)*parseInt(document.getElementById('unidadRiego').value);"
            "const tNR=(parseFloat(document.getElementById('tiempoNoRiego').value)||0)*parseInt(document.getElementById('unidadNoRiego').value);"
            "const total=(cant*tR)+((cant-1)*tNR);"
            "const nuevoFin=(inicio+total)%86400;"
            "setHoraDesdeSegundos('horaOffR3',nuevoFin); bloqueado=false;"
          "}"

          "document.getElementById('cantidad').addEventListener('input',actualizarHoraOffDesdeCantidad);"
          "document.getElementById('horaOnR3').addEventListener('input',calcularCantidad);"
          "document.getElementById('horaOffR3').addEventListener('input',calcularCantidad);"
          "document.getElementById('tiempoRiego').addEventListener('input',actualizarHoraOffDesdeCantidad);"
          "document.getElementById('unidadRiego').addEventListener('change',actualizarHoraOffDesdeCantidad);"
          "document.getElementById('tiempoNoRiego').addEventListener('input',actualizarHoraOffDesdeCantidad);"
          "document.getElementById('unidadNoRiego').addEventListener('change',actualizarHoraOffDesdeCantidad);"

          "setupConversion('tiempoRiego','unidadRiego',actualizarHoraOffDesdeCantidad);"
          "setupConversion('tiempoNoRiego','unidadNoRiego',actualizarHoraOffDesdeCantidad);"

          "window.addEventListener('load',()=>{actualizarHoraOffDesdeCantidad();});"
          "</script>";

  html += "</body></html>";
  server.send(200, "text/html", html);
}





void saveConfigR3() {
    if (server.method() == HTTP_POST) {

        if (server.hasArg("modoR3")) {
            modoR3 = server.arg("modoR3").toInt();
        }

        if (server.hasArg("horaOnR3")) {
            String horaOn = server.arg("horaOnR3");
            int sepIndex = horaOn.indexOf(':');
            if (sepIndex != -1) {
                horaOnR3 = horaOn.substring(0, sepIndex).toInt();
                minOnR3 = horaOn.substring(sepIndex + 1).toInt();
            }
        }

        if (server.hasArg("horaOffR3")) {
            String horaOff = server.arg("horaOffR3");
            int sepIndex = horaOff.indexOf(':');
            if (sepIndex != -1) {
                horaOffR3 = horaOff.substring(0, sepIndex).toInt();
                minOffR3 = horaOff.substring(sepIndex + 1).toInt();
            }
        }

        // Valores por defecto
        unidadRiego = server.hasArg("unidadRiego") ? server.arg("unidadRiego").toInt() : 1;
        unidadNoRiego = server.hasArg("unidadNoRiego") ? server.arg("unidadNoRiego").toInt() : 1;

        tiempoRiego = server.hasArg("tiempoRiego") ? round(server.arg("tiempoRiego").toFloat() * unidadRiego) : 0;
        tiempoNoRiego = server.hasArg("tiempoNoRiego") ? round(server.arg("tiempoNoRiego").toFloat() * unidadNoRiego) : 0;

        int cicloTotal = tiempoRiego + tiempoNoRiego;
        int maxCantidad = (cicloTotal > 0) ? 86400 / cicloTotal : 1;

        if (server.hasArg("cantidad")) {
            cantidadRiegos = server.arg("cantidad").toInt();
        } else {
            int segundosOn = horaOnR3 * 3600 + minOnR3 * 60;
            int segundosOff = horaOffR3 * 3600 + minOffR3 * 60;
            int duracion = segundosOff - segundosOn;
            if (duracion < 0) duracion += 86400; // ajuste si pasa medianoche

            cantidadRiegos = (cicloTotal > 0) ? duracion / cicloTotal : 1;
        }

        if (cantidadRiegos < 1) cantidadRiegos = 1;
        if (cantidadRiegos > maxCantidad) cantidadRiegos = maxCantidad;

        if (server.hasArg("estadoR3")) {
            estadoR3 = server.arg("estadoR3").toInt();
        }

        for (int i = 0; i < 7; i++) diasRiego[i] = 0;
        for (int i = 0; i < 7; i++) {
            String paramName = "diaRiego" + String(i);
            if (server.hasArg(paramName)) {
                diasRiego[i] = 1;
            }
        }

        // Guardado general
        handleSaveConfig();

    } else {
        server.send(405, "text/plain", "Método no permitido");
    }
}






void handleConfigR4() {
  // ------ Estado actual del relé (por si querés usarlo luego) ------
  int pinLevel = digitalRead(RELAY4);
  bool isOn = RELAY4_ACTIVE_LOW ? (pinLevel == LOW) : (pinLevel == HIGH);

  // ------ Valores base visibles ------
  int showOnM  = (horaOnR4  * 60 + minOnR4 ) % 1440;
  int showOffM = (horaOffR4 * 60 + minOffR4) % 1440;

  // ------ Vista SUPERCICLO (deriva OFF = ON + horasLuz) ------
  if (modoR4 == SUPERCICLO || modoR4 == SUPERCICLO1313) {
    int Lm = (modoR4 == SUPERCICLO1313) ? (13 * 60) : (int)horasLuz;
    if (Lm <= 0) Lm = 1;

    int onAbs  = (horaOnR4 * 60 + minOnR4) % 1440;
    int offAbs = (onAbs + Lm) % 1440;

    showOnM  = onAbs;
    showOffM = offAbs;
  }

  // ------ HTML ------
  String html;
  html  = "<!DOCTYPE html><html lang='es'><head><meta charset='UTF-8'/>"
          "<meta name='viewport' content='width=device-width, initial-scale=1, viewport-fit=cover'/>"
          "<title>Config R4</title>"
          "<link href='https://fonts.googleapis.com/css2?family=Press+Start+2P&display=swap' rel='stylesheet'>"
          "<style>"
          ":root{--radius:16px;--pad:16px;--gap:12px;--pad-lg:24px;"
          "--fs-body:clamp(16px,3.5vw,18px);--fs-small:clamp(12px,2.6vw,14px);--fs-h1:clamp(22px,6vw,56px)}"
          "*,*::before,*::after{box-sizing:border-box;-webkit-tap-highlight-color:transparent}"
          "html,body{height:100%}"
          "body{margin:0;padding:10px;font-family:'Press Start 2P',monospace;"
            "background:linear-gradient(to bottom,#0a0f1e,#111927);color:#e0e0e0;"
            "min-height:100dvh;display:flex;flex-direction:column;align-items:center;justify-content:flex-start;"
            "line-height:1.25;gap:16px;position:relative}"
          ".cloud{position:absolute;top:-40px;left:50%;transform:translateX(-50%);width:300px;height:150px;"
            "background:radial-gradient(ellipse at center,rgba(0,240,255,.2),transparent 70%);border-radius:50%;filter:blur(40px);"
            "animation:pulse 6s ease-in-out infinite;z-index:-1}"
          "@keyframes pulse{0%,100%{transform:translateX(-50%) scale(1);opacity:.3}"
            "50%{transform:translateX(-50%) scale(1.1);opacity:.5}}"
          ".logo-container{margin:0 0 4px 0;position:relative;text-align:center;animation:fadeIn 1.2s ease-out}"
          ".logo-text{font-size:var(--fs-h1);font-weight:bold;line-height:1.15;color:#00f0ff;"
            "text-shadow:0 0 12px #00f0ff,0 0 28px #00f0ff;animation:glow 3s infinite alternate}"
          "@keyframes glow{from{text-shadow:0 0 8px #00f0ff,0 0 18px #00f0ff}"
            "to{text-shadow:0 0 18px #00f0ff,0 0 36px #00f0ff}}"
          ".glitch{position:relative;display:inline-block}"
          ".glitch::before,.glitch::after{content:attr(data-text);position:absolute;left:0;top:0;width:100%;overflow:hidden;"
            "color:#00f0ff;background:transparent;clip:rect(0,0,0,0)}"
          ".glitch::before{text-shadow:2px 0 red;animation:glitchTop 2s infinite linear alternate-reverse}"
          ".glitch::after{text-shadow:-2px 0 blue;animation:glitchBottom 2s infinite linear alternate-reverse}"
          "@keyframes glitchTop{0%{clip:rect(0,9999px,0,0)}5%{clip:rect(0,9999px,20px,0);transform:translate(-2px,-2px)}"
            "10%{clip:rect(0,9999px,10px,0);transform:translate(2px,0)}15%{clip:rect(0,9999px,5px,0);transform:translate(-1px,1px)}"
            "20%,100%{clip:rect(0,0,0,0);transform:none}}"
          "@keyframes glitchBottom{0%{clip:rect(0,0,0,0)}5%{clip:rect(20px,9999px,40px,0);transform:translate(1px,1px)}"
            "10%{clip:rect(10px,9999px,30px,0);transform:translate(-1px,-1px)}15%{clip:rect(5px,9999px,25px,0);transform:translate(1px,-1px)}"
            "20%,100%{clip:rect(0,0,0,0);transform:none}}"
          ".container{margin-top:0;background:#1e293b;border:1px solid #00f0ff;border-radius:var(--radius);padding:var(--pad-lg);"
            "width:min(92vw,560px);max-width:560px;box-shadow:0 0 20px rgba(0,240,255,.2);animation:fadeInUp .9s ease-out;"
            "text-align:center;display:flex;flex-direction:column;gap:var(--gap)}"
          ".card-title{font-size:var(--fs-body);margin:4px 0 6px;color:#00f0ff}"
          "form{display:flex;flex-direction:column;gap:10px}"
          "label{font-size:var(--fs-body);width:100%;color:#e0e0e0;text-align:center;display:block;margin-top:10px;margin-bottom:6px}"
          "select,input[type='time'],input[type='number']{font-family:'Press Start 2P',monospace;font-size:var(--fs-body);"
            "width:100%;padding:10px 12px;border-radius:10px;background:#0f172a;color:#e0e0e0;border:2px solid #00f0ff;"
            "text-align:center;box-sizing:border-box;min-height:44px}"
          "select:disabled,input:disabled{opacity:.55}"
          ".muted{opacity:.55}"
          ".hidden{display:none!important}"
          ".subt{color:#00f0ff;font-size:var(--fs-body);text-align:center;margin:8px 0 2px}"
          ".button-group-vertical{display:flex;flex-direction:column;gap:10px;justify-content:center;margin-top:10px;width:100%}"
          ".btn,button{background-color:#00f0ff;color:#0f172a;border:none;padding:12px 14px;font-size:var(--fs-body);font-weight:bold;"
            "cursor:pointer;border-radius:10px;transition:background-color .2s ease,transform .15s ease;"
            "font-family:'Press Start 2P',monospace;min-height:48px;width:100%}"
          ".btn:hover,button:hover{background-color:#00c0dd;transform:translateY(-1px)}"
          "@keyframes fadeInUp{from{opacity:0;transform:translateY(40px)}to{opacity:1;transform:translateY(0)}}"
          "footer{margin-top:16px;text-align:center;font-size:var(--fs-small);color:#8aa;opacity:.9;padding:12px}"
          "footer p{margin:0}"
          "</style></head><body>";

  html += "<div class='cloud'></div>";
  html += "<div class='logo-container'><h1 class='logo-text glitch' data-text='DRUIDA BOT'>DRUIDA<br>BOT</h1></div>";

  html += "<div class='container'>";
  html += "  <div class='card-title'>Config de " + getRelayName(R4name) + "</div>";
  html += "  <form action='/saveConfigR4' method='POST' id='formR4'>";

  // Selector de Modo
  html += "    <label for='modoR4'>MODO</label>";
  html += "    <select id='modoR4' name='modoR4'>"
          "<option value='1'"  + String((modoR4 == 1)               ? " selected" : "") + ">Manual</option>"
          "<option value='2'"  + String((modoR4 == 2)               ? " selected" : "") + ">Automático</option>"
          "<option value='4'"  + String((modoR4 == SUPERCICLO)      ? " selected" : "") + ">Superciclo</option>"
          "<option value='13'" + String((modoR4 == SUPERCICLO1313)  ? " selected" : "") + ">Superciclo 13/13</option>"
          "</select>";

  // ON/OFF (Auto/Superciclo)
  html += "    <div id='secOnOff'>"
            "<label for='horaOnR4' id='lblHoraOn'>HORA ENCENDIDO</label>"
            "<input type='time' id='horaOnR4' name='horaOnR4' value='" + minutesToHHMM(showOnM) + "'>"
            "<label for='horaOffR4' id='lblHoraOff'>HORA APAGADO</label>"
            // IMPORTANTE: con name para que se envíe en Automático
            "<input type='time' id='horaOffR4' name='horaOffR4' value='" + minutesToHHMM(showOffM) + "'>"
          "</div>";

  // Amanecer/Atardecer (solo Automático)
  html += "    <div id='secDawnDusk'>"
            "<label id='lblAmanecer' for='horaAmanecer'>HORA AMANECER</label>"
            "<input type='time' id='horaAmanecer' name='horaAmanecer' value='" + minutesToHHMM(horaAmanecer) + "'>"
            "<label id='lblAtardecer' for='horaAtardecer'>HORA ATARDECER</label>"
            "<input type='time' id='horaAtardecer' name='horaAtardecer' value='" + minutesToHHMM(horaAtardecer) + "'>"
          "</div>";

  // Superciclo
  html += "    <div id='secSuper'>"
            "<div class='subt'>SUPERCICLO</div>"
            "<label for='horasLuz' id='lblHorasLuz'>HORAS DE LUZ</label>"
            "<input type='time' step='60' id='horasLuz' name='horasLuz' value='" + minutesToHHMM(horasLuz) + "'>"
            "<label for='horasOscuridad' id='lblHorasOsc'>HORAS DE OSCURIDAD</label>"
            "<input type='time' step='60' id='horasOscuridad' name='horasOscuridad' value='" + minutesToHHMM(horasOscuridad) + "'>"
          "</div>";

  // Manual
  html += "    <div id='secManual' class='hidden'>"
            "<div class='button-group-vertical'>"
              "<button type='button' class='btn' onclick=\"location.href='/controlR4On'\">ENCENDER</button>"
              "<button type='button' class='btn' onclick=\"location.href='/controlR4Off'\">APAGAR</button>"
              "<button type='button' class='btn' onclick=\"location.href='/config'\">VOLVER</button>"
            "</div>"
          "</div>";

  // Botones globales (ocultos en Manual)
  html += "    <div id='mainButtons' class='button-group-vertical'>"
            "<button type='submit' class='btn'>GUARDAR</button>"
            "<button type='button' class='btn' onclick=\"location.href='/controlR4'\">CONTROL</button>"
            "<button type='button' class='btn' onclick=\"location.href='/config'\">VOLVER</button>"
          "</div>";

  html += "  </form>";
  html += "</div>";

  html += "<footer><p>druidadata@gmail.com<br>DataDruida</p></footer>";

  // ------ Script ------
  html += "<script>"
          "function parseHHMM(x){if(!x)return-1;var i=x.indexOf(':');if(i<1)return-1;var h=parseInt(x.substring(0,i));var m=parseInt(x.substring(i+1));"
            "if(isNaN(h)||isNaN(m)||h<0||h>23||m<0||m>59)return-1;return h*60+m;}"
          "function toHHMM(mins){mins=((mins%1440)+1440)%1440;var h=Math.floor(mins/60),m=mins%60;"
            "return(h<10?'0':'')+h+':'+(m<10?'0':'')+m;}"

          // Recalcula OFF solo en modos superciclo
          "function recalcOff(){"
            "var modo=document.getElementById('modoR4').value;"
            "var onM=parseHHMM(document.getElementById('horaOnR4').value); if(onM<0) return;"
            "var offInput=document.getElementById('horaOffR4');"
            "if(modo==='4'){"
              "var luzM=parseHHMM(document.getElementById('horasLuz').value);"
              "if(luzM>=0){offInput.value=toHHMM(onM+luzM);} return;"
            "}"
            "if(modo==='13'){offInput.value=toHHMM(onM+780); return;}"
          "}"

          "function applyModeUI(){"
            "var modo=document.getElementById('modoR4').value;"
            "var secOnOff=document.getElementById('secOnOff');"
            "var secDawnDusk=document.getElementById('secDawnDusk');"
            "var secSuper=document.getElementById('secSuper');"
            "var secManual=document.getElementById('secManual');"
            "var mainButtons=document.getElementById('mainButtons');"
            "var offInput=document.getElementById('horaOffR4');"
            "var offLabel=document.getElementById('lblHoraOff');"
            "var luz=document.getElementById('horasLuz'); var lblLuz=document.getElementById('lblHorasLuz');"
            "var oscur=document.getElementById('horasOscuridad'); var lblOsc=document.getElementById('lblHorasOsc');"
            "var aman=document.getElementById('horaAmanecer'); var atar=document.getElementById('horaAtardecer');"
            "var lblAman=document.getElementById('lblAmanecer'); var lblAtar=document.getElementById('lblAtardecer');"

            // reset visible/estados
            "secOnOff.classList.add('hidden'); secDawnDusk.classList.add('hidden'); secSuper.classList.add('hidden');"
            "secManual.classList.add('hidden'); mainButtons.classList.remove('hidden');"
            "offInput.disabled=false; offInput.classList.remove('muted'); offLabel.classList.remove('muted');"
            "if(luz){luz.disabled=false; luz.classList.remove('muted');} if(lblLuz){lblLuz.classList.remove('muted');}"
            "if(oscur){oscur.disabled=false; oscur.classList.remove('muted');} if(lblOsc){lblOsc.classList.remove('muted');}"
            "if(aman){aman.readOnly=false; aman.classList.remove('muted');} if(atar){atar.readOnly=false; atar.classList.remove('muted');}"
            "if(lblAman){lblAman.classList.remove('muted');} if(lblAtar){lblAtar.classList.remove('muted');}"

            // modos
            "if(modo==='1'){secManual.classList.remove('hidden'); mainButtons.classList.add('hidden'); return;}"
            "if(modo==='2'){"
              "secOnOff.classList.remove('hidden');"
              "secDawnDusk.classList.remove('hidden');"
              // En Automático el OFF lo define el usuario
              "offInput.disabled=false; offInput.classList.remove('muted'); offLabel.classList.remove('muted');"
              "return;"
            "}"
            "if(modo==='4'){"
              "secOnOff.classList.remove('hidden');"
              "secSuper.classList.remove('hidden');"
              // OFF calculado, bloquear edición
              "offInput.disabled=true; offInput.classList.add('muted'); offLabel.classList.add('muted');"
              "recalcOff(); return;"
            "}"
            "if(modo==='13'){"
              "secOnOff.classList.remove('hidden');"
              "secSuper.classList.remove('hidden');"
              // fijar 13:00 y bloquear edición de duraciones
              "if(luz){luz.value='13:00'; luz.disabled=true; luz.classList.add('muted');}"
              "if(oscur){oscur.value='13:00'; oscur.disabled=true; oscur.classList.add('muted');}"
              "if(lblLuz){lblLuz.classList.add('muted');} if(lblOsc){lblOsc.classList.add('muted');}"
              // OFF calculado, bloquear edición
              "offInput.disabled=true; offInput.classList.add('muted'); offLabel.classList.add('muted');"
              "recalcOff(); return;"
            "}"
          "}"

          "document.getElementById('modoR4').addEventListener('change',applyModeUI);"
          "document.getElementById('horaOnR4').addEventListener('change',recalcOff);"
          "var luzEl=document.getElementById('horasLuz'); if(luzEl){luzEl.addEventListener('change',recalcOff);}"
          "window.onload=function(){applyModeUI();};"
          "</script>";

  html += "</body></html>";

  server.send(200, "text/html", html);
}










void saveConfigR4() {
  if (server.method() != HTTP_POST) {
    server.send(405, "text/plain", "Método no permitido");
    return;
  }

  // ===== 1) Lectura =====
  if (server.hasArg("modoR4")) modoR4 = server.arg("modoR4").toInt();

  // Hora encendido (ancla UI)
  if (server.hasArg("horaOnR4")) {
    int onMin = parseHHMMToMinutesSafe(server.arg("horaOnR4"));
    if (onMin >= 0) { horaOnR4 = onMin / 60; minOnR4 = onMin % 60; }
  }

  // Hora apagado (solo si NO es SUPERCICLO)
  if (modoR4 != SUPERCICLO && server.hasArg("horaOffR4")) {
    int offMin = parseHHMMToMinutesSafe(server.arg("horaOffR4"));
    if (offMin >= 0) { horaOffR4 = offMin / 60; minOffR4 = offMin % 60; }
  }

  // Amanecer / Atardecer
  if (server.hasArg("horaAmanecer")) {
    int am = parseHHMMToMinutesSafe(server.arg("horaAmanecer"));
    if (am >= 0) horaAmanecer = am;
  }
  if (server.hasArg("horaAtardecer")) {
    int at = parseHHMMToMinutesSafe(server.arg("horaAtardecer"));
    if (at >= 0) horaAtardecer = at;
  }

  // Duraciones SUPER (minutos)
  if (server.hasArg("horasLuz")) {
    int luz = parseHHMMToMinutesSafe(server.arg("horasLuz"));
    if (luz >= 0) horasLuz = luz;
  }
  if (server.hasArg("horasOscuridad")) {
    int osc = parseHHMMToMinutesSafe(server.arg("horasOscuridad"));
    if (osc >= 0) horasOscuridad = osc;
  }

  // ===== 2) Normalizaciones =====
  horaOnR4  = constrain(horaOnR4,  0, 23);
  minOnR4   = constrain(minOnR4,   0, 59);
  horaOffR4 = constrain(horaOffR4, 0, 23);
  minOffR4  = constrain(minOffR4,  0, 59);

  long periodo = (long)horasLuz + (long)horasOscuridad;
  if (modoR4 == SUPERCICLO && periodo <= 0) {
    horasLuz = 12 * 60; horasOscuridad = 12 * 60;
  }

  // ===== 3) SUPERCICLO: dejar UI coherente con on + horasLuz =====
if (modoR4 == SUPERCICLO) {
  int Lm = (int)horasLuz;
  if (Lm <= 0) Lm = 1; // fallback seguro

  int onAbs  = (horaOnR4  * 60 + minOnR4 ) % 1440;
  int offAbs = (onAbs + Lm) % 1440;

  // Par canónico que la UI debe mostrar
  horaOffR4 = offAbs / 60;
  minOffR4  = offAbs % 60;

  // (Opcional) si tu UI usa estos para mostrar “próximos”
  nextOnR4Abs  = onAbs;
  nextOffR4Abs = offAbs;

  // Nota: no tocamos relé ni “seed” acá. El loop de SUPERCICLO
  // recalcula y persiste en cada encendido/apagado.
}


  // ===== 4) Persistencia y respuesta =====
  Guardado_General();
  handleSaveConfig();

  // ===== 5) Debug =====
  Serial.println(F("------ [saveConfigR4] ------"));
  Serial.print(F("Modo R4: ")); Serial.println(modoR4);
  Serial.print(F("ON  (UI): ")); Serial.print(horaOnR4);  Serial.print(':'); Serial.println(minOnR4);
  Serial.print(F("OFF (UI): ")); Serial.print(horaOffR4); Serial.print(':'); Serial.println(minOffR4);
  Serial.print(F("Amanecer(min): ")); Serial.println(horaAmanecer);
  Serial.print(F("Atardecer(min): ")); Serial.println(horaAtardecer);
  Serial.print(F("Luz(min): ")); Serial.println(horasLuz);
  Serial.print(F("Osc(min): ")); Serial.println(horasOscuridad);
  if (modoR4 == SUPERCICLO) {
    Serial.print(F("nextOnAbs : "));  Serial.println(nextOnR4Abs);
    Serial.print(F("nextOffAbs: "));  Serial.println(nextOffR4Abs);
  }
  Serial.println(F("---------------------------"));
}







void handleSaveConfig() {
    // Guardar configuraciones (ya definido en tu código)
    Guardado_General();

    // Mostrar mensaje de confirmación
    handleConfirmation("Configuracion guardada correctamente", "/config");
}

void saveConfigR1() {
    if (server.method() == HTTP_POST) {
        // Verificar y asignar cada parámetro recibido
        if (server.hasArg("modoR1")) {
            modoR1 = server.arg("modoR1").toInt();
        }
        if (server.hasArg("minR1")) {
            minR1 = server.arg("minR1").toFloat();
        }
        if (server.hasArg("maxR1")) {
            maxR1 = server.arg("maxR1").toFloat();
        }
        if (server.hasArg("paramR1")) {
            paramR1 = server.arg("paramR1").toInt();
        }
        
        if (server.hasArg("direccionR1")) {
            direccionR1 = server.arg("direccionR1").toInt();
        }

        if (server.hasArg("horaOnR1")) {
            String horaOn = server.arg("horaOnR1");
            horaOnR1 = horaOn.substring(0, horaOn.indexOf(":")).toInt();
            minOnR1 = horaOn.substring(horaOn.indexOf(":") + 1).toInt();
        }
        if (server.hasArg("horaOffR1")) {
            String horaOff = server.arg("horaOffR1");
            horaOffR1 = horaOff.substring(0, horaOff.indexOf(":")).toInt();
            minOffR1 = horaOff.substring(horaOff.indexOf(":") + 1).toInt();
        }
        if (server.hasArg("estadoR1")) {
            estadoR1 = server.arg("estadoR1").toInt();
        }

        if (server.hasArg("R1name")) {
            R1name = server.arg("R1name").toInt();
        }

        // Guardar cambios y mostrar confirmación
        handleSaveConfig();
    } else {
        // Si no es un método POST, devolver error
        server.send(405, "text/plain", "Método no permitido");
    }
}

void saveConfigR5() {
    if (server.method() == HTTP_POST) {
        // Verificar y asignar cada parámetro recibido
        if (server.hasArg("modoR5")) {
            modoR5 = server.arg("modoR5").toInt();
        }
        if (server.hasArg("minR5")) {
            minR5 = server.arg("minR5").toFloat();
        }
        if (server.hasArg("maxR5")) {
            maxR5 = server.arg("maxR5").toFloat();
        }
        if (server.hasArg("paramR5")) {
            paramR5 = server.arg("paramR5").toInt();
        }
        if (server.hasArg("direccionR5")) {
            direccionR5 = server.arg("direccionR5").toInt();
        }

        if (server.hasArg("horaOnR5")) {
            String horaOn = server.arg("horaOnR5");
            horaOnR5 = horaOn.substring(0, horaOn.indexOf(":")).toInt();
            minOnR5  = horaOn.substring(horaOn.indexOf(":") + 1).toInt();
        }
        if (server.hasArg("horaOffR5")) {
            String horaOff = server.arg("horaOffR5");
            horaOffR5 = horaOff.substring(0, horaOff.indexOf(":")).toInt();
            minOffR5  = horaOff.substring(horaOff.indexOf(":") + 1).toInt();
        }
        if (server.hasArg("estadoR5")) {
            estadoR5 = server.arg("estadoR5").toInt();
        }

        if (server.hasArg("R5name")) {
            R5name = server.arg("R5name").toInt();
        }

        // Guardar cambios y mostrar confirmación
        handleSaveConfig();
    } else {
        // Si no es un método POST, devolver error
        server.send(405, "text/plain", "Método no permitido");
    }
}



// Mensaje de confirmación



// Función para solicitar datos al Arduino Nano (I2C)
// Función para solicitar datos al Arduino Nano (I2C)



void mostrarEnPantallaOLED(float temperature, float humedad, float DPV, String hora) {
  display.clearDisplay();
  display.setTextColor(SH110X_WHITE);

  // Validaciones simples y robustas
  bool okTemp = isfinite(temperature) && temperature > -40.0f && temperature < 85.0f; // rango típico AHT/Si7021
  bool okHum  = isfinite(humedad)     && humedad   >= 0.0f    && humedad   <= 100.0f;
  bool okDPV  = isfinite(DPV)         && DPV       >= 0.0f    && DPV       <  30.0f;  // hPa o kPa*10; ajustá si hiciera falta

  // Si cualquiera es inválido => mostramos nan en TODOS
  bool sensorOffline = !(okTemp && okHum && okDPV);

  String tempDisplay = sensorOffline ? "nan" : (String(temperature, 1) + " C");
  String humDisplay  = sensorOffline ? "nan" : (String(humedad, 1) + " %");
  String dpvDisplay  = sensorOffline ? "nan" : String(DPV, 1);

  // --- UI ---
  display.setTextSize(2);
  display.setCursor(0, 0);
  display.print("T: ");
  display.print(tempDisplay);

  display.setCursor(0, 20);
  display.print("H: ");
  display.print(humDisplay);

  display.setCursor(0, 40);
  display.print("VPD: ");
  display.print(dpvDisplay);
  if (!sensorOffline) {            // solo mostrar unidad si el valor es válido
    display.setTextSize(1);
    display.print("hPa");
  }

  // Hora e IP
  display.setTextSize(1);
  display.setCursor(95, 57);
  display.print(hora);
  display.setCursor(0, 57);
  display.print((WiFi.getMode() == WIFI_STA) ? WiFi.localIP() : WiFi.softAPIP());

  display.display();
}






void mostrarMensajeBienvenida() {
  display.clearDisplay();
  display.setTextSize(3);
  display.setTextColor(SH110X_WHITE); // ✅ Correcta para Adafruit_SH110X
  // Tamaño del texto más grande
  //display.setTextColor(SSD1306_WHITE  );  // Color del texto

  // Mostrar el mensaje de bienvenida "Druida"
  display.setCursor((128 - (6 * 3 * 6)) / 2, 5);    // Centrando "Druida" en X
  display.println("Druida");

  // Mostrar "Bot" justo debajo, centrado
  display.setCursor((128 - (3 * 3 * 6)) / 2, 35);    // Centrando "Bot" en X
  display.println("Bot");

  display.display();            // Actualiza la pantalla
}

String obtenerMotivoReinicio() {
  esp_reset_reason_t resetReason = esp_reset_reason();
  String motivoReinicio;

  switch (resetReason) {
    case ESP_RST_POWERON:
      motivoReinicio = "Power-on";
      break;
    case ESP_RST_EXT:
      motivoReinicio = "External reset";
      break;
    case ESP_RST_SW:
      motivoReinicio = "Software reset";
      break;
    case ESP_RST_PANIC:
      motivoReinicio = "Panic";
      break;
    case ESP_RST_INT_WDT:
      motivoReinicio = "Interrupt WDT";
      break;
    case ESP_RST_TASK_WDT:
      motivoReinicio = "Task WDT";
      break;
    case ESP_RST_WDT:
      motivoReinicio = "Other WDT";
      break;
    case ESP_RST_DEEPSLEEP:
      motivoReinicio = "Deep sleep";
      break;
    case ESP_RST_BROWNOUT:
      motivoReinicio = "Brownout";
      break;
    case ESP_RST_SDIO:
      motivoReinicio = "SDIO reset";
      break;
    default:
      motivoReinicio = "Unknown";
      break;
  }

  return motivoReinicio;
}

String convertirModo(int modo) {
    switch (modo) {
        case MANUAL:
            return "Manual";
        case AUTO:
            return "Automático";
        case CONFIG:
            return "Configuración";
        case STATUS:
            return "Estado";
        case SUPERCICLO:
            return "Superciclo";
        case TIMER:
            return "Temporizador";
        case RIEGO:
            return "Riego";
        default:
            return "Desconocido";
    }
}

String convertirParametro(int parametro) {
    switch (parametro) {
        case H:
            return "Humedad";
        case T:
            return "Temperatura";
        case D:
            return "DPV";
        default:
            return "Desconocido";
    }
}


String convertirDia(int dia) {
    switch (dia) {
        case 0:
            return "Domingo";
        case 1:
            return "Lunes";
        case 2:
            return "Martes";
        case 3:
            return "Miércoles";
        case 4:
            return "Jueves";
        case 5:
            return "Viernes";
        case 6:
            return "Sábado";
        default:
            return "Desconocido";
    }
}


String formatoHora(int hora, int minuto) {
    char buffer[6]; // Buffer para almacenar la cadena formateada
    sprintf(buffer, "%02d:%02d", hora, minuto); // Formatear con dos dígitos
    return String(buffer); // Devolver como String
}



void riegoIntermitente() {
  unsigned long currentMillis = millis();

  if (!enRiego) { // Si está en pausa
    if (currentMillis - previousMillisRiego >= tiempoNoRiego * 1000) {
      // Encender el relé
      digitalWrite(RELAY3, LOW);
      R3estado = LOW;
      previousMillisRiego = currentMillis;
      enRiego = true;
    }
  } else { // Si está en riego
    if (currentMillis - previousMillisRiego >= tiempoRiego * 1000) {
      // Apagar el relé
      digitalWrite(RELAY3, HIGH);
      R3estado = HIGH;
      previousMillisRiego = currentMillis;
      enRiego = false;
    }
  }
}


void moveServoSlowly(int targetPosition) {
  if (targetPosition > currentPosition) {
    for (int pos = currentPosition; pos <= targetPosition; pos++) {
      dimmerServo.write(pos); // Mover un paso
      delay(15); // Controla la velocidad del movimiento (ajusta si es necesario)
    }
  } else {
    for (int pos = currentPosition; pos >= targetPosition; pos--) {
      dimmerServo.write(pos); // Mover un paso
      delay(15); // Controla la velocidad del movimiento (ajusta si es necesario)
    }
  }
  currentPosition = targetPosition; // Actualizar la posición actual
}

String getRelayName(int relayIndex) {
    if (relayIndex >= 0 && relayIndex < 10) {
        return relayNames[relayIndex];
    }
    return "Desconocido";
}

String htmlEncode(const String& data) {
  String encoded = "";
  for (unsigned int i = 0; i < data.length(); ++i) {
    char c = data.charAt(i);
    switch (c) {
      case '&': encoded += "&amp;"; break;
      case '<': encoded += "&lt;"; break;
      case '>': encoded += "&gt;"; break;
      case '"': encoded += "&quot;"; break;
      case '\'': encoded += "&#39;"; break;
      default:
        // Solo incluir caracteres imprimibles normales
        if (c >= 32 && c <= 126) {
          encoded += c;
        } else {
          encoded += '?';  // Caracteres no imprimibles se muestran como '?'
        }
        break;
    }
  }
  return encoded;
}

/*void requestSensorData() {
  sensorDataValid = false;

  Wire.requestFrom(8, 8);  // Dirección 8, 8 bytes esperados

  unsigned long start = millis();
  while (Wire.available() < 8 && millis() - start < 100) {
    delay(1); // Espera a que lleguen todos los datos
  }

  if (Wire.available() == 8) {
    // Leer los 4 enteros de 2 bytes (MSB + LSB)
    sensor1Value = Wire.read() << 8 | Wire.read();  // H1 (%)
    sensor2Value = Wire.read() << 8 | Wire.read();  // H2 (%)
    sensor3Value = Wire.read() << 8 | Wire.read();  // H3 (%)
    int rawPH = Wire.read() << 8 | Wire.read();     // pH * 100
    sensorPH = rawPH / 100.0;

    sensorDataValid = true;

    // Debug opcional
    Serial.printf("H1: %d%%, H2: %d%%, H3: %d%%, pH: %.2f\n", sensor1Value, sensor2Value, sensor3Value, sensorPH);

  } else {
    Serial.println("⚠️ Error: no se recibieron datos I2C desde Arduino Nano.");
    sensor1Value = sensor2Value = sensor3Value = 0;
    sensorPH = 0.0;
  }
}*/




void requestSensorData() {
  sensorDataValid = false;

  I2CNano.requestFrom(8, 8);  // Dirección 8, 8 bytes esperados

  unsigned long start = millis();
  while (I2CNano.available() < 8 && millis() - start < 100) {
    delay(1);
  }

  if (I2CNano.available() == 8) {
    sensor1Value = I2CNano.read() << 8 | I2CNano.read();  // H1
    sensor2Value = I2CNano.read() << 8 | I2CNano.read();  // H2
    sensor3Value = I2CNano.read() << 8 | I2CNano.read();  // H3
    int rawPH = I2CNano.read() << 8 | I2CNano.read();     // pH
    sensorPH = rawPH / 100.0;

    sensorDataValid = true;
    Serial.printf("H1: %d%%, H2: %d%%, H3: %d%%, pH: %.2f\n", sensor1Value, sensor2Value, sensor3Value, sensorPH);
  } else {
    //Serial.println("⚠️ Error: no se recibieron datos I2C desde Arduino Nano.");
    sensor1Value = sensor2Value = sensor3Value = 0;
    sensorPH = 0.0;
  }
}


void debugPrintConfig() {
  Serial.println(F("===== CONFIGURACIÓN CARGADA ====="));

  // R1–R4
  Serial.print(F("R1 -> min: ")); Serial.print(minR1);
  Serial.print(F(" | max: ")); Serial.print(maxR1);
  Serial.print(F(" | modo: ")); Serial.print(modoR1);
  Serial.print(F(" | estado: ")); Serial.println(estadoR1);

  Serial.print(F("R2 -> min: ")); Serial.print(minR2);
  Serial.print(F(" | max: ")); Serial.print(maxR2);
  Serial.print(F(" | modo: ")); Serial.print(modoR2);
  Serial.print(F(" | estado: ")); Serial.println(estadoR2);

  Serial.print(F("R3 -> min: ")); Serial.print(minR3);
  Serial.print(F(" | max: ")); Serial.print(maxR3);
  Serial.print(F(" | modo: ")); Serial.print(modoR3);
  Serial.print(F(" | estado: ")); Serial.println(estadoR3);

  Serial.print(F("R4 -> modo: ")); Serial.print(modoR4);
  Serial.print(F(" | estado: ")); Serial.println(estadoR4);

  // R5 (el crítico)
  Serial.print(F("R5 -> min: ")); Serial.print(minR5);
  Serial.print(F(" | max: ")); Serial.print(maxR5);
  Serial.print(F(" | minOn: ")); Serial.print(minOnR5);
  Serial.print(F(" | minOff: ")); Serial.print(minOffR5);
  Serial.print(F(" | horaOn: ")); Serial.print(horaOnR5);
  Serial.print(F(" | horaOff: ")); Serial.print(horaOffR5);
  Serial.print(F(" | param: ")); Serial.print(paramR5);
  Serial.print(F(" | direccion: ")); Serial.print(direccionR5);
  Serial.print(F(" | modo: ")); Serial.print(modoR5);
  Serial.print(F(" | estado: ")); Serial.println(estadoR5);

  // WiFi
  Serial.print(F("SSID: ")); Serial.println(ssid);
  Serial.print(F("Password: ")); Serial.println(password);

  // Telegram
  Serial.print(F("Chat ID: ")); Serial.println(chat_id);

  Serial.println(F("================================="));
}

// === Escritura de String ACOTADA por capacidad del bloque ===
void writeStringBounded(int addr, const String& s, size_t capacity) {
  if (capacity < 2) return;                 // necesita al menos len + '\0'
  size_t maxChars = capacity - 2;           // espacio útil real
  size_t n = s.length();
  if (n > maxChars) n = maxChars;

  EEPROM.write(addr, (uint8_t)n);           // longitud efectiva
  for (size_t i = 0; i < n; ++i) {
    EEPROM.write(addr + 1 + i, s[i]);
  }
  EEPROM.write(addr + 1 + n, '\0');         // terminador

  // Limpia sobrante del bloque (evita residuos de strings previas más largas)
  for (size_t i = n + 1; i < capacity; ++i) {
    EEPROM.write(addr + i, 0x00);
  }
  // En ESP8266/ESP32: acordate de EEPROM.commit() tras guardar todo.
}

// === Lectura de String ACOTADA por capacidad del bloque ===
String readStringBounded(int addr, size_t capacity) {
  if (capacity < 2) return String();
  uint8_t n = EEPROM.read(addr);
  size_t maxChars = capacity - 2;
  if (n > maxChars) n = maxChars;

  String out;
  out.reserve(n);
  for (size_t i = 0; i < n; ++i) {
    out += char(EEPROM.read(addr + 1 + i));
  }
  return out;
}

// Convierte "HH:MM" a minutos (0..1439). Si String inválido, devuelve -1.
int parseHHMMToMinutes(const String& hhmm) {
  if (hhmm.length() < 4) return -1;
  int sep = hhmm.indexOf(':');
  if (sep < 0) return -1;
  int h = hhmm.substring(0, sep).toInt();
  int m = hhmm.substring(sep + 1).toInt();
  if (h < 0 || h > 23 || m < 0 || m > 59) return -1;
  return h * 60 + m;
}

// Normaliza minutos de reloj a rango [0,1439] (usalo sólo si lo necesitás)
int normClockMin(int mm) {
  int x = mm % 1440;
  if (x < 0) x += 1440;
  return x;
}

// ===== Tiempo absoluto (RTC) y conversión a reloj =====
// Minuto absoluto desde RTC (DS3231)
unsigned long nowAbsMin() {
  return rtc.now().unixtime() / 60UL;
}

// Convierte minuto absoluto a reloj del día (0..1439, -1 si inválido)
int absToClockMin(long absMin) {
  if (absMin < 0) return -1;
  long m = absMin % 1440L;
  if (m < 0) m += 1440L;
  return (int)m;
}

// Setea HH:MM visibles desde minuto absoluto
void setClockFromAbs(long absMin, int &outHour, int &outMin) {
  int cm = absToClockMin(absMin);
  if (cm >= 0) {
    outHour = cm / 60;
    outMin  = cm % 60;
  }
}

// ===== Semilla clara del SUPERCICLO (idempotente) =====
void seedSupercicloR4(bool encendidaAhora) {
  Serial.println(F("[SUPERCICLO] Seed R4"));

  int nowM  = nowMinutesLocal();                // 0..1439
  int onUI  = (horaOnR4 * 60 + minOnR4) % 1440; // ancla UI
  int offUI = (onUI + horasLuz) % 1440;

  auto inWindow = [&](int now, int start, int end) {
    return (start < end) ? (now >= start && now < end)
                         : (now >= start || now < end);
  };

  if (encendidaAhora) {
    // Encendida: próximo evento = apagar en now + horasLuz
    nextOffR4Abs = (nowM + (int)horasLuz) % 1440;
    nextOnR4Abs  = (nextOffR4Abs + (int)horasOscuridad) % 1440;
  } else {
    if (inWindow(nowM, onUI, offUI)) {
      // Está dentro de la ventana pero apagada: apaga pronto (failsafe) y rearmá ciclo
      nextOffR4Abs = (nowM + (int)horasLuz) % 1440;    // o ahora mismo si querés
      nextOnR4Abs  = (nextOffR4Abs + (int)horasOscuridad) % 1440;
    } else {
      // Próximo encendido en el onUI válido (hoy o siguiente ciclo)
      // si ya pasó onUI hoy, programá el próximo ciclo completo
      int ciclo = ((int)horasLuz + (int)horasOscuridad) % 1440;
      if (ciclo == 0) ciclo = 1; // evitar ciclos nulos
      int targetOn = (nowM <= onUI) ? onUI : (onUI + ciclo) % 1440;

      nextOnR4Abs  = targetOn;
      nextOffR4Abs = (targetOn + (int)horasLuz) % 1440;
    }
  }

  // Saneá por si acaso (nunca números grandes)
  nextOnR4Abs  = (nextOnR4Abs  % 1440 + 1440) % 1440;
  nextOffR4Abs = (nextOffR4Abs % 1440 + 1440) % 1440;

  Serial.print(F("  nextOnR4Abs : "));  Serial.println(nextOnR4Abs);
  Serial.print(F("  nextOffR4Abs: "));  Serial.println(nextOffR4Abs);
}


// Convierte minutos [0..1439] a "HH:MM"
String minutesToHHMM(int mins) {
  if (mins < 0) mins = 0;
  mins %= 1440;
  return formatTwoDigits(mins/60) + ":" + formatTwoDigits(mins%60);
}

int parseHHMMToMinutesSafe(const String& hhmm) {
  String s = hhmm; s.trim();
  int colon = s.indexOf(':');
  if (colon < 1 || colon > (int)s.length()-2) return -1;
  int h = s.substring(0, colon).toInt();
  int m = s.substring(colon+1).toInt();
  if (h < 0 || h > 23 || m < 0 || m > 59) return -1;
  return h*60 + m;
}

// Usa el mismo criterio que tu loop: DS3231 -> hora/minuto y -3 horas.
// Requiere: RTC_DS3231 rtc; (o equivalente) ya inicializado en setup().
inline int nowMinutesLocal() {
  DateTime now = rtc.now();

  int h = now.hour();   // 0..23 (hora del RTC)
  int m = now.minute(); // 0..59

  // Aplicar mismo ajuste que en tu loop:
  h -= 3;
  if (h < 0) h += 24;   // wrap a rango 0..23

  // devolver minutos locales 0..1439
  return h * 60 + m;
}

// Devuelve YYYYMMDD local (aplicando -3h si tu RTC está en UTC)
uint32_t localDateKey(const DateTime& now) {
  // Ajuste local: -3 horas (si tu DS3231 ya está en hora local, poné TZ=0)
  const int32_t TZ = -3 * 3600;
  uint32_t t = now.unixtime();
  t += TZ;
  DateTime loc(t);

  // yyyymmdd como entero: 20250915
  return (uint32_t)(loc.year()) * 10000UL + (uint32_t)(loc.month()) * 100UL + (uint32_t)(loc.day());
}

// Llamar en setup() tras cargar memoria y también en loop() periódicamente
void tickDaily() {
  DateTime now = rtc.now();
  uint32_t todayKey = localDateKey(now);

  if (lastDateKey == 0) {
    // Primer arranque/boot: inicializamos referencia de día
    lastDateKey = todayKey;
    Guardado_General();
    return;
  }

  if (todayKey != lastDateKey) {
    // ¡Cambiamos de día! Incrementar contadores activos
    if (vegeActive && vegeDays > 0)  vegeDays++;
    if (floraActive && floraDays > 0) floraDays++;
    lastDateKey = todayKey;
    Guardado_General();
  }
}


// ===== Ajuste horario (cambiar a 0 si tu DS3231 ya está en hora local) =====
static const int32_t TZ_SECONDS = -3 * 3600; // Argentina (-3)

// Epoch "local" para que coincida con tu UI
uint32_t nowLocalEpoch() {
  DateTime now = rtc.now();
  uint32_t t = now.unixtime();
  t += TZ_SECONDS; // si tu RTC ya está en local, quita esta línea o poné TZ_SECONDS=0
  return t;
}

// Convierte horas a segundos con sanidad
static inline uint32_t hoursToSec(int h) {
  if (h < 0) h = 0;
  return (uint32_t)h * 3600UL;
}



// ===== Utilidades
static inline int64_t nowUtcSec64() {
  return (int64_t)rtc.now().unixtime();
}

//constexpr uint32_t SEC_PER_DAY = 86400UL;


// ===== OPCIONAL: versión en minutos para ciclos no enteros (12.5 h = 750 min)
int virtualDaysSinceMinutes(uint32_t startEpoch,
                            uint32_t minutosLuz,
                            uint32_t minutosOscuridad,
                            int tzOffsetSec = (-3 * 3600)) {
  if (startEpoch == 0) return 0;

  uint64_t cycleMin = (uint64_t)minutosLuz + (uint64_t)minutosOscuridad;
  if (cycleMin == 0) return 0;

  uint64_t cycleSec = cycleMin * 60ULL;

  int64_t nowLocal   = nowUtcSec64()        + (int64_t)tzOffsetSec;
  int64_t startLocal = (int64_t)startEpoch  + (int64_t)tzOffsetSec;
  if (nowLocal < startLocal) return 0;

  uint64_t elapsed = (uint64_t)(nowLocal - startLocal);
  uint64_t cyclesCompleted = elapsed / cycleSec;
  return (int)(cyclesCompleted + 1);
}


void handleSetFloraDay() {
  if (server.method() != HTTP_POST) {
    server.send(405, "text/plain", "Método no permitido");
    return;
  }

  if (!server.hasArg("floraDay")) {
    handleConfirmation("Falta parámetro 'floraDay'", "/controlR4");
    return;
  }

  int day = server.arg("floraDay").toInt();
  if (day < 1) day = 1;
  if (day > 200) day = 200; // límite sano

  // Queremos que realDaysSince(floraStartEpoch) == day
  // realDaysSince cuenta desde 1 y hace cortes cada 24h locales, pero al usar epoch UTC
  // alcanza con retrasar el inicio (day-1) días en UTC:
  time_t nowUTC = rtc.now().unixtime();
  uint32_t back = (uint32_t)(day - 1) * 86400UL;

  floraStartEpoch = (uint32_t)(nowUTC - back);
  floraActive     = true;  // si estaba apagado, lo activamos

  Guardado_General();
  handleConfirmation("FLORACIÓN ajustada · Día " + String(day), "/controlR4");
}

void handleSetVegeDay() {
  if (server.method() != HTTP_POST) {
    server.send(405, "text/plain", "Método no permitido");
    return;
  }

  if (!server.hasArg("vegeDay")) {
    handleConfirmation("Falta parámetro 'vegeDay'", "/controlR4");
    return;
  }

  int day = server.arg("vegeDay").toInt();
  if (day < 1) day = 1;
  if (day > 200) day = 200; // tope razonable

  // Queremos que realDaysSince(vegeStartEpoch) == day
  time_t nowUTC = rtc.now().unixtime();
  uint32_t back = (uint32_t)(day - 1) * 86400UL;

  vegeStartEpoch = (uint32_t)(nowUTC - back);
  vegeActive     = true;   // lo activamos si estaba apagado

  Guardado_General();
  handleConfirmation("VEGETATIVO ajustado · Día " + String(day), "/controlR4");
}

// Arrancar LUZ "ahora"
void supercycleStartLightNow() {
  int32_t now_epoch = rtc.now().unixtime();       // ajustá si tu RTC está en UTC
  superAnchorEpochR4 = now_epoch;                 // ancla al inicio de la luz actual
  Guardado_General();                             // persiste el ANCLA y duraciones
  // Forzar reseed en el loop:
  // (podés poner variables estáticas nextOnEpoch/nextOffEpoch a -1 desde aquí si las haces globales)
}

// Arrancar OSCURIDAD "ahora"
void supercycleStartDarkNow() {
  int32_t now_epoch = rtc.now().unixtime();
  superAnchorEpochR4 = now_epoch - (int32_t)horasLuz * 60; // estamos L min después del ancla
  Guardado_General();
  // idem reseed
}

// Cambia duraciones manteniendo fase del ciclo
void supercycleSetDurations(uint16_t Lmin, uint16_t Dmin) {
  horasLuz = Lmin; horasOscuridad = Dmin;
  Guardado_General();
  // El loop, con el mismo superAnchorEpochR4, recalculará todo coherente.
}

void setAllRelays(bool turnOn) {
  // R1
  estadoR1 = turnOn ? 1 : 0;
  modoR1   = MANUAL;               // si en tu código MANUAL = 1, queda alineado
  setRelayActiveLow(RELAY1, turnOn);

  // R2
  estadoR2 = turnOn ? 1 : 0;
  modoR2   = MANUAL;
  setRelayActiveLow(RELAY2, turnOn);

  // R3
  estadoR3 = turnOn ? 1 : 0;
  modoR3   = MANUAL;
  setRelayActiveLow(RELAY3, turnOn);

  // R4
  estadoR4 = turnOn ? 1 : 0;
  modoR4   = MANUAL;
  setRelayActiveLow(RELAY4, turnOn);

  // R5 (lógica inversa, ya tenés helper propio)
  estadoR5 = turnOn ? 1 : 0;
  modoR5   = MANUAL;
  setRelay5(turnOn);               // ON = HIGH (cierra), OFF = LOW (abre)

  Guardado_General();              // persistimos TODO junto
}
