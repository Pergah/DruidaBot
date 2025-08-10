
// Proyecto: Druida BOT de DataDruida
// Autor: Bryan Murphy
// Año: 2025
// Licencia: MIT

#include "config.h"

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
    // Esperar que el bus I2C esté libre antes de comenzar
  waitForI2CBusFree(SDA_NANO, SCL_NANO);
  I2CNano.begin(SDA_NANO, SCL_NANO);
  Wire.begin();
  Serial.begin(115200);
  EEPROM.begin(512);
  rtc.begin();
  aht.begin();
  esp_task_wdt_init(WDT_TIMEOUT, true);  // Reinicio automático habilitado
  esp_task_wdt_add(NULL);

  //ProteccionSetup();

  esp_reset_reason_t resetReason = esp_reset_reason();

  // Configurar pines de relé como salidas
  pinMode(RELAY1, OUTPUT);
  pinMode(RELAY2, OUTPUT);
  pinMode(RELAY3, OUTPUT);
  pinMode(RELAY4, OUTPUT);

  // Desactivar todos los relés al inicio
  digitalWrite(RELAY1, HIGH);
  digitalWrite(RELAY2, HIGH);
  digitalWrite(RELAY3, HIGH);
  digitalWrite(RELAY4, HIGH);

  // Inicializar pantalla SSD
int retriesDisplayInit = 0;
while (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
  Serial.println(F("Error al inicializar la pantalla OLED, reintentando..."));
  retriesDisplayInit++;
  delay(500);
  if (retriesDisplayInit > 5) {
    Serial.println(F("No se pudo inicializar la pantalla OLED, reiniciando..."));
    ESP.restart();
  }
}
 
// Inicializar pantalla SH
/*
// Inicializar pantalla
int retriesDisplayInit = 0;
while (!display.begin(0x3C, true)) {  // true = hace soft reset
  Serial.println(F("Error al inicializar la pantalla OLED (SH1106), reintentando..."));
  retriesDisplayInit++;
  delay(500);
  if (retriesDisplayInit > 5) {
    Serial.println(F("No se pudo inicializar la pantalla OLED, reiniciando..."));
    ESP.restart();
  }
}*/


  display.clearDisplay();
  display.display();

  unsigned long startMillis = millis();
  Carga_General();
  dimmerServo.attach(SERVO); // 
  moveServoSlowly(currentPosition); // Mover a la última posición guardada

  mostrarMensajeBienvenida();


  if (modoWiFi == 1) {
  // Modo WiFi cliente
  connectToWiFi(ssid.c_str(), password.c_str());

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("Conectado a Wi-Fi exitosamente.");

    secured_client.setCACert(TELEGRAM_CERTIFICATE_ROOT);

    startWebServer();

    String motivoReinicio = obtenerMotivoReinicio();
    String message = "Druida Bot is ON (" + motivoReinicio + ")";
    bot.sendMessage(chat_id, message);

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
    const int DST_OFFSET_SEC = 0;

    bool horaSincronizada = false;

    for (uint8_t i = 0; NTP_SERVERS[i] != nullptr && !horaSincronizada; ++i) {
      Serial.printf("\n⏱️  Probando NTP: %s\n", NTP_SERVERS[i]);
      configTime(GMT_OFFSET_SEC, DST_OFFSET_SEC, NTP_SERVERS[i]);

      time_t now = 0;
      uint32_t t0 = millis();
      while (now < 24 * 3600 && millis() - t0 < 6000) {
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

        const char* days[] = { "Domingo", "Lunes", "Martes", "Miércoles", "Jueves", "Viernes", "Sábado" };
        diaNumero = timeinfo.tm_wday;
        Serial.print("📆 Día de hoy: ");
        Serial.println(days[diaNumero]);
      }
    } else {
      Serial.println("❌  NTP no sincronizado, continúo sin hora precisa");
    }
  } else {
    Serial.println("No se pudo conectar a Wi-Fi. Verifique las credenciales.");
  }
} else {
  // Modo AP
  Serial.println("\nModo AP activado para configuración.");
  startAccessPoint();
}

  //startAccessPoint();
  Serial.print("chat_id: ");
  Serial.println(chat_id);
  Serial.println("Menu Serial: ");
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



  // DATA TIMERS

  timeOnR3 = horaOnR3 * 60 + minOnR3;
  timeOffR3 = horaOffR3 * 60 + minOffR3;
  timeOnR4 = horaOnR4 * 60 + minOnR4;
  timeOffR4 = horaOffR4 * 60 + minOffR4;
  timeOnR1 = horaOnR1 * 60 + minOnR1;
  timeOffR1 = horaOffR1 * 60 + minOffR1;

  // MODO AUTO R3 (Riego)

  // Convierte todo a minutos para facilitar la comparación
  int currentTime = hour * 60 + minute;
  int startR3 = timeOnR3;
  int offR3 = timeOffR3;
  int startR4 = timeOnR4;  // *60
  int offR4 = timeOffR4;   // *60
  int startR1 = timeOnR1;
  int offR1 = timeOffR1;
  int c;

  //MODO TIMER R1 

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
    // Caso cruzando medianoche: encendido después que apagado
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






  if (modoR2 == AUTO) {

  if (paramR2 == H) {
    if (isnan(humedad) || humedad < 0 || humedad > 99.9) {
      bot.sendMessage(chat_id, "⚠️ Alerta: Valor de *humedad* fuera de rango o inválido en R2. Revisa el sensor o el ambiente.", "");
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
      bot.sendMessage(chat_id, "⚠️ Alerta: Valor de *temperatura* fuera de rango o inválido en R2. Revisa el sensor o el ambiente.", "");
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
      bot.sendMessage(chat_id, "⚠️ Alerta: Valor de *humedad* fuera de rango o inválido en R2 (HT).", "");
    }

    if (!temperaturaValida) {
      bot.sendMessage(chat_id, "⚠️ Alerta: Valor de *temperatura* fuera de rango o inválido en R2 (HT).", "");
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

    if (valorInvalido) {
      bot.sendMessage(chat_id, "⚠️ Alerta: Valor de parámetro inválido. Humidificador apagado por seguridad.", "");
      digitalWrite(RELAY1, LOW);
      R1estado = LOW;
      enHumidificacion = false;
      return;
    }

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

  // Control del servomotor (manejo de horarios cruzados)
  bool dentroAmanecer = (horaAmanecer < horaAtardecer) 
                          ? (currentTime >= horaAmanecer && currentTime < horaAtardecer)
                          : (currentTime >= horaAmanecer || currentTime < horaAtardecer);

  if (dentroAmanecer) {
    moveServoSlowly(180); // Simula el mediodía
  } else {
    moveServoSlowly(0); // Simula amanecer o atardecer
  }
}
/*
// MODO SUPERCICLO R4 (Luz)
if (modoR4 == SUPERCICLO) {
  if (startR4 < offR4) {
    // Caso normal: encendido antes que apagado
    if (currentTime >= startR4 && currentTime < offR4) {
      if (R4estado == HIGH) {
        digitalWrite(RELAY4, LOW);
        R4estado = LOW;

        // Calcular nueva hora de encendido ----------------------------
        startR4 = (currentTime + horasOscuridad) % 1440;
        horaOnR4 = startR4 / 60;
        minOnR4  = startR4 % 60;
        Guardado_General();
      }
    } else {
      if (R4estado == LOW) {
        digitalWrite(RELAY4, HIGH);
        R4estado = HIGH;

        // Calcular nueva hora de apagado ------------------------------
        offR4 = (currentTime + horasLuz) % 1440;
        horaOffR4 = offR4 / 60;
        minOffR4  = offR4 % 60;
        Guardado_General();
      }
    }
  } else {
    // Caso cruzando medianoche: encendido después que apagado
    if (currentTime >= startR4 || currentTime < offR4) {
      if (R4estado == HIGH) {
        digitalWrite(RELAY4, LOW);
        R4estado = LOW;

        // Calcular nueva hora de encendido ----------------------------
        startR4 = (currentTime + horasOscuridad) % 1440;
        horaOnR4 = startR4 / 60;
        minOnR4  = startR4 % 60;
        Guardado_General();
      }
    } else {
      if (R4estado == LOW) {
        digitalWrite(RELAY4, HIGH);
        R4estado = HIGH;

        // Calcular nueva hora de apagado ------------------------------
        offR4 = (currentTime + horasLuz) % 1440;
        horaOffR4 = offR4 / 60;
        minOffR4  = offR4 % 60;
        Guardado_General();
      }
    }
  }
}
*/

// ----------  VARIABLES DE APOYO  ----------
   // solo al entrar en SUPERCICLO
// (si pones varios modos, reinícialo a false cuando cambies de modo)



// ----------  MODO SUPERCICLO R4 (Luz) ----------
if (modoR4 == SUPERCICLO) {

  // Obtener el tiempo actual en minutos desde época Unix
  unsigned long tiempoActual = rtc.now().unixtime() / 60;

  // --------- Inicialización tras reset / cambio de modo ---------
  if (!superR4_Inicializado) {
    if (R4estado == HIGH) {  // Estamos en luz
      tiempoProxApagado = tiempoActual + horasLuz;
    } else {  // Estamos en oscuridad
      tiempoProxEncendido = tiempoActual + horasOscuridad;
    }

    superR4_Inicializado = true;
    Guardado_General();
  }

  // --------- Cambio de estado automático ---------
  if (R4estado == LOW && tiempoActual >= tiempoProxEncendido) {
    // Entramos en período de luz
    digitalWrite(RELAY4, HIGH);
    R4estado = HIGH;

    tiempoProxApagado = tiempoActual + horasLuz;

    // Mostrar estimación al usuario
    horaOffR4 = (tiempoProxApagado % 1440) / 60;
    minOffR4  = (tiempoProxApagado % 1440) % 60;

    Guardado_General();
  }

  else if (R4estado == HIGH && tiempoActual >= tiempoProxApagado) {
    // Entramos en período de oscuridad
    digitalWrite(RELAY4, LOW);
    R4estado = LOW;

    tiempoProxEncendido = tiempoActual + horasOscuridad;

    // Mostrar estimación al usuario
    horaOnR4 = (tiempoProxEncendido % 1440) / 60;
    minOnR4  = (tiempoProxEncendido % 1440) % 60;

    Guardado_General();
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
  minR1 = EEPROM.get(0, minR1);
  maxR1 = EEPROM.get(4, maxR1);
  minR2 = EEPROM.get(8, minR2);
  maxR2 = EEPROM.get(12, maxR2);
  paramR1 = EEPROM.get(16, paramR1);
  paramR2 = EEPROM.get(17, paramR2);
  modoR1 = EEPROM.get(18, modoR1);
  modoR2 = EEPROM.get(19, modoR2);
  modoR3 = EEPROM.get(20, modoR3);
  modoR4 = EEPROM.get(25, modoR4);
  int u = 0;
  int h = 0;
  for (u = 0; u < 7; u++) {
    h = 30 + u;
    diasRiego[u] = EEPROM.get(h, diasRiego[u]);
  }

  ssid = readStringFromEEPROM(37);
  password = readStringFromEEPROM(87);
  estadoR1 = EEPROM.get(137, estadoR1);
  estadoR2 = EEPROM.get(138, estadoR2);
  estadoR3 = EEPROM.get(139, estadoR3);
  estadoR4 = EEPROM.get(140, estadoR4);
  horaOnR3 = EEPROM.get(141, horaOnR3);
  minOnR3 = EEPROM.get(145, minOnR3);
  horaOffR3 = EEPROM.get(149, horaOffR3);
  minOffR3 = EEPROM.get(153, minOffR3);
  horaOnR4 = EEPROM.get(158, horaOnR4);
  minOnR4 = EEPROM.get(162, minOnR4);
  horaOffR4 = EEPROM.get(166, horaOffR4);
  minOffR4 = EEPROM.get(170, minOffR4);
  chat_id = EEPROM.get(215, chat_id);
  minR3 = EEPROM.get(240, minR3);
  maxR3 = EEPROM.get(245, maxR3);
  //minR2ir = EEPROM.get(250, minR2ir);
  //maxR2ir = EEPROM.get(255, maxR2ir);
  //paramR2ir = EEPROM.get(260, paramR2ir);
  //estadoR2ir = EEPROM.get(265, estadoR2ir);
  //modoR2ir = EEPROM.get(270, modoR2ir);
  //R2irestado = EEPROM.get(272, R2irestado);
  horaOnR1 = EEPROM.get(276, horaOnR1);
  horaOffR1 = EEPROM.get(280, horaOffR1);
  minOnR1 = EEPROM.get(284, minOnR1);
  minOffR1 = EEPROM.get(288, minOffR1);
  tiempoRiego = EEPROM.get(292, tiempoRiego);
  tiempoNoRiego = EEPROM.get(296, tiempoNoRiego);
  direccionR1 = EEPROM.get(300, direccionR1);
  currentPosition = EEPROM.get(304, currentPosition);
  horaAmanecer = EEPROM.get(308, horaAmanecer);
  horaAtardecer = EEPROM.get(312, horaAtardecer);
  modoWiFi = EEPROM.get(316, modoWiFi);
  R1name = EEPROM.get(320, R1name);
  //intervaloDatos = EEPROM.get(324, intervaloDatos);
  //luzEncendida = EEPROM.get(328, luzEncendida);
  minTR2 = EEPROM.get(330, minTR2);
  maxTR2 = EEPROM.get(334, maxTR2);
  //proximoEncendidoR4 = EEPROM.get(338, proximoEncendidoR4);
  //proximoApagadoR4 = EEPROM.get(342, proximoApagadoR4);
  horasLuz = EEPROM.get(346, horasLuz);
  horasOscuridad = EEPROM.get(350, horasOscuridad);
  tiempoGoogle = EEPROM.get(354, tiempoGoogle);
  tiempoTelegram = EEPROM.get(358, tiempoTelegram);
  cantidadRiegos = EEPROM.get(362, cantidadRiegos);
  unidadRiego = EEPROM.get(366, unidadRiego);
  unidadNoRiego = EEPROM.get(370, unidadNoRiego);

  /*for (int i = 0; i < 100; i++) {
    rawData[i] = EEPROM.get(350 + i * sizeof(uint16_t), rawData[i]);
  }*/

  Serial.println("Carga completa");
}

void Guardado_General() {
  Serial.println("Guardando en memoria..");
  EEPROM.put(0, minR1);
  EEPROM.put(4, maxR1);
  EEPROM.put(8, minR2);
  EEPROM.put(12, maxR2);
  EEPROM.put(16, paramR1);
  EEPROM.put(17, paramR2);
  EEPROM.put(18, modoR1);
  EEPROM.put(19, modoR2);
  EEPROM.put(20, modoR3);
  EEPROM.put(21, timeOnR3);
  EEPROM.put(23, timeOffR3);
  EEPROM.put(25, modoR4);
  EEPROM.put(26, timeOnR4);
  EEPROM.put(28, timeOffR4);
  int y = 0;
  int p = 0;
  for (y = 0; y < 7; y++) {
    p = 30 + y;
    EEPROM.put(p, diasRiego[y]);
  }

  writeStringToEEPROM(37, ssid);
  writeStringToEEPROM(87, password);
  EEPROM.put(137, estadoR1);
  EEPROM.put(138, estadoR2);
  EEPROM.put(139, estadoR3);
  EEPROM.put(140, estadoR4);
  EEPROM.put(141, horaOnR3);
  EEPROM.put(145, minOnR3);
  EEPROM.put(149, horaOffR3);
  EEPROM.put(153, minOffR3);
  EEPROM.put(158, horaOnR4);
  EEPROM.put(162, minOnR4);
  EEPROM.put(166, horaOffR4);
  EEPROM.put(170, minOffR4);
  EEPROM.put(215, chat_id);
  EEPROM.put(240, minR3);
  EEPROM.put(245, maxR3);
  //EEPROM.put(250, minR2ir);
  //EEPROM.put(255, maxR2ir);
  //EEPROM.put(260, paramR2ir);
  //EEPROM.put(265, estadoR2ir);
  //EEPROM.put(270, modoR2ir);
  //EEPROM.put(272, R2irestado);
  EEPROM.put(276, horaOnR1);
  EEPROM.put(280, horaOffR1);
  EEPROM.put(284, minOnR1);
  EEPROM.put(288, minOffR1);
  EEPROM.put(292, tiempoRiego);
  EEPROM.put(296, tiempoNoRiego);
  EEPROM.put(300, direccionR1);
  EEPROM.put(304, currentPosition);
  EEPROM.put(308, horaAmanecer);
  EEPROM.put(312, horaAtardecer);
  EEPROM.put(316, modoWiFi);
  EEPROM.put(320, R1name);
  //EEPROM.put(324, intervaloDatos);
  //EEPROM.put(328, luzEncendida);
  EEPROM.put(330, minTR2);
  EEPROM.put(334, maxTR2);
  //EEPROM.put(338, proximoEncendidoR4);
  //EEPROM.put(342, proximoApagadoR4);
  EEPROM.put(346, horasLuz);
  EEPROM.put(350, horasOscuridad);
  EEPROM.put(354, tiempoGoogle);
  EEPROM.put(358, tiempoTelegram);
  EEPROM.put(362, cantidadRiegos);
  EEPROM.put(366, unidadRiego);
  EEPROM.put(370, unidadNoRiego);


  
  /*for (int i = 0; i < 100; i++) {
    EEPROM.put(380 + i * sizeof(uint16_t), rawData[i]);
  }*/
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
  Serial.print("Conectando a WiFi: ");
  Serial.println(ssid);

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("Ya conectado a WiFi.");
    return;
  }

  WiFi.mode(WIFI_STA);
  WiFi.disconnect(true);
  delay(100);

  if (conPW == 1) {
    WiFi.begin(ssid, password);
  } else {
    WiFi.begin(ssid);
  }

  unsigned long startAttemptTime = millis();
  const unsigned long timeoutInitial = 15000;
  const unsigned long timeoutRetry = 10000;
  const int maxRetries = 4;

  bool isConnected = false;

  while (millis() - startAttemptTime < timeoutInitial) {
    if (WiFi.status() == WL_CONNECTED) {
      isConnected = true;
      break;
    }
    delay(200);
    Serial.print(".");
  }

  int retryCount = 0;
  while (!isConnected && retryCount < maxRetries) {
    Serial.print("\nIntento reconectar WiFi #");
    Serial.println(retryCount + 1);

    WiFi.disconnect(true);
    delay(100);

    if (conPW == 1) {
      WiFi.begin(ssid, password);
    } else {
      WiFi.begin(ssid);
    }

    startAttemptTime = millis();
    while (millis() - startAttemptTime < timeoutRetry) {
      if (WiFi.status() == WL_CONNECTED) {
        isConnected = true;
        break;
      }
      delay(200);
      Serial.print(".");
    }
    retryCount++;
  }

  if (isConnected) {
    Serial.println("\nWiFi conectado.");
    Serial.print("IP: ");
    Serial.println(WiFi.localIP());

    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    //display.setTextColor(SH110X_WHITE); // ✅ Correcta para Adafruit_SH110X
    display.setCursor(16, 24);
    display.println("WiFi conectado");
    display.display();
    delay(1500);

    display.clearDisplay();
    display.setCursor(12, 32);
    display.print("IP: ");
    display.println(WiFi.localIP());
    display.display();
    delay(1500);
    display.clearDisplay();

  } else {
    Serial.println("\nNo se pudo conectar a WiFi tras múltiples intentos.");
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    //display.setTextColor(SH110X_WHITE); // ✅ Correcta para Adafruit_SH110X
    display.setCursor(0, 0);
    display.println("WiFi AP");
    display.print("IP: 192.168.4.1");
    display.display();

    startAccessPoint();  // Asegurate de que esta función esté bien implementada
  }
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

void checkWiFiConnection() {

  const unsigned long RETRY_INTERVAL = 5UL * 60UL * 1000UL;   // 5 min
  const unsigned long CONNECT_TIMEOUT = 10UL * 1000UL;        // 10 s

  // Si ya estamos conectados
  if (WiFi.status() == WL_CONNECTED) {
    if (apMode) {
      Serial.println("Conexión restablecida. Cerrando modo AP...");
      WiFi.softAPdisconnect(true);
      WiFi.mode(WIFI_STA);
      delay(100);
      apMode = false;
      startWebServer();
    } else {
      Serial.println("WiFi OK.");
    }
    return;
  }

  // Si NO estamos conectados y NO en modo AP, intentamos conectar
  if (!apMode) {
    Serial.println("WiFi desconectado. Intentando conexión...");

    WiFi.disconnect(true);
    delay(100);
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid.c_str(), password.c_str());

    unsigned long t0 = millis();
    while (millis() - t0 < CONNECT_TIMEOUT) {
      if (WiFi.status() == WL_CONNECTED) {
        Serial.println("¡Conectado!");
        startWebServer();
        return;
      }
      delay(200);
    }

    // No se pudo conectar -> activar modo AP
    Serial.println("No se pudo conectar. Activando modo AP...");
    startAccessPoint();
    startWebServer();
    apMode = true;
    lastRetryTime = millis();
  }

  // Si estamos en modo AP, reintentamos conexión cada X minutos
  if (apMode && millis() - lastRetryTime > RETRY_INTERVAL) {
    Serial.println("Reintentando conexión desde modo AP...");
    lastRetryTime = millis();

    WiFi.softAPdisconnect(true);
    delay(100);
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid.c_str(), password.c_str());

    unsigned long t0 = millis();
    while (millis() - t0 < CONNECT_TIMEOUT) {
      if (WiFi.status() == WL_CONNECTED) {
        Serial.println("¡Conectado desde AP!");
        apMode = false;
        startWebServer();
        return;
      }
      delay(200);
    }

    // Si aún no conecta, vuelve a AP
    Serial.println("Sigue sin conexión. Vuelve al modo AP.");
    startAccessPoint();
    startWebServer();
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
    server.on("/controlR1", handleControlR1);
    server.on("/controlR1On", handleControlR1On);
    server.on("/controlR1Off", handleControlR1Off);
    server.on("/controlR1Auto", handleControlR1Auto);
    server.on("/controlR2", handleControlR2);
    server.on("/controlR2On", handleControlR2On);
    server.on("/controlR2Off", handleControlR2Off);
    server.on("/controlR2Auto", handleControlR2Auto);
    server.on("/controlR3", handleControlR3);
    server.on("/controlR3On", handleControlR3On);
    server.on("/controlR3Off", handleControlR3Off);
    server.on("/controlR3Auto", handleControlR3Auto);
    server.on("/controlR3OnFor", handleControlR3OnFor);
    server.on("/controlR4", handleControlR4);
    server.on("/controlR4On", handleControlR4On);
    server.on("/controlR4Off", handleControlR4Off);
    server.on("/controlR4Auto", handleControlR4Auto);
    server.on("/controlR4Superciclo", handleControlR4Superciclo);
    server.on("/controlR4Nube", HTTP_POST, handleControlR4Nube);
    server.on("/controlR4Mediodia", HTTP_POST, handleControlR4Mediodia);
    server.on("/configR1", handleConfigR1);
    server.on("/configR2", handleConfigR2);
    server.on("/configR3", handleConfigR3);
    server.on("/configR4", handleConfigR4);
    server.on("/configWiFi", handleConfigWiFi);
    server.on("/saveConfigR1", saveConfigR1);
    server.on("/saveConfigR2", saveConfigR2);
    server.on("/saveConfigR3", saveConfigR3);
    server.on("/saveConfigR4", saveConfigR4);
    server.on("/saveConfigWiFi", saveConfigWiFi);
    server.on("/connectWiFi", connectWiFi);
    server.on("/disconnectWiFi", HTTP_POST, []() {
  modoWiFi = 0;
  Guardado_General();
  WiFi.disconnect(true);   // corta y borra configuración actual de conexión
  Serial.println("WiFi desconectado. modoWiFi = 0");
  server.sendHeader("Location", "/", true);
  server.send(302, "text/plain", "");
});

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
    // Obtener datos del sensor
    sensors_event_t humidity, temp;
    aht.getEvent(&humidity, &temp);

    float temperature = temp.temperature;
    float humedad = humidity.relative_humidity;

    DateTime now = rtc.now();
    int horaBot = now.hour();

    // Ajustar la hora si es necesario
    horaBot -= 3;
    if (horaBot < 0)
        horaBot = 24 + horaBot;

    // Construir fecha y hora
    String dateTime = String(now.day()) + "/" + String(now.month()) + "/" + String(now.year()) + " ";
    dateTime += (horaBot < 10 ? "0" : "") + String(horaBot) + ":";
    dateTime += (now.minute() < 10 ? "0" : "") + String(now.minute()) + ":";
    dateTime += (now.second() < 10 ? "0" : "") + String(now.second());

    // Construir mensaje de estado con IDs
    String statusMessage = "<div class='line' id='temp'>Temp: " + String(temperature, 1) + " C</div>";
    statusMessage += "<div class='line' id='hum'>Hum: " + String(humedad, 1) + " %</div>";
    statusMessage += "<div class='line' id='dpv'>VPD: " + String(DPV, 1) + " hPa</div>";
    statusMessage += "<div class='line' id='fechaHora' data-hora='" + String(horaBot) + ":" + String(now.minute()) + ":" + String(now.second()) + "'>" + dateTime + "</div>";

    // HTML completo
    String html = "<!DOCTYPE html><html lang='es'><head><meta charset='UTF-8'/><meta name='viewport' content='width=device-width, initial-scale=1'/>";
    html += "<title>Estado General</title>";
    html += "<link href='https://fonts.googleapis.com/css2?family=Press+Start+2P&display=swap' rel='stylesheet'>";
    html += "<style>";
    html += "body { margin: 0; padding: 0; font-family: 'Press Start 2P', monospace; background: linear-gradient(to bottom, #0a0f1e, #111927); color: #e0e0e0; display: flex; flex-direction: column; align-items: center; min-height: 100vh; }";
    html += ".cloud { position: absolute; top: -40px; left: 50%; transform: translateX(-50%); width: 300px; height: 150px; background: radial-gradient(ellipse at center, rgba(0,240,255,0.2), transparent 70%); border-radius: 50%; filter: blur(40px); animation: pulse 6s ease-in-out infinite; z-index: -1; }";
    html += "@keyframes pulse { 0%, 100% { transform: translateX(-50%) scale(1); opacity: 0.3; } 50% { transform: translateX(-50%) scale(1.1); opacity: 0.5; } }";
    html += ".logo-container { margin-top: 40px; position: relative; text-align: center; animation: fadeIn 1.5s ease-out; }";
    html += ".logo-text { font-size: 3.5rem; font-weight: bold; line-height: 1.2; color: #00f0ff; text-shadow: 0 0 20px #00f0ff, 0 0 40px #00f0ff; animation: glow 3s infinite alternate; }";
    html += "@keyframes glow { from { text-shadow: 0 0 10px #00f0ff, 0 0 20px #00f0ff; } to { text-shadow: 0 0 25px #00f0ff, 0 0 50px #00f0ff; } }";
    html += ".glitch { position: relative; display: inline-block; }";
    html += ".glitch::before, .glitch::after { content: attr(data-text); position: absolute; left: 0; top: 0; width: 100%; overflow: hidden; color: #00f0ff; background: transparent; clip: rect(0, 0, 0, 0); }";
    html += ".glitch::before { text-shadow: 2px 0 red; animation: glitchTop 2s infinite linear alternate-reverse; }";
    html += ".glitch::after { text-shadow: -2px 0 blue; animation: glitchBottom 2s infinite linear alternate-reverse; }";
    html += "@keyframes glitchTop { 0% { clip: rect(0, 9999px, 0, 0); } 5% { clip: rect(0, 9999px, 20px, 0); transform: translate(-2px, -2px); } 10% { clip: rect(0, 9999px, 10px, 0); transform: translate(2px, 0); } 15% { clip: rect(0, 9999px, 5px, 0); transform: translate(-1px, 1px); } 20%, 100% { clip: rect(0, 0, 0, 0); transform: translate(0, 0); } }";
    html += "@keyframes glitchBottom { 0% { clip: rect(0, 0, 0, 0); } 5% { clip: rect(20px, 9999px, 40px, 0); transform: translate(1px, 1px); } 10% { clip: rect(10px, 9999px, 30px, 0); transform: translate(-1px, -1px); } 15% { clip: rect(5px, 9999px, 25px, 0); transform: translate(1px, -1px); } 20%, 100% { clip: rect(0, 0, 0, 0); transform: translate(0, 0); } }";
    html += ".container { margin-top: 60px; background-color: #1e293b; border: 1px solid #00f0ff; border-radius: 20px; padding: 40px; width: fit-content; max-width: 90vw; box-shadow: 0 0 20px rgba(0, 240, 255, 0.2); animation: fadeInUp 1s ease-out; text-align: center; position: relative; margin-left: auto; margin-right: auto; }";
    html += ".info-box { margin-bottom: 30px; text-align: center; font-size: 0.9rem; color: #00f0ff; }";
    html += ".info-box .line { margin: 10px 0; }";
    html += ".button-group-vertical { display: flex; flex-direction: column; align-items: center; gap: 20px; margin-top: 30px; }";
    html += "button { background-color: #00f0ff; color: #0f172a; border: none; padding: 14px; font-size: 0.7rem; font-weight: bold; cursor: pointer; border-radius: 8px; transition: background-color 0.3s ease, transform 0.2s; font-family: 'Press Start 2P', monospace; width: 200px; }";
    html += "button:hover { background-color: #00c0dd; transform: scale(1.05); }";
    html += "footer { margin-top: auto; padding: 20px; font-size: 0.5rem; text-align: center; color: #888; }";
    html += "@keyframes fadeIn { from { opacity: 0; transform: translateY(-20px); } to { opacity: 1; transform: translateY(0); } }";
    html += "@keyframes fadeInUp { from { opacity: 0; transform: translateY(40px); } to { opacity: 1; transform: translateY(0); } }";
    html += "</style></head><body>";
    html += "<div class='cloud'></div>";
    html += "<div class='logo-container'><h1 class='logo-text glitch' data-text='DRUIDA BOT'>DRUIDA<br>BOT</h1></div>";
    html += "<div class='container'>";
    html += "<div class='info-box'>";
    html += statusMessage;
    html += "</div>";
    html += "<div class='button-group-vertical'>";
    html += "<a href='/control'><button>CONTROL</button></a>";
    html += "<a href='/config'><button>CONFIG</button></a>";
    html += "</div>";
    html += "</div>";
    html += "<footer><p>druidadata@gmail.com<br>DataDruida</p></footer>";

    // Script para actualizar la hora en vivo
    html += "<script>";
    html += "function startClock() {";
    html += "  const div = document.getElementById('fechaHora');";
    html += "  let [h, m, s] = div.getAttribute('data-hora').split(':').map(Number);";
    html += "  setInterval(() => {";
    html += "    s++;";
    html += "    if (s >= 60) { s = 0; m++; }";
    html += "    if (m >= 60) { m = 0; h++; }";
    html += "    if (h >= 24) h = 0;";
    html += "    const pad = (n) => n.toString().padStart(2, '0');";
    html += "    const now = new Date();";
    html += "    const fecha = pad(now.getDate()) + '/' + pad(now.getMonth() + 1) + '/' + now.getFullYear();";
    html += "    div.innerHTML = fecha + ' ' + pad(h) + ':' + pad(m) + ':' + pad(s);";
    html += "  }, 1000);";
    html += "}";
    html += "window.onload = startClock;";
    html += "</script>";

    html += "</body></html>";

    server.send(200, "text/html", html);
}







void handleControl() {
    String html = "<!DOCTYPE html><html lang='es'><head><meta charset='UTF-8'/><meta name='viewport' content='width=device-width, initial-scale=1'/>";
    html += "<title>Control</title>";
    html += "<link href='https://fonts.googleapis.com/css2?family=Press+Start+2P&display=swap' rel='stylesheet'>";
    html += "<style>";
    
    html += "body { margin: 0; font-family: 'Press Start 2P', monospace; background: linear-gradient(to bottom, #0a0f1e, #111927); color: #e0e0e0; display: flex; flex-direction: column; align-items: center; min-height: 100vh; }";
    
    html += ".cloud { position: absolute; top: -40px; left: 50%; transform: translateX(-50%); width: 300px; height: 150px; background: radial-gradient(ellipse at center, rgba(0,240,255,0.2), transparent 70%); border-radius: 50%; filter: blur(40px); animation: pulse 6s ease-in-out infinite; z-index: -1; }";
    
    html += "@keyframes pulse { 0%, 100% { transform: translateX(-50%) scale(1); opacity: 0.3; } 50% { transform: translateX(-50%) scale(1.1); opacity: 0.5; } }";

    html += ".logo-container { margin-top: 40px; position: relative; text-align: center; animation: fadeIn 1.5s ease-out; }";
    
    html += ".logo-text { font-size: 2.5rem; font-weight: bold; line-height: 1.2; color: #00f0ff; text-shadow: 0 0 20px #00f0ff, 0 0 40px #00f0ff; animation: glow 3s infinite alternate; }";
    
    html += "@keyframes glow { from { text-shadow: 0 0 10px #00f0ff, 0 0 20px #00f0ff; } to { text-shadow: 0 0 25px #00f0ff, 0 0 50px #00f0ff; } }";

    html += ".glitch { position: relative; display: inline-block; }";
    html += ".glitch::before, .glitch::after { content: attr(data-text); position: absolute; left: 0; top: 0; width: 100%; overflow: hidden; color: #00f0ff; background: transparent; clip: rect(0, 0, 0, 0); }";
    html += ".glitch::before { text-shadow: 2px 0 red; animation: glitchTop 2s infinite linear alternate-reverse; }";
    html += ".glitch::after { text-shadow: -2px 0 blue; animation: glitchBottom 2s infinite linear alternate-reverse; }";

    html += "@keyframes glitchTop { 0% { clip: rect(0, 9999px, 0, 0); } 5% { clip: rect(0, 9999px, 20px, 0); transform: translate(-2px, -2px); } 10% { clip: rect(0, 9999px, 10px, 0); transform: translate(2px, 0); } 15% { clip: rect(0, 9999px, 5px, 0); transform: translate(-1px, 1px); } 20%, 100% { clip: rect(0, 0, 0, 0); transform: translate(0, 0); } }";
    html += "@keyframes glitchBottom { 0% { clip: rect(0, 0, 0, 0); } 5% { clip: rect(20px, 9999px, 40px, 0); transform: translate(1px, 1px); } 10% { clip: rect(10px, 9999px, 30px, 0); transform: translate(-1px, -1px); } 15% { clip: rect(5px, 9999px, 25px, 0); transform: translate(1px, -1px); } 20%, 100% { clip: rect(0, 0, 0, 0); transform: translate(0, 0); } }";

    html += ".container { margin-top: 40px; background-color: #1e293b; border: 1px solid #00f0ff; border-radius: 20px; padding: 20px 30px; display: flex; flex-direction: column; align-items: center; box-shadow: 0 0 20px rgba(0, 240, 255, 0.2); animation: fadeInUp 1s ease-out; }";

    html += "h1 { font-size: 1rem; margin-bottom: 20px; color: #00f0ff; }";

    html += "a { text-decoration: none; margin: 8px 0; }";
    html += "button { background-color: #00f0ff; color: #0f172a; border: none; padding: 10px 20px; font-size: 0.6rem; font-weight: bold; cursor: pointer; border-radius: 6px; transition: background-color 0.3s ease, transform 0.2s; font-family: 'Press Start 2P', monospace; display: inline-block; width: auto; }";
    html += "button:hover { background-color: #00c0dd; transform: scale(1.05); }";

    html += "@keyframes fadeInUp { from { opacity: 0; transform: translateY(40px); } to { opacity: 1; transform: translateY(0); } }";
    html += "@keyframes fadeIn { from { opacity: 0; transform: translateY(-20px); } to { opacity: 1; transform: translateY(0); } }";

    html += "footer { margin-top: auto; padding: 20px; font-size: 0.4rem; text-align: center; color: #888; }";

    html += "</style></head><body>";

    html += "<div class='cloud'></div>";
    html += "<div class='logo-container'><h1 class='logo-text glitch' data-text='DRUIDA BOT'>DRUIDA<br>BOT</h1></div>";

    html += "<div class='container'>";
    html += "<h1>Control</h1>";
    html += "<a href='/controlR1'><button>" + getRelayName(R1name) + "</button></a>";
    html += "<a href='/controlR2'><button>" + getRelayName(R2name) + "</button></a>";
    html += "<a href='/controlR3'><button>" + getRelayName(R3name) + "</button></a>";
    html += "<a href='/controlR4'><button>" + getRelayName(R4name) + "</button></a>";
    html += "<a href='/'><button>Volver</button></a>";
    html += "</div>";

    html += "<footer><p>druidadata@gmail.com<br>DataDruida</p></footer>";
    html += "</body></html>";

    server.send(200, "text/html", html);
}







void handleConfirmation(const String& mensaje, const String& redireccion) {
    String html = "<!DOCTYPE html><html><head>";
    html += "<meta charset='UTF-8'>";
    html += "<meta http-equiv='refresh' content='3; url=" + redireccion + "'>";
    html += "<link href='https://fonts.googleapis.com/css2?family=Press+Start+2P&display=swap' rel='stylesheet'>";
    html += "<style>";
    html += "body { margin: 0; padding: 0; background-color: #0f172a; color: #00f0ff; font-family: 'Press Start 2P', monospace; display: flex; flex-direction: column; align-items: center; justify-content: center; height: 100vh; text-align: center; }";
    html += ".logo { font-size: 1.2rem; margin-bottom: 40px; animation: glitch 1s infinite; text-shadow: 0 0 5px #00f0ff, 0 0 10px #00f0ff; }";
    html += "@keyframes glitch { 0% { text-shadow: 2px 2px #ff00ff, -2px -2px #00ffff; } 20% { text-shadow: -2px 2px #ff00ff, 2px -2px #00ffff; } 40% { text-shadow: 2px -2px #ff00ff, -2px 2px #00ffff; } 60% { text-shadow: -2px -2px #ff00ff, 2px 2px #00ffff; } 80% { text-shadow: 2px 2px #ff00ff, -2px -2px #00ffff; } 100% { text-shadow: 0 0 5px #00f0ff; } }";
    html += ".message-box { background-color: #1e40af; border: 2px solid #00ffcc; border-radius: 12px; padding: 30px; box-shadow: 0 0 15px rgba(0,255,255,0.3); animation: fadeIn 1.5s ease-out; }";
    html += "h1 { font-size: 1rem; margin: 0; }";
    html += "@keyframes fadeIn { from { opacity: 0; transform: translateY(10px); } to { opacity: 1; transform: translateY(0); } }";
    html += "</style></head><body>";
    html += "<div class='logo'>DATA DRUIDA</div>";
    html += "<div class='message-box'><h1>" + mensaje + "</h1></div>";
    html += "<script>setTimeout(function(){ window.location.href='" + redireccion + "'; }, 3000);</script>";
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
    String html = "<!DOCTYPE html><html lang='es'><head><meta charset='UTF-8'/><meta name='viewport' content='width=device-width, initial-scale=1'/>";
    html += "<title>Control R1</title>";
    html += "<link href='https://fonts.googleapis.com/css2?family=Press+Start+2P&display=swap' rel='stylesheet'>";
    html += "<style>";

    html += "body { margin: 0; font-family: 'Press Start 2P', monospace; background: linear-gradient(to bottom, #0a0f1e, #111927); color: #e0e0e0; display: flex; flex-direction: column; align-items: center; min-height: 100vh; }";

    html += ".cloud { position: absolute; top: -40px; left: 50%; transform: translateX(-50%); width: 300px; height: 150px; background: radial-gradient(ellipse at center, rgba(0,240,255,0.2), transparent 70%); border-radius: 50%; filter: blur(40px); animation: pulse 6s ease-in-out infinite; z-index: -1; }";

    html += "@keyframes pulse { 0%, 100% { transform: translateX(-50%) scale(1); opacity: 0.3; } 50% { transform: translateX(-50%) scale(1.1); opacity: 0.5; } }";

    html += ".logo-container { margin-top: 40px; position: relative; text-align: center; animation: fadeIn 1.5s ease-out; }";

    html += ".logo-text { font-size: 2.5rem; font-weight: bold; line-height: 1.2; color: #00f0ff; text-shadow: 0 0 20px #00f0ff, 0 0 40px #00f0ff; animation: glow 3s infinite alternate; }";

    html += "@keyframes glow { from { text-shadow: 0 0 10px #00f0ff, 0 0 20px #00f0ff; } to { text-shadow: 0 0 25px #00f0ff, 0 0 50px #00f0ff; } }";

    html += ".glitch { position: relative; display: inline-block; }";
    html += ".glitch::before, .glitch::after { content: attr(data-text); position: absolute; left: 0; top: 0; width: 100%; overflow: hidden; color: #00f0ff; background: transparent; clip: rect(0, 0, 0, 0); }";
    html += ".glitch::before { text-shadow: 2px 0 red; animation: glitchTop 2s infinite linear alternate-reverse; }";
    html += ".glitch::after { text-shadow: -2px 0 blue; animation: glitchBottom 2s infinite linear alternate-reverse; }";

    html += "@keyframes glitchTop { 0% { clip: rect(0, 9999px, 0, 0); } 5% { clip: rect(0, 9999px, 20px, 0); transform: translate(-2px, -2px); } 10% { clip: rect(0, 9999px, 10px, 0); transform: translate(2px, 0); } 15% { clip: rect(0, 9999px, 5px, 0); transform: translate(-1px, 1px); } 20%, 100% { clip: rect(0, 0, 0, 0); transform: translate(0, 0); } }";
    html += "@keyframes glitchBottom { 0% { clip: rect(0, 0, 0, 0); } 5% { clip: rect(20px, 9999px, 40px, 0); transform: translate(1px, 1px); } 10% { clip: rect(10px, 9999px, 30px, 0); transform: translate(-1px, -1px); } 15% { clip: rect(5px, 9999px, 25px, 0); transform: translate(1px, -1px); } 20%, 100% { clip: rect(0, 0, 0, 0); transform: translate(0, 0); } }";

    html += ".container { margin-top: 40px; background-color: #1e293b; border: 1px solid #00f0ff; border-radius: 20px; padding: 15px 20px; display: flex; flex-direction: column; align-items: center; box-shadow: 0 0 20px rgba(0, 240, 255, 0.2); animation: fadeInUp 1s ease-out; width: 320px; gap: 10px; }";

    html += "h1 { font-size: 1rem; margin: 0; padding: 0; color: #00f0ff; line-height: 1.3; text-align: center; }";

    html += "form { margin: 0; width: 100%; }";

    html += "input[type='submit'], button { background-color: #00f0ff; color: #0f172a; border: none; padding: 12px 0; font-size: 0.6rem; font-weight: bold; cursor: pointer; border-radius: 6px; transition: background-color 0.3s ease, transform 0.2s; font-family: 'Press Start 2P', monospace; display: block; width: 100%; margin: 8px 0; }";
    html += "input[type='submit']:hover, button:hover { background-color: #00c0dd; transform: scale(1.05); }";

    html += "a { text-decoration: none; width: 100%; }";

    html += "@keyframes fadeInUp { from { opacity: 0; transform: translateY(40px); } to { opacity: 1; transform: translateY(0); } }";
    html += "@keyframes fadeIn { from { opacity: 0; transform: translateY(-20px); } to { opacity: 1; transform: translateY(0); } }";

    html += "footer { margin-top: auto; padding: 20px; font-size: 0.4rem; text-align: center; color: #888; }";

    html += "</style></head><body>";

    html += "<div class='cloud'></div>";
    html += "<div class='logo-container'><h1 class='logo-text glitch' data-text='DATA DRUIDA'>DATA<br>DRUIDA</h1></div>";

    html += "<div class='container'>";
    html += "<h1>Control de " + getRelayName(R1name) + "</h1>";

    html += "<form action='/controlR1On' method='POST'><input type='submit' value='Encender'></form>";
    html += "<form action='/controlR1Off' method='POST'><input type='submit' value='Apagar'></form>";
    html += "<form action='/controlR1Auto' method='POST'><input type='submit' value='Automatico'></form>";
    html += "<a href='/control'><button>Volver</button></a>";

    html += "</div>";

    html += "<footer><p>bmurphy1.618@gmail.com<br>BmuRphY</p></footer>";

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



void handleControlR2() {
    String html = "<!DOCTYPE html><html lang='es'><head><meta charset='UTF-8'/><meta name='viewport' content='width=device-width, initial-scale=1'/>";
    html += "<title>Control R2</title>";
    html += "<link href='https://fonts.googleapis.com/css2?family=Press+Start+2P&display=swap' rel='stylesheet'>";
    html += "<style>";

    html += "body { margin: 0; font-family: 'Press Start 2P', monospace; background: linear-gradient(to bottom, #0a0f1e, #111927); color: #e0e0e0; display: flex; flex-direction: column; align-items: center; min-height: 100vh; }";

    html += ".cloud { position: absolute; top: -40px; left: 50%; transform: translateX(-50%); width: 300px; height: 150px; background: radial-gradient(ellipse at center, rgba(0,240,255,0.2), transparent 70%); border-radius: 50%; filter: blur(40px); animation: pulse 6s ease-in-out infinite; z-index: -1; }";

    html += "@keyframes pulse { 0%, 100% { transform: translateX(-50%) scale(1); opacity: 0.3; } 50% { transform: translateX(-50%) scale(1.1); opacity: 0.5; } }";

    html += ".logo-container { margin-top: 40px; position: relative; text-align: center; animation: fadeIn 1.5s ease-out; }";

    html += ".logo-text { font-size: 2.5rem; font-weight: bold; line-height: 1.2; color: #00f0ff; text-shadow: 0 0 20px #00f0ff, 0 0 40px #00f0ff; animation: glow 3s infinite alternate; }";

    html += "@keyframes glow { from { text-shadow: 0 0 10px #00f0ff, 0 0 20px #00f0ff; } to { text-shadow: 0 0 25px #00f0ff, 0 0 50px #00f0ff; } }";

    html += ".glitch { position: relative; display: inline-block; }";
    html += ".glitch::before, .glitch::after { content: attr(data-text); position: absolute; left: 0; top: 0; width: 100%; overflow: hidden; color: #00f0ff; background: transparent; clip: rect(0, 0, 0, 0); }";
    html += ".glitch::before { text-shadow: 2px 0 red; animation: glitchTop 2s infinite linear alternate-reverse; }";
    html += ".glitch::after { text-shadow: -2px 0 blue; animation: glitchBottom 2s infinite linear alternate-reverse; }";

    html += "@keyframes glitchTop { 0% { clip: rect(0, 9999px, 0, 0); } 5% { clip: rect(0, 9999px, 20px, 0); transform: translate(-2px, -2px); } 10% { clip: rect(0, 9999px, 10px, 0); transform: translate(2px, 0); } 15% { clip: rect(0, 9999px, 5px, 0); transform: translate(-1px, 1px); } 20%, 100% { clip: rect(0, 0, 0, 0); transform: translate(0, 0); } }";
    html += "@keyframes glitchBottom { 0% { clip: rect(0, 0, 0, 0); } 5% { clip: rect(20px, 9999px, 40px, 0); transform: translate(1px, 1px); } 10% { clip: rect(10px, 9999px, 30px, 0); transform: translate(-1px, -1px); } 15% { clip: rect(5px, 9999px, 25px, 0); transform: translate(1px, -1px); } 20%, 100% { clip: rect(0, 0, 0, 0); transform: translate(0, 0); } }";

    html += ".container { margin-top: 40px; background-color: #1e293b; border: 1px solid #00f0ff; border-radius: 20px; padding: 15px 20px; display: flex; flex-direction: column; align-items: center; box-shadow: 0 0 20px rgba(0, 240, 255, 0.2); animation: fadeInUp 1s ease-out; width: 320px; gap: 10px; }";

    html += "h1 { font-size: 1rem; margin: 0; padding: 0; color: #00f0ff; line-height: 1.3; text-align: center; }";

    html += "form { margin: 0; width: 100%; }";

    html += "input[type='submit'], button { background-color: #00f0ff; color: #0f172a; border: none; padding: 12px 0; font-size: 0.6rem; font-weight: bold; cursor: pointer; border-radius: 6px; transition: background-color 0.3s ease, transform 0.2s; font-family: 'Press Start 2P', monospace; display: block; width: 100%; margin: 8px 0; }";
    html += "input[type='submit']:hover, button:hover { background-color: #00c0dd; transform: scale(1.05); }";

    html += "a { text-decoration: none; width: 100%; }";

    html += "@keyframes fadeInUp { from { opacity: 0; transform: translateY(40px); } to { opacity: 1; transform: translateY(0); } }";
    html += "@keyframes fadeIn { from { opacity: 0; transform: translateY(-20px); } to { opacity: 1; transform: translateY(0); } }";

    html += "footer { margin-top: auto; padding: 20px; font-size: 0.4rem; text-align: center; color: #888; }";

    html += "</style></head><body>";

    html += "<div class='cloud'></div>";
    html += "<div class='logo-container'><h1 class='logo-text glitch' data-text='DATA DRUIDA'>DATA<br>DRUIDA</h1></div>";

    html += "<div class='container'>";
    html += "<h1>Control de " + getRelayName(R2name) + "</h1>";

    html += "<form action='/controlR2On' method='POST'><input type='submit' value='Encender'></form>";
    html += "<form action='/controlR2Off' method='POST'><input type='submit' value='Apagar'></form>";
    html += "<form action='/controlR2Auto' method='POST'><input type='submit' value='Automatico'></form>";
    html += "<a href='/control'><button>Volver</button></a>";

    html += "</div>";

    html += "<footer><p>bmurphy1.618@gmail.com<br>BmuRphY</p></footer>";

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
    String html = "<!DOCTYPE html><html lang='es'><head><meta charset='UTF-8'/><meta name='viewport' content='width=device-width, initial-scale=1'/>";
    html += "<title>Control R3</title>";
    html += "<link href='https://fonts.googleapis.com/css2?family=Press+Start+2P&display=swap' rel='stylesheet'>";
    html += "<style>";

    html += "body { margin: 0; font-family: 'Press Start 2P', monospace; background: linear-gradient(to bottom, #0a0f1e, #111927); color: #e0e0e0; display: flex; flex-direction: column; align-items: center; min-height: 100vh; }";

    html += ".cloud { position: absolute; top: -40px; left: 50%; transform: translateX(-50%); width: 300px; height: 150px; background: radial-gradient(ellipse at center, rgba(0,240,255,0.2), transparent 70%); border-radius: 50%; filter: blur(40px); animation: pulse 6s ease-in-out infinite; z-index: -1; }";

    html += "@keyframes pulse { 0%, 100% { transform: translateX(-50%) scale(1); opacity: 0.3; } 50% { transform: translateX(-50%) scale(1.1); opacity: 0.5; } }";

    html += ".logo-container { margin-top: 40px; position: relative; text-align: center; animation: fadeIn 1.5s ease-out; }";

    html += ".logo-text { font-size: 2.5rem; font-weight: bold; line-height: 1.2; color: #00f0ff; text-shadow: 0 0 20px #00f0ff, 0 0 40px #00f0ff; animation: glow 3s infinite alternate; }";

    html += "@keyframes glow { from { text-shadow: 0 0 10px #00f0ff, 0 0 20px #00f0ff; } to { text-shadow: 0 0 25px #00f0ff, 0 0 50px #00f0ff; } }";

    html += ".glitch { position: relative; display: inline-block; }";
    html += ".glitch::before, .glitch::after { content: attr(data-text); position: absolute; left: 0; top: 0; width: 100%; overflow: hidden; color: #00f0ff; background: transparent; clip: rect(0, 0, 0, 0); }";
    html += ".glitch::before { text-shadow: 2px 0 red; animation: glitchTop 2s infinite linear alternate-reverse; }";
    html += ".glitch::after { text-shadow: -2px 0 blue; animation: glitchBottom 2s infinite linear alternate-reverse; }";

    html += "@keyframes glitchTop { 0% { clip: rect(0, 9999px, 0, 0); } 5% { clip: rect(0, 9999px, 20px, 0); transform: translate(-2px, -2px); } 10% { clip: rect(0, 9999px, 10px, 0); transform: translate(2px, 0); } 15% { clip: rect(0, 9999px, 5px, 0); transform: translate(-1px, 1px); } 20%, 100% { clip: rect(0, 0, 0, 0); transform: translate(0, 0); } }";
    html += "@keyframes glitchBottom { 0% { clip: rect(0, 0, 0, 0); } 5% { clip: rect(20px, 9999px, 40px, 0); transform: translate(1px, 1px); } 10% { clip: rect(10px, 9999px, 30px, 0); transform: translate(-1px, -1px); } 15% { clip: rect(5px, 9999px, 25px, 0); transform: translate(1px, -1px); } 20%, 100% { clip: rect(0, 0, 0, 0); transform: translate(0, 0); } }";

    html += ".container { margin-top: 40px; background-color: #1e293b; border: 1px solid #00f0ff; border-radius: 20px; padding: 15px 20px; display: flex; flex-direction: column; align-items: center; box-shadow: 0 0 20px rgba(0, 240, 255, 0.2); animation: fadeInUp 1s ease-out; width: 340px; gap: 12px; }";

    html += "h1 { font-size: 1rem; margin: 0 0 10px 0; padding: 0; color: #00f0ff; line-height: 1.3; text-align: center; }";

    html += "form { margin: 0; width: 100%; display: flex; flex-direction: column; align-items: center; gap: 8px; }";

    html += "input[type='submit'], button { background-color: #00f0ff; color: #0f172a; border: none; padding: 12px 0; font-size: 0.6rem; font-weight: bold; cursor: pointer; border-radius: 6px; transition: background-color 0.3s ease, transform 0.2s; font-family: 'Press Start 2P', monospace; display: block; width: 100%; }";
    html += "input[type='submit']:hover, button:hover { background-color: #00c0dd; transform: scale(1.05); }";

    html += "input[type='number'] { padding: 8px 0; font-size: 0.7rem; width: 120px; text-align: center; border: 2px solid #00f0ff; border-radius: 8px; background: #0f172a; color: #e0e0e0; font-family: 'Press Start 2P', monospace; margin: 6px 0; }";

    html += "a { text-decoration: none; width: 100%; }";

    html += "@keyframes fadeInUp { from { opacity: 0; transform: translateY(40px); } to { opacity: 1; transform: translateY(0); } }";
    html += "@keyframes fadeIn { from { opacity: 0; transform: translateY(-20px); } to { opacity: 1; transform: translateY(0); } }";

    html += "footer { margin-top: auto; padding: 20px; font-size: 0.4rem; text-align: center; color: #888; }";

    html += "</style></head><body>";

    html += "<div class='cloud'></div>";
    html += "<div class='logo-container'><h1 class='logo-text glitch' data-text='DATA DRUIDA'>DATA<br>DRUIDA</h1></div>";

    html += "<div class='container'>";
    html += "<h1>Control de " + getRelayName(R3name) + "</h1>";

    html += "<form action='/controlR3On' method='POST'><input type='submit' value='Encender'></form>";
    html += "<form action='/controlR3Off' method='POST'><input type='submit' value='Apagar'></form>";
    html += "<form action='/controlR3Auto' method='POST'><input type='submit' value='Automatico'></form>";

    html += "<form action='/controlR3OnFor' method='POST'>";
    html += "Encender " + getRelayName(R3name) + " por <input type='number' name='duration' min='1' step='1' required> segundos";
    html += "<input type='submit' value='ON'>";
    html += "</form>";

    html += "<a href='/control'><button>Volver</button></a>";

    html += "</div>";

    html += "<footer><p>bmurphy1.618@gmail.com<br>BmuRphY</p></footer>";

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
    String html = "<!DOCTYPE html><html lang='es'><head><meta charset='UTF-8'/><meta name='viewport' content='width=device-width, initial-scale=1'/>";
    html += "<title>Control R4</title>";
    html += "<link href='https://fonts.googleapis.com/css2?family=Press+Start+2P&display=swap' rel='stylesheet'>";
    html += "<style>";

    html += "body { margin: 0; font-family: 'Press Start 2P', monospace; background: linear-gradient(to bottom, #0a0f1e, #111927); color: #e0e0e0; display: flex; flex-direction: column; align-items: center; min-height: 100vh; }";

    html += ".cloud { position: absolute; top: -40px; left: 50%; transform: translateX(-50%); width: 300px; height: 150px; background: radial-gradient(ellipse at center, rgba(0,240,255,0.2), transparent 70%); border-radius: 50%; filter: blur(40px); animation: pulse 6s ease-in-out infinite; z-index: -1; }";

    html += "@keyframes pulse { 0%, 100% { transform: translateX(-50%) scale(1); opacity: 0.3; } 50% { transform: translateX(-50%) scale(1.1); opacity: 0.5; } }";

    html += ".logo-container { margin-top: 40px; position: relative; text-align: center; animation: fadeIn 1.5s ease-out; }";

    html += ".logo-text { font-size: 2.5rem; font-weight: bold; line-height: 1.2; color: #00f0ff; text-shadow: 0 0 20px #00f0ff, 0 0 40px #00f0ff; animation: glow 3s infinite alternate; }";

    html += "@keyframes glow { from { text-shadow: 0 0 10px #00f0ff, 0 0 20px #00f0ff; } to { text-shadow: 0 0 25px #00f0ff, 0 0 50px #00f0ff; } }";

    html += ".glitch { position: relative; display: inline-block; }";
    html += ".glitch::before, .glitch::after { content: attr(data-text); position: absolute; left: 0; top: 0; width: 100%; overflow: hidden; color: #00f0ff; background: transparent; clip: rect(0, 0, 0, 0); }";
    html += ".glitch::before { text-shadow: 2px 0 red; animation: glitchTop 2s infinite linear alternate-reverse; }";
    html += ".glitch::after { text-shadow: -2px 0 blue; animation: glitchBottom 2s infinite linear alternate-reverse; }";

    html += "@keyframes glitchTop { 0% { clip: rect(0, 9999px, 0, 0); } 5% { clip: rect(0, 9999px, 20px, 0); transform: translate(-2px, -2px); } 10% { clip: rect(0, 9999px, 10px, 0); transform: translate(2px, 0); } 15% { clip: rect(0, 9999px, 5px, 0); transform: translate(-1px, 1px); } 20%, 100% { clip: rect(0, 0, 0, 0); transform: translate(0, 0); } }";
    html += "@keyframes glitchBottom { 0% { clip: rect(0, 0, 0, 0); } 5% { clip: rect(20px, 9999px, 40px, 0); transform: translate(1px, 1px); } 10% { clip: rect(10px, 9999px, 30px, 0); transform: translate(-1px, -1px); } 15% { clip: rect(5px, 9999px, 25px, 0); transform: translate(1px, -1px); } 20%, 100% { clip: rect(0, 0, 0, 0); transform: translate(0, 0); } }";

    html += ".container { margin-top: 40px; background-color: #1e293b; border: 1px solid #00f0ff; border-radius: 20px; padding: 15px 20px; display: flex; flex-direction: column; align-items: center; box-shadow: 0 0 20px rgba(0, 240, 255, 0.2); animation: fadeInUp 1s ease-out; width: 340px; gap: 12px; }";

    html += "h1 { font-size: 1rem; margin: 0 0 10px 0; padding: 0; color: #00f0ff; line-height: 1.3; text-align: center; }";

    html += "form { margin: 0; width: 100%; display: flex; flex-direction: column; align-items: center; gap: 8px; }";

    html += "input[type='submit'], button { background-color: #00f0ff; color: #0f172a; border: none; padding: 12px 0; font-size: 0.6rem; font-weight: bold; cursor: pointer; border-radius: 6px; transition: background-color 0.3s ease, transform 0.2s; font-family: 'Press Start 2P', monospace; display: block; width: 100%; }";
    html += "input[type='submit']:hover, button:hover { background-color: #00c0dd; transform: scale(1.05); }";

    html += "a { text-decoration: none; width: 100%; }";

    html += "@keyframes fadeInUp { from { opacity: 0; transform: translateY(40px); } to { opacity: 1; transform: translateY(0); } }";
    html += "@keyframes fadeIn { from { opacity: 0; transform: translateY(-20px); } to { opacity: 1; transform: translateY(0); } }";

    html += "footer { margin-top: auto; padding: 20px; font-size: 0.4rem; text-align: center; color: #888; }";

    html += "</style></head><body>";

    html += "<div class='cloud'></div>";
    html += "<div class='logo-container'><h1 class='logo-text glitch' data-text='DATA DRUIDA'>DATA<br>DRUIDA</h1></div>";

    html += "<div class='container'>";
    html += "<h1>Control de " + getRelayName(R4name) + "</h1>";

    html += "<form action='/controlR4On' method='POST'><input type='submit' value='Encender'></form>";
    html += "<form action='/controlR4Off' method='POST'><input type='submit' value='Apagar'></form>";
    html += "<form action='/controlR4Auto' method='POST'><input type='submit' value='Automatico'></form>";
    html += "<form action='/controlR4Superciclo' method='POST'><input type='submit' value='Superciclo'></form>";
    html += "<form action='/controlR4Nube' method='POST'><input type='submit' value='Nube'></form>";
    html += "<form action='/controlR4Mediodia' method='POST'><input type='submit' value='Medio Dia'></form>";

    html += "<a href='/control'><button>Volver</button></a>";

    html += "</div>";

    html += "<footer><p>bmurphy1.618@gmail.com<br>BmuRphY</p></footer>";

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






void handleConfig() {
    String html = "<!DOCTYPE html><html lang='es'><head><meta charset='UTF-8'/><meta name='viewport' content='width=device-width, initial-scale=1'/>";
    html += "<title>Configuración Druida BOT</title>";
    html += "<link href='https://fonts.googleapis.com/css2?family=Press+Start+2P&display=swap' rel='stylesheet'>";
    html += "<style>";
    
    // Body y fondo
    html += "body { margin: 0; font-family: 'Press Start 2P', monospace; background: linear-gradient(to bottom, #0a0f1e, #111927); color: #e0e0e0; display: flex; flex-direction: column; align-items: center; min-height: 100vh; }";

    // Nube animada
    html += ".cloud { position: absolute; top: -40px; left: 50%; transform: translateX(-50%); width: 300px; height: 150px; background: radial-gradient(ellipse at center, rgba(0,240,255,0.2), transparent 70%); border-radius: 50%; filter: blur(40px); animation: pulse 6s ease-in-out infinite; z-index: -1; }";
    html += "@keyframes pulse { 0%, 100% { transform: translateX(-50%) scale(1); opacity: 0.3; } 50% { transform: translateX(-50%) scale(1.1); opacity: 0.5; } }";

    // Logo principal con animación glitch
    html += ".logo-container { margin-top: 40px; position: relative; text-align: center; animation: fadeIn 1.5s ease-out; }";
    html += ".logo-text { font-size: 3rem; font-weight: bold; line-height: 1.2; color: #00f0ff; text-shadow: 0 0 20px #00f0ff, 0 0 40px #00f0ff; animation: glow 3s infinite alternate; }";
    html += "@keyframes glow { from { text-shadow: 0 0 10px #00f0ff, 0 0 20px #00f0ff; } to { text-shadow: 0 0 25px #00f0ff, 0 0 50px #00f0ff; } }";
    html += ".glitch { position: relative; display: inline-block; }";
    html += ".glitch::before, .glitch::after { content: attr(data-text); position: absolute; left: 0; top: 0; width: 100%; overflow: hidden; color: #00f0ff; background: transparent; clip: rect(0, 0, 0, 0); }";
    html += ".glitch::before { text-shadow: 2px 0 red; animation: glitchTop 2s infinite linear alternate-reverse; }";
    html += ".glitch::after { text-shadow: -2px 0 blue; animation: glitchBottom 2s infinite linear alternate-reverse; }";
    html += "@keyframes glitchTop { 0% { clip: rect(0, 9999px, 0, 0); } 5% { clip: rect(0, 9999px, 20px, 0); transform: translate(-2px, -2px); } 10% { clip: rect(0, 9999px, 10px, 0); transform: translate(2px, 0); } 15% { clip: rect(0, 9999px, 5px, 0); transform: translate(-1px, 1px); } 20%, 100% { clip: rect(0, 0, 0, 0); transform: translate(0, 0); } }";
    html += "@keyframes glitchBottom { 0% { clip: rect(0, 0, 0, 0); } 5% { clip: rect(20px, 9999px, 40px, 0); transform: translate(1px, 1px); } 10% { clip: rect(10px, 9999px, 30px, 0); transform: translate(-1px, -1px); } 15% { clip: rect(5px, 9999px, 25px, 0); transform: translate(1px, -1px); } 20%, 100% { clip: rect(0, 0, 0, 0); transform: translate(0, 0); } }";

    // Contenedor botones
    html += ".container { margin-top: 50px; background-color: #1e293b; border: 1px solid #00f0ff; border-radius: 20px; padding: 20px 25px; display: flex; flex-direction: column; align-items: center; width: 320px; gap: 15px; box-shadow: 0 0 25px rgba(0, 240, 255, 0.25); animation: fadeInUp 1s ease-out; }";

    html += "h1 { font-size: 1rem; margin: 0 0 15px 0; padding: 0; color: #00f0ff; line-height: 1.2; text-align: center; }";

    // Botones
    html += "a { width: 100%; text-decoration: none; }";
    html += "button { width: 100%; background-color: #00f0ff; color: #0f172a; border: none; padding: 12px 0; font-size: 0.6rem; font-weight: bold; cursor: pointer; border-radius: 6px; transition: background-color 0.3s ease, transform 0.2s; font-family: 'Press Start 2P', monospace; }";
    html += "button:hover { background-color: #00c0dd; transform: scale(1.05); }";

    html += "@keyframes fadeInUp { from { opacity: 0; transform: translateY(40px); } to { opacity: 1; transform: translateY(0); } }";
    html += "@keyframes fadeIn { from { opacity: 0; transform: translateY(-20px); } to { opacity: 1; transform: translateY(0); } }";

    html += "footer { margin-top: auto; padding: 15px; font-size: 0.4rem; text-align: center; color: #888; }";

    html += "</style></head><body>";

    html += "<div class='cloud'></div>";
    html += "<div class='logo-container'><h1 class='logo-text glitch' data-text='DATA DRUIDA'>DATA<br>DRUIDA</h1></div>";

    html += "<div class='container'>";
    html += "<h1>Configuración de Druida BOT</h1>";

    html += "<a href='/configR1'><button>" + getRelayName(R1name) + "</button></a>";
    html += "<a href='/configR2'><button>" + getRelayName(R2name) + "</button></a>";
    html += "<a href='/configR3'><button>" + getRelayName(R3name) + "</button></a>";
    html += "<a href='/configR4'><button>" + getRelayName(R4name) + "</button></a>";
    //html += "<a href='/configIR'><button>IR Config</button></a>";
    html += "<a href='/configWiFi'><button>WiFi</button></a>";
    html += "<a href='/'><button>Volver</button></a>";

    html += "</div>";

    html += "<footer><p>bmurphy1.618@gmail.com<br>BmuRphY</p></footer>";

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
  String html = "<!DOCTYPE html><html lang='es'><head><meta charset='UTF-8'/><meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<title>Configuración WiFi</title>";
  html += "<link href='https://fonts.googleapis.com/css2?family=Press+Start+2P&display=swap' rel='stylesheet'>";
  html += "<style>";

  html += "body { font-family: 'Press Start 2P', monospace; background: linear-gradient(to bottom, #0a0f1e, #111927); color: #e0e0e0; display: flex; flex-direction: column; align-items: center; justify-content: center; height: 100vh; margin: 0; padding: 10px; position: relative; }";
  html += ".fade-in { animation: fadeIn 1s ease-in-out both; }";
  html += "@keyframes fadeIn { 0% { opacity: 0; transform: translateY(20px); } 100% { opacity: 1; transform: translateY(0); } }";

  html += ".container { background-color: #1e293b; border: 1px solid #00f0ff; border-radius: 20px; padding: 20px; display: flex; flex-direction: column; align-items: center; box-shadow: 0 0 20px rgba(0,240,255,0.2); width: 90vw; max-width: 340px; gap: 12px; }";

  html += "h1 { font-size: 0.7rem; color: #00f0ff; text-align: center; position: relative; text-shadow: 0 0 2px #0ff, 0 0 4px #0ff; }";
  html += "h1::after { content: attr(data-text); position: absolute; left: 2px; text-shadow: -1px 0 red; animation: glitch 1s infinite; top: 0; }";
  html += "h1::before { content: attr(data-text); position: absolute; left: -2px; text-shadow: 1px 0 blue; animation: glitch 1s infinite; top: 0; }";
  html += "@keyframes glitch { 0% {clip: rect(0, 9999px, 0, 0);} 5% {clip: rect(0, 9999px, 5px, 0);} 10% {clip: rect(5px, 9999px, 10px, 0);} 15% {clip: rect(0, 9999px, 0, 0);} 100% {clip: rect(0, 9999px, 0, 0);} }";

  html += "label { font-size: 0.6rem; width: 100%; color: #e0e0e0; text-align: center; display: block; margin-top: 14px; margin-bottom: 6px; }";
  html += "input[type='text'], input[type='password'], input[type='number'] { font-family: 'Press Start 2P', monospace; font-size: 0.6rem; width: 100%; padding: 6px; border-radius: 8px; background: #0f172a; color: #e0e0e0; border: 2px solid #00f0ff; text-align: center; box-sizing: border-box; }";
  html += "select { font-family: 'Press Start 2P', monospace; font-size: 0.6rem; width: 100%; padding: 6px; border-radius: 8px; background: #0f172a; color: #e0e0e0; border: 2px solid #00f0ff; text-align: center; box-sizing: border-box; appearance: none; -webkit-appearance: none; -moz-appearance: none; }";

  html += ".button-container { display: flex; flex-direction: column; gap: 10px; width: 100%; margin-top: 12px; }";
  html += ".btn { font-family: 'Press Start 2P', monospace; font-size: 0.6rem; background-color: #00f0ff; color: #0f172a; border: 2px solid #00c0dd; padding: 10px; cursor: pointer; border-radius: 8px; width: 100%; text-align: center; transition: 0.3s; box-shadow: 0 0 8px rgba(0, 240, 255, 0.5); }";
  html += ".btn:hover { background-color: #00c0dd; transform: scale(1.05); }";

  html += "footer { position: absolute; bottom: 10px; font-size: 0.4rem; text-align: center; color: #888; }";
  html += "@media (min-width: 600px) { .button-container { flex-direction: row; } h1 { font-size: 0.8rem; } }";

  html += "</style></head><body class='fade-in'>";

  html += "<div class='container'>";
  html += "<h1 data-text='WiFi CONFIG'>WiFi CONFIG</h1>";
  html += "<form action='/saveConfigWiFi' method='POST'>";

  // Campo editable SSID
  html += "<label>Nombre de red (SSID)</label>";
  html += "<input type='text' id='ssidInput' name='ssid' value='" + ssid + "' maxlength='32'>";

  // Botón para forzar escaneo
  html += "<div class='button-container'>";
  html += "<button class='btn' type='button' onclick=\"location.href='?scan=1'\">BUSCAR REDES</button>";
  html += "</div>";

  // Escaneo de redes solo si el usuario pidió
  int n = 0;
  if (server.hasArg("scan") && server.arg("scan") == "1") {
    n = WiFi.scanNetworks();
    html += "<label>Redes disponibles</label>";
    html += "<select id='ssidSelect'>";
    if (n == 0) {
      html += "<option>No se encontraron redes</option>";
    } else {
      for (int i = 0; i < n; ++i) {
        String ssidFound = WiFi.SSID(i);
        html += "<option value='" + ssidFound + "'>" + ssidFound + "</option>";
      }
    }
    html += "</select>";
  }

  html += "<label>Contraseña WiFi</label>";
  html += "<input type='password' name='password' placeholder='Contraseña WiFi' value='" + password + "'>";

  html += "<label>Chat ID Telegram</label>";
  html += "<input type='text' name='chat_id' placeholder='Chat ID Telegram' value='" + String(chat_id) + "'>";

  html += "<label>GOOGLE (minutos)</label>";
  html += "<input type='number' name='tiempoGoogle' placeholder='GOOGLE (minutos)' value='" + String(tiempoGoogle) + "'>";

  html += "<label>TELEGRAM (minutos)</label>";
  html += "<input type='number' name='tiempoTelegram' placeholder='TELEGRAM (minutos)' value='" + String(tiempoTelegram) + "'>";

  html += "<div class='button-container'>";
  html += "<input class='btn' type='submit' value='GUARDAR'>";
  html += "<button class='btn' type='button' onclick=\"window.location.href='/'\">VOLVER</button>";
  html += "</div>";
  html += "</form>";

  // Botón CONECTAR WiFi
  html += "<form action='/connectWiFi' method='POST'>";
  html += "<div class='button-container'>";
  html += "<input class='btn' type='submit' value='CONECTAR WiFi'>";
  html += "</div>";
  html += "</form>";

  // NUEVO: Botón DESCONECTAR WiFi (manteniendo la misma estructura/estilos)
  html += "<form action='/disconnectWiFi' method='POST'>";
  html += "<div class='button-container'>";
  html += "<input class='btn' type='submit' value='DESCONECTAR WiFi'>";
  html += "</div>";
  html += "</form>";

  html += "</div>";

  html += "<footer><p>bmurphy1.618@gmail.com<br>BmuRphY</p></footer>";

// Sincronizar selector con campo de texto
  html += "<script>";
  html += "const sel = document.getElementById('ssidSelect');";
  html += "if (sel) {";
  html += "  sel.addEventListener('change', () => {";
  html += "    document.getElementById('ssidInput').value = sel.value;";
  html += "  });";
  html += "}";
  html += "</script>";

  html += "</body></html>";

  server.send(200, "text/html", html);
}







void connectWiFi() {
    // Cambiar las variables al presionar "Conectar WiFi"
    modoWiFi = 1;
    reset = 1;
    Guardado_General();

    // Mostrar mensaje con la misma estética
    String mensaje = "Conectando a WiFi...";
    String redireccion = "/config"; // Cambiar a la ruta deseada después de 3 segundos
    String html = "<html><head><style>";
    html += "body { background-color: #00264d; color: white; font-family: Arial, sans-serif; text-align: center; padding-top: 20%; margin: 0; }";
    html += "h1 { font-size: 800%; margin: 0 auto; line-height: 1.2; animation: fadeIn 2s ease-in-out; }";
    html += "div { background-color: #004080; border: 2px solid #00bfff; border-radius: 10px; padding: 20px; display: inline-block; text-align: center; }";
    html += "@keyframes fadeIn { from { opacity: 0; } to { opacity: 1; } }";
    html += "</style>";
    html += "<meta http-equiv=\"refresh\" content=\"3; url=" + redireccion + "\">"; // Redirección automática después de 3 segundos
    html += "</head><body>";

    html += "<div><h1><span>" + mensaje + "</span></h1></div>";

    html += "<script>setTimeout(function(){ window.location.href='" + redireccion + "'; }, 3000);</script>";

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
  String html = "<!DOCTYPE html><html lang='es'><head><meta charset='UTF-8'/><meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<title>Config R1</title>";
  html += "<link href='https://fonts.googleapis.com/css2?family=Press+Start+2P&display=swap' rel='stylesheet'>";
  html += "<style>";

  // Estilos generales
  html += "body { font-family: 'Press Start 2P', monospace; background: linear-gradient(to bottom, #0a0f1e, #111927); color: #e0e0e0; display: flex; flex-direction: column; align-items: center; justify-content: center; height: 100vh; margin: 0; padding: 10px; position: relative; }";
  html += ".fade-in { animation: fadeIn 1s ease-in-out both; }";
  html += "@keyframes fadeIn { 0% { opacity: 0; transform: translateY(20px); } 100% { opacity: 1; transform: translateY(0); } }";

  // Contenedor principal
  html += ".container { background-color: #1e293b; border: 1px solid #00f0ff; border-radius: 20px; padding: 20px; display: flex; flex-direction: column; align-items: center; box-shadow: 0 0 20px rgba(0,240,255,0.2); width: 90vw; max-width: 340px; gap: 12px; }";

  // Título con glitch
  html += "h1 { font-size: 0.7rem; color: #00f0ff; text-align: center; position: relative; text-shadow: 0 0 2px #0ff, 0 0 4px #0ff; }";
  html += "h1::after { content: attr(data-text); position: absolute; left: 2px; text-shadow: -1px 0 red; animation: glitch 1s infinite; top: 0; }";
  html += "h1::before { content: attr(data-text); position: absolute; left: -2px; text-shadow: 1px 0 blue; animation: glitch 1s infinite; top: 0; }";
  html += "@keyframes glitch { 0% {clip: rect(0, 9999px, 0, 0);} 5% {clip: rect(0, 9999px, 5px, 0);} 10% {clip: rect(5px, 9999px, 10px, 0);} 15% {clip: rect(0, 9999px, 0, 0);} 100% {clip: rect(0, 9999px, 0, 0);} }";

  // Labels y campos
  html += "label { font-size: 0.6rem; width: 100%; color: #e0e0e0; text-align: center; display: block; margin-top: 14px; margin-bottom: 6px; }";
  html += "select, input[type='time'], input[type='number'], input[type='text'] { font-family: 'Press Start 2P', monospace; font-size: 0.6rem; width: 100%; padding: 6px; border-radius: 8px; background: #0f172a; color: #e0e0e0; border: 2px solid #00f0ff; text-align: center; box-sizing: border-box; }";

  // Sliders
  html += "input[type='range'] { flex-grow: 1; margin: 0 6px; }";
  html += ".slider-row { display: flex; align-items: center; width: 100%; gap: 6px; }";
  html += ".slider-row span { font-size: 0.5rem; min-width: 60px; color: #00f0ff; text-align: center; }";

  // Estilo específico para los botones + y -
  html += ".btn-adjust { font-family: 'Press Start 2P', monospace; font-size: 0.6rem; background: #0f172a; color: #e0e0e0; border: 2px solid #00f0ff; border-radius: 8px; padding: 6px 10px; cursor: pointer; text-align: center; transition: 0.3s; box-shadow: 0 0 6px rgba(0, 240, 255, 0.3); }";
  html += ".btn-adjust:hover { background-color: #112031; transform: scale(1.05); }";

  // Botones
  html += ".button-container { display: flex; flex-direction: column; gap: 10px; width: 100%; margin-top: 12px; }";
  html += ".btn { font-family: 'Press Start 2P', monospace; font-size: 0.6rem; background-color: #00f0ff; color: #0f172a; border: 2px solid #00c0dd; padding: 10px; cursor: pointer; border-radius: 8px; width: 100%; text-align: center; transition: 0.3s; box-shadow: 0 0 8px rgba(0, 240, 255, 0.5); }";
  html += ".btn:hover { background-color: #00c0dd; transform: scale(1.05); }";


  html += "footer { position: absolute; bottom: 10px; font-size: 0.4rem; text-align: center; color: #888; }";
  html += "@media (min-width: 600px) { .button-container { flex-direction: row; } h1 { font-size: 0.8rem; } }";

  html += "</style></head><body class='fade-in'>";

  
  html += "<div class='container'>";
  html += "<h1 data-text='" + getRelayName(R1name) + "'>" + getRelayName(R1name) + "</h1>";
  html += "<form action='/saveConfigR1' method='POST'>";

  // MODO
  html += "<label for='modoR1'>MODO</label>";
  html += "<select id='modoR1' name='modoR1'>";
  html += "<option value='1'" + String((modoR1 == 1) ? " selected" : "") + ">Manual</option>";
  html += "<option value='2'" + String((modoR1 == 2) ? " selected" : "") + ">Automático</option>";
  html += "<option value='6'" + String((modoR1 == 6) ? " selected" : "") + ">Timer</option>";
  html += "</select>";

  // PARÁMETRO
  html += "<label for='paramR1'>PARÁMETRO</label>";
  html += "<select id='paramR1' name='paramR1'>";
  html += "<option value='1'" + String((paramR1 == 1) ? " selected" : "") + ">Humedad</option>";
  html += "<option value='2'" + String((paramR1 == 2) ? " selected" : "") + ">Temperatura</option>";
  html += "<option value='3'" + String((paramR1 == 3) ? " selected" : "") + ">VPD</option>";
  html += "</select>";

  // OBJETIVO
  html += "<label for='direccionR1'>OBJETIVO</label>";
  html += "<select id='direccionR1' name='direccionR1'>";
  html += "<option value='0'" + String((direccionR1 == 0) ? " selected" : "") + ">SUBIR</option>";
  html += "<option value='1'" + String((direccionR1 == 1) ? " selected" : "") + ">BAJAR</option>";
  html += "</select>";


// RANGO MÍNIMO
html += "<label for='minR1'>MÍNIMO</label>";
html += "<div class='slider-row'>";
html += "<input type='range' min='0' max='100' step='0.1' id='minR1Range' value='" + String(minR1, 1) + "' oninput=\"syncMinR1(this.value)\">";
html += "<input type='number' id='minR1' name='minR1' min='0' max='100' step='0.1' value='" + String(minR1, 1) + "' oninput=\"syncMinR1(this.value)\">";
html += "<button type='button' class='btn-adjust' id='minR1minus' onclick=\"adjustValue('minR1', -1)\">−</button>";
html += "<button type='button' class='btn-adjust' id='minR1plus' onclick=\"adjustValue('minR1', 1)\">+</button>";
html += "</div>";

// RANGO MÁXIMO
html += "<label for='maxR1'>MÁXIMO</label>";
html += "<div class='slider-row'>";
html += "<input type='range' min='0' max='100' step='0.1' id='maxR1Range' value='" + String(maxR1, 1) + "' oninput=\"syncMaxR1(this.value)\">";
html += "<input type='number' id='maxR1' name='maxR1' step='0.1' value='" + String(maxR1, 1) + "' oninput=\"syncMaxR1(this.value)\">";
html += "<button type='button' class='btn-adjust' id='maxR1minus' onclick=\"adjustValue('maxR1', -1)\">−</button>";
html += "<button type='button' class='btn-adjust' id='maxR1plus' onclick=\"adjustValue('maxR1', 1)\">+</button>";
html += "</div>";




  // HORA ENCENDIDO
  html += "<label for='horaOnR1'>HORA DE ENCENDIDO</label>";
  html += "<input type='time' id='horaOnR1' name='horaOnR1' value='" + formatTwoDigits(horaOnR1) + ":" + formatTwoDigits(minOnR1) + "'>";

  // HORA APAGADO
  html += "<label for='horaOffR1'>HORA DE APAGADO</label>";
  html += "<input type='time' id='horaOffR1' name='horaOffR1' value='" + formatTwoDigits(horaOffR1) + ":" + formatTwoDigits(minOffR1) + "'>";

  // ETIQUETA
  html += "<label for='R1name'>ETIQUETA</label>";
  html += "<select id='R1name' name='R1name'>";
  html += "<option value='0'" + String((R1name == 0) ? " selected" : "") + ">" + relayNames[0] + "</option>";
  html += "<option value='5'" + String((R1name == 5) ? " selected" : "") + ">" + relayNames[5] + "</option>";
  html += "</select>";

  // Botones
  html += "<div class='button-container'>";
  html += "<button class='btn' type='submit'>Guardar</button>";
  html += "<button class='btn' type='button' onclick=\"location.href='/'\">Volver</button>";
  html += "</div>";

  html += "</form></div><footer>Data Druida ©</footer>";

// Script ajuste valores
html += "<script>";
html += "function adjustValue(id, delta) {";
html += "  var input = document.getElementById(id);";
html += "  var value = parseFloat(input.value) || 0;";
html += "  value = (value + delta).toFixed(1);";
html += "  input.value = value;";
html += "  input.dispatchEvent(new Event('input'));";
html += "}";

html += "function syncMinR1(val) {";
html += "  val = parseFloat(val);";
html += "  let max = parseFloat(document.getElementById('maxR1').value);";
html += "  if (val > max) val = max;";
html += "  document.getElementById('minR1').value = val.toFixed(1);";
html += "  document.getElementById('minR1Range').value = val;";
html += "  updateButtons();";
html += "}";

html += "function syncMaxR1(val) {";
html += "  val = parseFloat(val);";
html += "  let min = parseFloat(document.getElementById('minR1').value);";
html += "  if (val < min) val = min;";
html += "  document.getElementById('maxR1').value = val.toFixed(1);";
html += "  document.getElementById('maxR1Range').value = val;";
html += "  updateButtons();";
html += "}";

html += "function updateButtons() {";
html += "  let min = parseFloat(document.getElementById('minR1').value);";
html += "  let max = parseFloat(document.getElementById('maxR1').value);";

html += "  document.getElementById('minR1minus').disabled = (min <= 0);";
html += "  document.getElementById('minR1plus').disabled = (min + 0.1 > max);";

html += "  document.getElementById('maxR1minus').disabled = (max - 0.1 < min);";
html += "  document.getElementById('maxR1plus').disabled = (max >= 100);";
html += "}";

html += "window.onload = updateButtons;";
html += "</script>";



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




void handleConfigR2() {
String html = "<!DOCTYPE html><html lang='es'><head><meta charset='UTF-8'/><meta name='viewport' content='width=device-width, initial-scale=1'>";
html += "<title>Config R2</title>";
html += "<link href='https://fonts.googleapis.com/css2?family=Press+Start+2P&display=swap' rel='stylesheet'>";
html += "<style>";
html += "body { font-family: 'Press Start 2P', monospace; background: linear-gradient(to bottom, #0a0f1e, #111927); color: #e0e0e0; display: flex; flex-direction: column; align-items: center; justify-content: center; height: 100vh; margin: 0; padding: 10px; position: relative; }";
html += ".fade-in { animation: fadeIn 1s ease-in-out both; }";
html += "@keyframes fadeIn { 0% { opacity: 0; transform: translateY(20px); } 100% { opacity: 1; transform: translateY(0); } }";
html += ".container { background-color: #1e293b; border: 1px solid #00f0ff; border-radius: 20px; padding: 20px; display: flex; flex-direction: column; align-items: center; box-shadow: 0 0 6px rgba(0,240,255,0.2); width: 90vw; max-width: 340px; gap: 12px; }";
html += "h1 { font-size: 0.7rem; color: #00f0ff; text-align: center; position: relative; text-shadow: 0 0 2px #0ff, 0 0 4px #0ff; }";
html += "h1::after { content: attr(data-text); position: absolute; left: 2px; text-shadow: -1px 0 red; animation: glitch 1s infinite; top: 0; }";
html += "h1::before { content: attr(data-text); position: absolute; left: -2px; text-shadow: 1px 0 blue; animation: glitch 1s infinite; top: 0; }";
html += "@keyframes glitch { 0%,100% {clip: rect(0,9999px,0,0);} 5% {clip: rect(0,9999px,5px,0);} 10% {clip: rect(5px,9999px,10px,0);} 15% {clip: rect(0,9999px,0,0);} }";
html += "label { font-size: 0.6rem; width: 100%; color: #e0e0e0; text-align: center; display: block; margin-top: 14px; margin-bottom: 6px; }";
html += "select, input[type='number'] { font-family: 'Press Start 2P', monospace; font-size: 0.6rem; width: 100%; padding: 6px; border-radius: 8px; background: #0f172a; color: #e0e0e0; border: 2px solid #00f0ff; text-align: center; box-sizing: border-box; }";
html += "form > *:not(.button-container) { margin-bottom: 12px; }";
html += ".slider-row { display: flex; align-items: center; width: 100%; gap: 6px; flex-wrap: nowrap; }";
html += ".slider-row span { font-size: 0.6rem; min-width: 60px; color: #00f0ff; text-align: center; }";
html += "input[type='range'] { flex-grow: 1; margin: 0 6px; }";
html += "input[type='number'] { max-width: 64px; }";
html += ".button-container { display: flex; flex-direction: column; gap: 10px; width: 100%; margin-top: 12px; justify-content: center; }";
html += ".btn-adjust { font-family: 'Press Start 2P', monospace; font-size: 0.6rem; background: #0f172a; color: #e0e0e0; border: 2px solid #00f0ff; border-radius: 8px; padding: 6px 10px; cursor: pointer; text-align: center; transition: 0.3s; box-shadow: 0 0 6px rgba(0, 240, 255, 0.3); }";
html += ".btn-adjust:hover { background-color: #112031; transform: scale(1.05); }";
html += ".disabled-btn { pointer-events: none; opacity: 0.4; filter: grayscale(100%); }";
html += "@media (min-width: 600px) { .button-container { flex-direction: row; } h1 { font-size: 0.8rem; } }";
html += "</style></head><body class='fade-in'>";




String disabledAttr = (modoR2 == 2 && paramR2 == 4) ? "" : " disabled";
String tempButtonsDisabled = (modoR2 == 2 && paramR2 == 4) ? "" : " disabled-btn";

html += "<div class='container'>";
html += "<h1 data-text='" + getRelayName(R2name) + "'>" + getRelayName(R2name) + "</h1>";
html += "<form action='/saveConfigR2' method='POST' id='formConfigR2'>";

html += "<label for='modoR2'>MODO</label>";
html += "<select id='modoR2' name='modoR2'>";
html += "<option value='1'" + String((modoR2 == 1) ? " selected" : "") + ">Manual</option>";
html += "<option value='2'" + String((modoR2 == 2) ? " selected" : "") + ">Automático</option>";
html += "<option value='3'" + String((modoR2 == 9) ? " selected" : "") + ">Automático Inteligente</option>";
html += "</select>";

// Min Humedad
html += "<div style='text-align:center; font-size:0.6rem; color:#e0e0e0; margin-top:14px; margin-bottom:6px;'>MINIMO</div>";
html += "<div class='slider-row' style='justify-content: center; gap: 6px;'>";
html += "<input type='range' style='max-width:150px;' min='0' max='100' step='0.1' id='minR2Range' value='" + String(minR2, 1) + "'>";
html += "<input type='number' style='max-width:80px;' id='minR2' name='minR2' step='0.1' value='" + String(minR2, 1) + "'>";
html += "<button type='button' class='btn-adjust' onclick=\"adjustValue('minR2', 1)\">+</button>";
html += "<button type='button' class='btn-adjust' onclick=\"adjustValue('minR2', -1)\">−</button>";
html += "</div>";

// Max Humedad
html += "<div style='text-align:center; font-size:0.6rem; color:#e0e0e0; margin-top:14px; margin-bottom:6px;'>MÁXIMO</div>";
html += "<div class='slider-row' style='justify-content: center; gap: 6px;'>";
html += "<input type='range' style='max-width:150px;' min='0' max='100' step='0.1' id='maxR2Range' value='" + String(maxR2, 1) + "'>";
html += "<input type='number' style='max-width:80px;' id='maxR2' name='maxR2' step='0.1' value='" + String(maxR2, 1) + "'>";
html += "<button type='button' class='btn-adjust' onclick=\"adjustValue('maxR2', 1)\">+</button>";
html += "<button type='button' class='btn-adjust' onclick=\"adjustValue('maxR2', -1)\">−</button>";
html += "</div>";

// Min Temp
html += "<div style='text-align:center; font-size:0.6rem; color:#e0e0e0; margin-top:14px; margin-bottom:6px;'>MIN TEMP</div>";
html += "<div class='slider-row' style='justify-content: center; gap: 6px;'>";

html += "<input type='range' style='max-width:150px;' min='0' max='50' step='0.1' id='minTR2Range' value='" + String(minTR2, 1) + "'" + disabledAttr + ">";
html += "<input type='number' style='max-width:80px;' id='minTR2' name='minTR2' step='0.1' value='" + String(minTR2, 1) + "'" + disabledAttr + ">";

html += "<button type='button' id='minTR2Minus' class='btn-adjust' onclick=\"adjustValue('minTR2', -1)\">−</button>";
html += "<button type='button' id='minTR2Plus' class='btn-adjust' onclick=\"adjustValue('minTR2', 1)\">+</button>";

html += "</div>";

// Max Temp
html += "<div style='text-align:center; font-size:0.6rem; color:#e0e0e0; margin-top:14px; margin-bottom:6px;'>MAX TEMP</div>";
html += "<div class='slider-row' style='justify-content: center; gap: 6px;'>";

html += "<input type='range' style='max-width:150px;' min='0' max='50' step='0.1' id='maxTR2Range' value='" + String(maxTR2, 1) + "'" + disabledAttr + ">";
html += "<input type='number' style='max-width:80px;' id='maxTR2' name='maxTR2' step='0.1' value='" + String(maxTR2, 1) + "'" + disabledAttr + ">";

html += "<button type='button' id='maxTR2Minus' class='btn-adjust' onclick=\"adjustValue('maxTR2', -1)\">−</button>";
html += "<button type='button' id='maxTR2Plus' class='btn-adjust' onclick=\"adjustValue('maxTR2', 1)\">+</button>";

html += "</div>";

// Selector de parámetro
html += "<label for='paramR2'>PARÁMETRO</label>";
html += "<select id='paramR2' name='paramR2' onchange='toggleTempInputs()'>";
html += "<option value='1'" + String((paramR2 == 1) ? " selected" : "") + ">Humedad</option>";
html += "<option value='2'" + String((paramR2 == 2) ? " selected" : "") + ">Temperatura</option>";
html += "<option value='3'" + String((paramR2 == 3) ? " selected" : "") + ">VPD</option>";
html += "<option value='4'" + String((paramR2 == 4) ? " selected" : "") + ">H + T</option>";
html += "</select>";

// Botones finales
html += "<div class='button-container'>";
html += "<button type='submit' class='btn-adjust'>GUARDAR</button>";
html += "<a href='/' class='btn-adjust' style='text-decoration:none;'>VOLVER</a>";
html += "</div>";

html += "</form></div>";

html += "<script>";
html += "function syncInputs(rangeId, numberId, minLimit, maxLimit, pairId, isMin) {";
html += "  const range = document.getElementById(rangeId);";
html += "  const number = document.getElementById(numberId);";
html += "  const pair = document.getElementById(pairId);";
html += "  function update(val) {";
html += "    val = parseFloat(val);";
html += "    if (isMin && val > parseFloat(pair.value)) val = parseFloat(pair.value);";
html += "    if (!isMin && val < parseFloat(pair.value)) val = parseFloat(pair.value);";
html += "    val = Math.min(Math.max(val, minLimit), maxLimit);";
html += "    range.value = val.toFixed(1);";
html += "    number.value = val.toFixed(1);";
html += "    updateButtons(numberId, minLimit, maxLimit, pairId, isMin);";
html += "  }";
html += "  range.addEventListener('input', (e) => { update(e.target.value); });";
html += "  number.addEventListener('input', (e) => { update(e.target.value); });";
html += "  range.addEventListener('change', (e) => { update(e.target.value); });";
html += "  number.addEventListener('change', (e) => { update(e.target.value); });";
html += "}";

html += "function adjustValue(id, delta) {";
html += "  const input = document.getElementById(id);";
html += "  let val = parseFloat(input.value) || 0;";
html += "  const isMin = id.startsWith('min');";
html += "  const isTemp = id.includes('TR2');";
html += "  const pairId = isMin ? id.replace('min', 'max') : id.replace('max', 'min');";
html += "  const pairInput = document.getElementById(pairId);";
html += "  let pairVal = parseFloat(pairInput?.value);";
html += "  if (isNaN(pairVal)) pairVal = isMin ? 50 : 0;";
html += "  const limit = isTemp ? 50 : 100;";
html += "  val += delta * 0.1;";
html += "  val = parseFloat(val.toFixed(1));";
html += "  if (isMin && val > pairVal) val = pairVal;";
html += "  if (!isMin && val < pairVal) val = pairVal;";
html += "  val = Math.min(Math.max(val, 0), limit);";
html += "  input.value = val.toFixed(1);";
html += "  const range = document.getElementById(id + 'Range');";
html += "  if (range) range.value = val.toFixed(1);";
html += "  updateButtons(id, 0, limit, pairId, isMin);";
html += "}";

html += "function updateButtons(id, minLimit, maxLimit, pairId, isMin) {";
html += "  const input  = document.getElementById(id);";
html += "  const minus  = document.getElementById(id + 'Minus');";
html += "  const plus   = document.getElementById(id + 'Plus');";
html += "  const val    = parseFloat(input.value);";
html += "  const pairVal= parseFloat(document.getElementById(pairId).value);";
html += "  if (isMin) {";
html += "    if (minus) minus.disabled = val <= minLimit;";
html += "    if (plus)  plus.disabled  = val >= pairVal;";
html += "  } else {";
html += "    if (minus) minus.disabled = val <= pairVal;";
html += "    if (plus)  plus.disabled  = val >= maxLimit;";
html += "  }";
html += "}";

html += "function toggleTempInputs() {";
html += "  const isHT = document.getElementById('paramR2').value === '4';";
html += "  ['minTR2', 'maxTR2'].forEach(id => {";
html += "    document.getElementById(id).disabled        = !isHT;";
html += "    document.getElementById(id + 'Range').disabled = !isHT;";
html += "    document.getElementById(id + 'Plus').disabled  = !isHT;";
html += "    document.getElementById(id + 'Minus').disabled = !isHT;";
html += "    if (isHT) {";
html += "      const isMin  = id.startsWith('min');";
html += "      const pairId = isMin ? 'maxTR2' : 'minTR2';";
html += "      updateButtons(id, 0, 50, pairId, isMin);";
html += "    }";
html += "  });";
html += "  if (isHT && !window._tr2Synced) {";
html += "    syncInputs('minTR2Range', 'minTR2', 0, 50, 'maxTR2', true);";
html += "    syncInputs('maxTR2Range', 'maxTR2', 0, 50, 'minTR2', false);";
html += "    window._tr2Synced = true;";
html += "  }";
html += "}";

html += "document.addEventListener('DOMContentLoaded', () => {";
html += "  syncInputs('minR2Range', 'minR2', 0, 100, 'maxR2', true);";
html += "  syncInputs('maxR2Range', 'maxR2', 0, 100, 'minR2', false);";
html += "  syncInputs('minTR2Range', 'minTR2', 0, 50, 'maxTR2', true);";
html += "  syncInputs('maxTR2Range', 'maxTR2', 0, 50, 'minTR2', false);";
html += "  toggleTempInputs();";
html += "  document.getElementById('paramR2').addEventListener('change', toggleTempInputs);";
html += "});";

html += "document.getElementById('formConfigR2').addEventListener('submit', e => {";
html += "  const minH = parseFloat(document.getElementById('minR2').value);";
html += "  const maxH = parseFloat(document.getElementById('maxR2').value);";
html += "  const minT = parseFloat(document.getElementById('minTR2').value);";
html += "  const maxT = parseFloat(document.getElementById('maxTR2').value);";
html += "  if (minH > maxH) { alert('MÍNIMO no puede ser mayor que MÁXIMO para Humedad.'); e.preventDefault(); }";
html += "  else if (minT > maxT) { alert('MIN TEMP no puede ser mayor que MAX TEMP.'); e.preventDefault(); }";
html += "});";
html += "</script>";





  html += "</body></html>";

  server.send(200, "text/html", html);
}




void handleConfigR3() {


  String html = "<!DOCTYPE html><html lang='es'><head><meta charset='UTF-8'/><meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<title>Config R3</title>";
  html += "<link href='https://fonts.googleapis.com/css2?family=Press+Start+2P&display=swap' rel='stylesheet'>";
  html += "<style>";
  html += "body { font-family: 'Press Start 2P', monospace; background: linear-gradient(to bottom, #0a0f1e, #111927); color: #e0e0e0; display: flex; flex-direction: column; align-items: center; justify-content: center; height: 100vh; margin: 0; padding: 10px; position: relative; }";
  html += ".fade-in { animation: fadeIn 1s ease-in-out both; }";
  html += "@keyframes fadeIn { 0% { opacity: 0; transform: translateY(20px); } 100% { opacity: 1; transform: translateY(0); } }";
  html += ".container { background-color: #1e293b; border: 1px solid #00f0ff; border-radius: 20px; padding: 20px; display: flex; flex-direction: column; align-items: center; box-shadow: 0 0 20px rgba(0,240,255,0.2); width: 90vw; max-width: 340px; gap: 12px; }";
  html += "h1 { font-size: 0.7rem; color: #00f0ff; text-align: center; position: relative; text-shadow: 0 0 2px #0ff, 0 0 4px #0ff; }";
  html += "h1::after { content: attr(data-text); position: absolute; left: 2px; text-shadow: -1px 0 red; animation: glitch 1s infinite; top: 0; }";
  html += "h1::before { content: attr(data-text); position: absolute; left: -2px; text-shadow: 1px 0 blue; animation: glitch 1s infinite; top: 0; }";
  html += "@keyframes glitch { 0% {clip: rect(0, 9999px, 0, 0);} 5% {clip: rect(0, 9999px, 5px, 0);} 10% {clip: rect(5px, 9999px, 10px, 0);} 15% {clip: rect(0, 9999px, 0, 0);} 100% {clip: rect(0, 9999px, 0, 0);} }";
  html += "label { font-size: 0.6rem; width: 100%; color: #e0e0e0; text-align: center; display: block; margin-top: 14px; margin-bottom: 6px; }";
  html += "select, input[type='time'], input[type='number'] { font-family: 'Press Start 2P', monospace; font-size: 0.6rem; width: 100%; padding: 6px; border-radius: 8px; background: #0f172a; color: #e0e0e0; border: 2px solid #00f0ff; text-align: center; box-sizing: border-box; }";
  html += "form > *:not(.button-container) { margin-bottom: 12px; }";
  html += ".button-container { display: flex; flex-direction: column; gap: 10px; width: 100%; margin-top: 12px; }";
  html += ".btn { font-family: 'Press Start 2P', monospace; font-size: 0.6rem; background-color: #00f0ff; color: #0f172a; border: 2px solid #00c0dd; padding: 10px; cursor: pointer; border-radius: 8px; width: 100%; text-align: center; transition: 0.3s; box-shadow: 0 0 8px rgba(0, 240, 255, 0.5); }";
  html += ".btn:hover { background-color: #00c0dd; transform: scale(1.05); }";
  html += "@media (min-width: 600px) { .button-container { flex-direction: row; } h1 { font-size: 0.8rem; } }";
  html += ".glitch-label { font-family: 'Press Start 2P', monospace; color: #00f0ff; font-size: 0.6rem; text-shadow: 0 0 5px #00f0ff, 0 0 10px #00f0ff; position: relative; display: block; margin-top: 14px; margin-bottom: 6px; text-align: center; }";
  html += ".glitch-label::after { content: attr(data-text); position: absolute; left: 2px; top: 2px; color: #ff00c8; z-index: -1; text-shadow: none; animation: glitch-anim 1s infinite; }";
  html += "@keyframes glitch-anim { 0% { transform: translate(0); opacity: 1; } 20% { transform: translate(-2px, 1px); opacity: 0.8; } 40% { transform: translate(3px, -2px); opacity: 0.6; } 60% { transform: translate(-1px, 2px); opacity: 0.8; } 80% { transform: translate(1px, -1px); opacity: 1; } 100% { transform: translate(0); opacity: 1; } }";
  html += ".dias-grid { display: grid; grid-template-columns: repeat(7, 1fr); gap: 4px; justify-items: center; }";
  html += ".dias-grid span { font-size: 0.55rem; color: #00f0ff; }";
  html += ".dias-grid input[type='checkbox'] { transform: scale(1.2); accent-color: #00f0ff; cursor: pointer; margin-top: 4px; }";
  html += ".input-row { display: flex; gap: 6px; align-items: center; width: 100%; }";
  html += ".input-row input, .input-row select { flex: 1; }";
  html += "</style></head><body class='fade-in'>";

  int tiempoRiegoSegundos = tiempoRiego;  // este ya viene guardado en segundos
  int tiempoNoRiegoSegundos = tiempoNoRiego;

  int unidadRiego = 1;
  if (tiempoRiego % 3600 == 0) unidadRiego = 3600;
  else if (tiempoRiego % 60 == 0) unidadRiego = 60;

  int unidadNoRiego = 1;
  if (tiempoNoRiego % 3600 == 0) unidadNoRiego = 3600;
  else if (tiempoNoRiego % 60 == 0) unidadNoRiego = 60;

  int tiempoRiegoMostrar = tiempoRiegoSegundos / unidadRiego;
  int tiempoNoRiegoMostrar = tiempoNoRiegoSegundos / unidadNoRiego;

  html += "<div class='container'>";
  html += "<h1 data-text='" + getRelayName(R3name) + "'>" + getRelayName(R3name) + "</h1>";
  html += "<form action='/saveConfigR3' method='POST' id='formConfigR3'>";

  html += "<label for='modoR3'>MODO</label>";
  html += "<select id='modoR3' name='modoR3'>";
  html += "<option value='1'" + String((modoR3 == 1) ? " selected" : "") + ">Manual</option>";
  html += "<option value='2'" + String((modoR3 == 2) ? " selected" : "") + ">Automático</option>";
  html += "</select>";

  html += "<label for='horaOnR3'>HORA ENCENDIDO</label>";
  html += "<input type='time' id='horaOnR3' name='horaOnR3' value='" + formatTwoDigits(horaOnR3) + ":" + formatTwoDigits(minOnR3) + "'>";

  html += "<label for='horaOffR3'>HORA APAGADO</label>";
  html += "<input type='time' id='horaOffR3' name='horaOffR3' readonly style='pointer-events: none; background-color: #222; color: #ccc;'>";

  html += "<label for='tiempoRiego'>PULSO</label>";
  html += "<div class='input-row'>";
  html += "<input type='number' id='tiempoRiego' name='tiempoRiego' min='0' step='1' value='" + String(tiempoRiegoMostrar) + "'>";
  html += "<select id='unidadRiego' name='unidadRiego'>";
  html += "<option value='1'" + String((unidadRiego == 1) ? " selected" : "") + ">seg</option>";
  html += "<option value='60'" + String((unidadRiego == 60) ? " selected" : "") + ">min</option>";
  html += "<option value='3600'" + String((unidadRiego == 3600) ? " selected" : "") + ">h</option>";
  html += "</select>";
  html += "</div>";

  html += "<label for='tiempoNoRiego'>PAUSA</label>";
  html += "<div class='input-row'>";
  html += "<input type='number' id='tiempoNoRiego' name='tiempoNoRiego' min='0' step='1' value='" + String(tiempoNoRiegoMostrar) + "'>";
  html += "<select id='unidadNoRiego' name='unidadNoRiego'>";
  html += "<option value='1'" + String((unidadNoRiego == 1) ? " selected" : "") + ">seg</option>";
  html += "<option value='60'" + String((unidadNoRiego == 60) ? " selected" : "") + ">min</option>";
  html += "<option value='3600'" + String((unidadNoRiego == 3600) ? " selected" : "") + ">h</option>";
  html += "</select>";
  html += "</div>";

  int cicloTotal = tiempoRiegoSegundos + tiempoNoRiegoSegundos;
  // int maxCantidad = (cicloTotal > 0) ? 86400 / cicloTotal : 1; // Ya no se usa

  html += "<label for='cantidad'>CICLOS</label>";
  html += "<div class='input-row'>";
  html += "<input type='number' id='cantidad' name='cantidad' min='1' step='1' value='" + String(cantidadRiegos) + "'>";
  html += "<select id='unidadCantidad' name='unidadCantidad'>";
  html += "<option value='dia' selected>Pulsos/Día</option>";
  html += "</select>";
  html += "</div>";



  html += "<label class='glitch-label' data-text='DÍAS DE RIEGO'>DÍAS DE RIEGO</label>";
  html += "<div class='dias-grid'>";
  html += "<span>D</span><span>L</span><span>M</span><span>M</span><span>J</span><span>V</span><span>S</span>";
  for (int i = 0; i < 7; i++) {
    html += "<input type='checkbox' name='diaRiego" + String(i) + "'" + String((diasRiego[i]) ? " checked" : "") + ">";
  }
  html += "</div>";

  html += "<div class='button-container'>";
  html += "<button type='submit' class='btn'>GUARDAR</button>";
  html += "<button type='button' class='btn' onclick=\"window.location.href='/config'\">VOLVER</button>";
  html += "</div>";

  html += "</form></div>";



  // Script para conversión automática de unidades
  html += "<script>";
  html += "let bloqueado = false;";

  html += "function setupConversion(inputId, selectId, onValueChange) {";
  html += "  const input = document.getElementById(inputId);";
  html += "  const select = document.getElementById(selectId);";
  html += "  let lastUnit = parseInt(select.value);";
  html += "  select.addEventListener('change', () => {";
  html += "    const newUnit = parseInt(select.value);";
  html += "    const currentVal = parseFloat(input.value) || 0;";
  html += "    const converted = currentVal * lastUnit / newUnit;";
  html += "    input.value = Math.round(converted * 100) / 100;";
  html += "    lastUnit = newUnit;";
  html += "    if (typeof onValueChange === 'function') onValueChange();";
  html += "  });";
  html += "  input.addEventListener('input', () => {";
  html += "    if (typeof onValueChange === 'function') onValueChange();";
  html += "  });";
  html += "}";

  html += "function tiempoTotalRiego() {";
  html += "  const tiempoR = parseFloat(document.getElementById('tiempoRiego').value) * parseInt(document.getElementById('unidadRiego').value) || 0;";
  html += "  const tiempoNR = parseFloat(document.getElementById('tiempoNoRiego').value) * parseInt(document.getElementById('unidadNoRiego').value) || 0;";
  html += "  return tiempoR + tiempoNR;";
  html += "}";

  html += "function getSecondsDesdeHora(hora) {";
  html += "  if (!hora.includes(':')) return 0;";
  html += "  const partes = hora.split(':');";
  html += "  const h = parseInt(partes[0]);";
  html += "  const m = parseInt(partes[1]);";
  html += "  return (isNaN(h) || isNaN(m)) ? 0 : h * 3600 + m * 60;";
  html += "}";

  html += "function setHoraDesdeSegundos(id, totalSegundos) {";
  html += "  let horas = Math.floor(totalSegundos / 3600) % 24;";
  html += "  let minutos = Math.floor((totalSegundos % 3600) / 60);";
  html += "  let segundos = totalSegundos % 60;";
  html += "  if (segundos > 0) {";
  html += "    minutos += 1;";
  html += "    if (minutos >= 60) {";
  html += "      minutos = 0;";
  html += "      horas = (horas + 1) % 24;";
  html += "    }";
  html += "  }";
  html += "  document.getElementById(id).value = ('0' + horas).slice(-2) + ':' + ('0' + minutos).slice(-2);";
  html += "}";

  html += "function calcularCantidad() {";
  html += "  if (bloqueado) return;";
  html += "  bloqueado = true;";
  html += "  const inicio = getSecondsDesdeHora(document.getElementById('horaOnR3').value);";
  html += "  const fin = getSecondsDesdeHora(document.getElementById('horaOffR3').value);";
  html += "  const tiempoR = parseFloat(document.getElementById('tiempoRiego').value) * parseInt(document.getElementById('unidadRiego').value) || 0;";
  html += "  const tiempoNR = parseFloat(document.getElementById('tiempoNoRiego').value) * parseInt(document.getElementById('unidadNoRiego').value) || 0;";
  html += "  if (tiempoR <= 0) { document.getElementById('cantidad').value = 1; bloqueado = false; return; }";
  html += "  const duracion = (fin >= inicio) ? (fin - inicio) : (86400 - inicio + fin);";
  html += "  const cantidad = Math.max(1, Math.floor((duracion + tiempoNR) / (tiempoR + tiempoNR)));";
  html += "  document.getElementById('cantidad').value = cantidad;";
  html += "  bloqueado = false;";
  html += "}";

  html += "function actualizarHoraOffDesdeCantidad() {";
  html += "  if (bloqueado) return;";
  html += "  bloqueado = true;";
  html += "  const inicio = getSecondsDesdeHora(document.getElementById('horaOnR3').value);";
  html += "  const cantidad = parseInt(document.getElementById('cantidad').value) || 1;";
  html += "  const tiempoR = parseFloat(document.getElementById('tiempoRiego').value) * parseInt(document.getElementById('unidadRiego').value) || 0;";
  html += "  const tiempoNR = parseFloat(document.getElementById('tiempoNoRiego').value) * parseInt(document.getElementById('unidadNoRiego').value) || 0;";
  html += "  const duracionTotal = (cantidad * tiempoR) + ((cantidad - 1) * tiempoNR);";
  html += "  const nuevoFin = (inicio + duracionTotal) % 86400;";
  html += "  setHoraDesdeSegundos('horaOffR3', nuevoFin);";
  html += "  bloqueado = false;";
  html += "}";

  // Eventos para mantener sincronía
  html += "document.getElementById('cantidad').addEventListener('input', actualizarHoraOffDesdeCantidad);";
  html += "document.getElementById('horaOnR3').addEventListener('input', calcularCantidad);";
  html += "document.getElementById('horaOffR3').addEventListener('input', calcularCantidad);";
  html += "document.getElementById('tiempoRiego').addEventListener('input', actualizarHoraOffDesdeCantidad);";
  html += "document.getElementById('unidadRiego').addEventListener('change', actualizarHoraOffDesdeCantidad);";
  html += "document.getElementById('tiempoNoRiego').addEventListener('input', actualizarHoraOffDesdeCantidad);";
  html += "document.getElementById('unidadNoRiego').addEventListener('change', actualizarHoraOffDesdeCantidad);";

  // Setup de conversión de unidades
  html += "setupConversion('tiempoRiego', 'unidadRiego', actualizarHoraOffDesdeCantidad);";
  html += "setupConversion('tiempoNoRiego', 'unidadNoRiego', actualizarHoraOffDesdeCantidad);";

  // Inicialización al cargar
  html += "window.addEventListener('load', () => { actualizarHoraOffDesdeCantidad(); });";

  html += "</script>";





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
  String html = "<!DOCTYPE html><html lang='es'><head><meta charset='UTF-8'/><meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<title>Config R4</title>";
  html += "<link href='https://fonts.googleapis.com/css2?family=Press+Start+2P&display=swap' rel='stylesheet'>";
  html += "<style>";
  html += "body { font-family: 'Press Start 2P', monospace; background: linear-gradient(to bottom, #0a0f1e, #111927); color: #e0e0e0; display: flex; flex-direction: column; align-items: center; justify-content: center; height: 100vh; margin: 0; padding: 10px; position: relative; }";
  html += ".fade-in { animation: fadeIn 1s ease-in-out both; }";
  html += "@keyframes fadeIn { 0% { opacity: 0; transform: translateY(20px); } 100% { opacity: 1; transform: translateY(0); } }";
  html += ".container { background-color: #1e293b; border: 1px solid #00f0ff; border-radius: 20px; padding: 20px; display: flex; flex-direction: column; align-items: center; box-shadow: 0 0 20px rgba(0,240,255,0.2); width: 90vw; max-width: 340px; gap: 12px; }";
  html += "h1 { font-size: 0.7rem; color: #00f0ff; text-align: center; position: relative; text-shadow: 0 0 2px #0ff, 0 0 4px #0ff; }";
  html += "h1::after { content: attr(data-text); position: absolute; left: 2px; text-shadow: -1px 0 red; animation: glitch 1s infinite; top: 0; }";
  html += "h1::before { content: attr(data-text); position: absolute; left: -2px; text-shadow: 1px 0 blue; animation: glitch 1s infinite; top: 0; }";
  html += "@keyframes glitch { 0% {clip: rect(0, 9999px, 0, 0);} 5% {clip: rect(0, 9999px, 5px, 0);} 10% {clip: rect(5px, 9999px, 10px, 0);} 15% {clip: rect(0, 9999px, 0, 0);} 100% {clip: rect(0, 9999px, 0, 0);} }";
  html += "label { font-size: 0.6rem; width: 100%; color: #e0e0e0; text-align: center; display: block; margin-top: 14px; margin-bottom: 6px; }";
  html += "select, input[type='time'], input[type='number'] { font-family: 'Press Start 2P', monospace; font-size: 0.6rem; width: 100%; padding: 6px; border-radius: 8px; background: #0f172a; color: #e0e0e0; border: 2px solid #00f0ff; text-align: center; box-sizing: border-box; }";
  html += "form > *:not(.button-container) { margin-bottom: 12px; }";
  html += ".button-container { display: flex; flex-direction: column; gap: 10px; width: 100%; margin-top: 12px; }";
  html += ".btn { font-family: 'Press Start 2P', monospace; font-size: 0.6rem; background-color: #00f0ff; color: #0f172a; border: 2px solid #00c0dd; padding: 10px; cursor: pointer; border-radius: 8px; width: 100%; text-align: center; transition: 0.3s; box-shadow: 0 0 8px rgba(0, 240, 255, 0.5); }";
  html += ".btn:hover { background-color: #00c0dd; transform: scale(1.05); }";
  html += "@media (min-width: 600px) { .button-container { flex-direction: row; } h1 { font-size: 0.8rem; } }";
  html += "</style></head><body class='fade-in'>";

  html += "<div class='container'>";
  html += "<h1 data-text='" + getRelayName(R4name) + "'>" + getRelayName(R4name) + "</h1>";
  html += "<form action='/saveConfigR4' method='POST'>";

  html += "<label for='modoR4'>MODO</label>";
  html += "<select id='modoR4' name='modoR4'>";
  html += "<option value='1'" + String((modoR4 == 1) ? " selected" : "") + ">Manual</option>";
  html += "<option value='2'" + String((modoR4 == 2) ? " selected" : "") + ">Automático</option>";
  html += "<option value='4'" + String((modoR4 == 4) ? " selected" : "") + ">SuperCiclo</option>";
  html += "</select>";

  html += "<label for='horaOnR4'>Hora Encendido</label>";
  html += "<input type='time' id='horaOnR4' name='horaOnR4' value='" + formatTwoDigits(horaOnR4) + ":" + formatTwoDigits(minOnR4) + "'>";

  html += "<label for='horaOffR4'>Hora Apagado</label>";
  html += "<input type='time' id='horaOffR4' name='horaOffR4' value='" + formatTwoDigits(horaOffR4) + ":" + formatTwoDigits(minOffR4) + "'>";

  html += "<label for='horaAmanecer'>Hora Amanecer</label>";
  html += "<input type='time' id='horaAmanecer' name='horaAmanecer' value='" + formatTwoDigits(horaAmanecer / 60) + ":" + formatTwoDigits(horaAmanecer % 60) + "'>";

  html += "<label for='horaAtardecer'>Hora Atardecer</label>";
  html += "<input type='time' id='horaAtardecer' name='horaAtardecer' value='" + formatTwoDigits(horaAtardecer / 60) + ":" + formatTwoDigits(horaAtardecer % 60) + "'>";

  html += "<p style='color: #00f0ff; font-size: 0.7rem; text-align: center; margin: 10px 0;'>SUPERCICLO</p>";
  html += "<label for='horasLuz'>Duración de Luz</label>";
  html += "<input type='time' step='60' id='horasLuz' name='horasLuz' value='" + formatTwoDigits(horasLuz / 60) + ":" + formatTwoDigits(horasLuz % 60) + "'>";

  html += "<label for='horasOscuridad'>Duración de Oscuridad</label>";
  html += "<input type='time' step='60' id='horasOscuridad' name='horasOscuridad' value='" + formatTwoDigits(horasOscuridad / 60) + ":" + formatTwoDigits(horasOscuridad % 60) + "'>";

  html += "<div class='button-container'>";
  html += "<button type='submit' class='btn'>GUARDAR</button>";
  html += "<button type='button' class='btn' onclick=\"window.location.href='/config'\">VOLVER</button>";
  html += "</div>";

  // ---------- Script JS ----------
  html += "<script>";
  html += "function toggleHoraOffInput() {";
  html += "  const modo = document.getElementById('modoR4').value;";
  html += "  const input = document.getElementById('horaOffR4');";
  html += "  if (modo === '4') {";
  html += "    input.disabled = true;";
  html += "    input.style.opacity = 0.5;";
  html += "  } else {";
  html += "    input.disabled = false;";
  html += "    input.style.opacity = 1;";
  html += "  }";
  html += "}";
  html += "document.getElementById('modoR4').addEventListener('change', toggleHoraOffInput);";
  html += "window.onload = toggleHoraOffInput;";
  html += "</script>";

  html += "</form></div></body></html>";

  server.send(200, "text/html", html);
}






void saveConfigR4() {
  if (server.method() == HTTP_POST) {
    
    // MODO de operación
    if (server.hasArg("modoR4")) {
      modoR4 = server.arg("modoR4").toInt();
    }

    // Hora de Encendido (siempre editable)
    if (server.hasArg("horaOnR4")) {
      String horaOn = server.arg("horaOnR4");
      int sepIndex = horaOn.indexOf(':');
      if (sepIndex != -1) {
        horaOnR4 = horaOn.substring(0, sepIndex).toInt();
        minOnR4 = horaOn.substring(sepIndex + 1).toInt();
      }
    }

    // Hora de Apagado (solo si NO es SUPERCICLO)
    if (modoR4 != SUPERCICLO && server.hasArg("horaOffR4")) {
      String horaOff = server.arg("horaOffR4");
      int sepIndex = horaOff.indexOf(':');
      if (sepIndex != -1) {
        horaOffR4 = horaOff.substring(0, sepIndex).toInt();
        minOffR4 = horaOff.substring(sepIndex + 1).toInt();
      }
    }

    // Estado manual si corresponde
    if (server.hasArg("estadoR4")) {
      estadoR4 = server.arg("estadoR4").toInt();
    }

    // Hora Amanecer
    if (server.hasArg("horaAmanecer")) {
      int h = server.arg("horaAmanecer").substring(0, 2).toInt();
      int m = server.arg("horaAmanecer").substring(3, 5).toInt();
      horaAmanecer = h * 60 + m;
    }

    // Hora Atardecer
    if (server.hasArg("horaAtardecer")) {
      int h = server.arg("horaAtardecer").substring(0, 2).toInt();
      int m = server.arg("horaAtardecer").substring(3, 5).toInt();
      horaAtardecer = h * 60 + m;
    }

    // Duración de luz (en minutos)
    if (server.hasArg("horasLuz")) {
      int h = server.arg("horasLuz").substring(0, 2).toInt();
      int m = server.arg("horasLuz").substring(3, 5).toInt();
      horasLuz = h * 60 + m;
      if (horasLuz < 0) horasLuz = 0;
    }

    // Duración de oscuridad (en minutos)
    if (server.hasArg("horasOscuridad")) {
      int h = server.arg("horasOscuridad").substring(0, 2).toInt();
      int m = server.arg("horasOscuridad").substring(3, 5).toInt();
      horasOscuridad = h * 60 + m;
      if (horasOscuridad < 0) horasOscuridad = 0;
    }

    // Recalcular el ciclo si estamos en modo SUPERCICLO
    if (modoR4 == SUPERCICLO) {
      unsigned long ahora = rtc.now().unixtime() / 60;

      if (R4estado == HIGH) {
        tiempoProxApagado = ahora + horasLuz;
      } else {
        tiempoProxEncendido = ahora + horasOscuridad;
      }

      superR4_Inicializado = true;
    }

    // Guardar todo en memoria
    Guardado_General();

    // Mostrar confirmación
    handleSaveConfig();

  } else {
    server.send(405, "text/plain", "Método no permitido");
  }
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


// Mensaje de confirmación



// Función para solicitar datos al Arduino Nano (I2C)
// Función para solicitar datos al Arduino Nano (I2C)



void mostrarEnPantallaOLED(float temperature, float humedad, float DPV, String hora) {
  display.clearDisplay();       // Limpia la pantalla
  display.setTextColor(SSD1306_WHITE);  // Color del texto
  //display.setTextColor(SH110X_WHITE); // ✅ Correcta para Adafruit_SH110X

  
  // Comprobar si las lecturas de temperatura y humedad son válidas
  String tempDisplay;
  String humDisplay;
  
  if (humedad == 0) {
    // Si la humedad es 0, se consideran ambas lecturas (temperatura y humedad) no válidas
    tempDisplay = "nan";
    humDisplay = "nan";
  } else {
    tempDisplay = (temperature < -40 || temperature > 85) ? "nan" : String(temperature, 1) + " C";
    humDisplay = (humedad < 0 || humedad > 100) ? "nan" : String(humedad, 1) + " %";
  }

  // Comprobar si DPV es válido
  String dpvDisplay = (isnan(DPV)) ? "nan" : String(DPV, 1);

  // Mostrar temperatura (tamaño 2)
  display.setTextSize(2);       // Tamaño del texto para temperatura, humedad, DPV
  display.setCursor(0, 0);      // Ajusta la posición Y para evitar cortes
  display.print("T: ");
  display.print(tempDisplay);
  
  // Mostrar humedad (tamaño 2)
  display.setCursor(0, 20);     // Baja un poco el texto
  display.print("H: ");
  display.print(humDisplay);

  // Mostrar DPV (tamaño 2)
  display.setCursor(0, 40);     // Baja más el texto
  display.print("VPD: ");
  display.print(dpvDisplay);
  display.setTextSize(1); 
  display.print("hPa");

  // Mostrar hora (solo horas y minutos, tamaño 1)
  display.setTextSize(1);       // Cambiar el tamaño a 1 para la hora
  display.setCursor(95, 57);     // Posición para la hora
  display.print(hora);
  display.setCursor(0, 57);
  display.print((WiFi.getMode() == WIFI_STA) ? WiFi.localIP() : WiFi.softAPIP());
  
  display.display();            // Actualiza la pantalla
}






void mostrarMensajeBienvenida() {
  display.clearDisplay();
  display.setTextSize(3);
  //display.setTextColor(SH110X_WHITE); // ✅ Correcta para Adafruit_SH110X
  // Tamaño del texto más grande
  display.setTextColor(SSD1306_WHITE);  // Color del texto

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
    if (relayIndex >= 0 && relayIndex < 6) {
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
    Serial.println("⚠️ Error: no se recibieron datos I2C desde Arduino Nano.");
    sensor1Value = sensor2Value = sensor3Value = 0;
    sensorPH = 0.0;
  }
}


