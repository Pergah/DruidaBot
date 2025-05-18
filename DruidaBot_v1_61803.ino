
// Proyecto: Druida BOT de DataDruida
// Autor: Bryan Murphy
// Año: 2025
// Licencia: MIT

#include "config.h"

void handleNewMessages(int numNewMessages);


void setup() {
  Wire.begin();
  Serial.begin(115200);
  EEPROM.begin(512);
  rtc.begin();
  irsend.begin();
  irrecv.enableIRIn();
  aht.begin();
  esp_task_wdt_init(WDT_TIMEOUT, true);  // Reinicio automático habilitado
  esp_task_wdt_add(NULL);

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

  // Inicializar pantalla
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) { 
    Serial.println(F("Error al inicializar la pantalla OLED"));
    while(true);
  }
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

        // Configurar NTP y RTC
        configTime(0, 0, "pool.ntp.org");
        time_t now = time(nullptr);
        while (now < 24 * 3600 && millis() - startMillis < 15000) {
            Serial.print(".");
            delay(100);
            now = time(nullptr);
        }

        struct tm timeinfo;
        if (getLocalTime(&timeinfo)) {
            rtc.adjust(DateTime(timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday, timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec));
            Serial.println("Hora de Internet obtenida y ajustada en el reloj RTC.");
        } else {
            Serial.println("No se pudo obtener la hora de Internet.");
        }

        const char* days[] = { "Domingo", "Lunes", "Martes", "Miércoles", "Jueves", "Viernes", "Sábado" };
        diaNumero = timeinfo.tm_wday;

        Serial.print("Día de hoy: ");
        Serial.println(days[diaNumero]);
    } else {
        Serial.println("No se pudo conectar a Wi-Fi. Verifique las credenciales.");
    }
} else {
    // Modo AP
    Serial.println("\nModo AP activado para configuración.");
    startAccessPoint();
}
  //startAccessPoint();
  Serial.println("Menu Serial: ");
  Serial.println("1. Modificar Red WiFi");
  Serial.println("2. Modificar Chat ID");
  Serial.println("3. Modificar Señal IR");
  Serial.println("4. Mostrar sensores");
}

void loop() {
  server.handleClient();
  unsigned long currentMillis = millis();
  unsigned long intervaloEnvio = intervaloDatos * 60000UL;  // de minutos a milisegundos

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
      while (numNewMessages) {
        Serial.println("got response");
        handleNewMessages(numNewMessages);
        numNewMessages = bot.getUpdates(bot.last_message_received + 1);
        delay(10);
      }
      bot_lasttime = millis();
    }
  }

  //requestSensorData();

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


if (currentMillis - previousMillisEnvio >= intervaloEnvio) {
  previousMillisEnvio = currentMillis;

  if (WiFi.status() == WL_CONNECTED) {
    // Enviar a Google Sheets
    sendDataToGoogleSheets();

    // Enviar mensaje Telegram
    DateTime now = rtc.now();  // usar para mostrar fecha y hora
    int hour = now.hour() - 3;
    if (hour < 0) hour += 24;

    String dateTime = "📅 Fecha y Hora: " + String(now.day()) + "/" + String(now.month()) + "/" + String(now.year()) + " " +
                      String(hour) + ":" + String(now.minute()) + ":" + String(now.second()) + "\n";

    String statusMessage = "🌡️ Temperatura: " + String(temperature, 1) + " °C\n";
    statusMessage += "💧 Humedad: " + String(humedad, 1) + " %\n";
    statusMessage += "🌬️ DPV: " + String(DPV, 1) + " hPa\n";
    statusMessage += dateTime;

    bot.sendMessage(chat_id, statusMessage, "");

    // Resetear máximos y mínimos
    maxHum = -999;
    minHum = 999;
    maxTemp = -999;
    minTemp = 999;
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

  if (serial == '3') {
    modificarValoresArray(false); //MODO AUTOMATICO
    serial = 0;
  }

  if (serial == '4') {
    mostrarSensores();
    serial = 0;
  }

  if (serial == '5') {
    irsend.sendRaw(rawData, 72, 38);
    delay(200);
    mostrarArray();
    serial = 0;
  }

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
  if (modoR2ir == MANUAL) {
    if (estadoR2ir == 1 && R2irestado == HIGH) {
      irsend.sendRaw(rawData, 72, 38);  // Envía la señal IR ajustada con frecuencia de 38 kHz
      R2irestado = LOW;
      esp_task_wdt_reset();
    }
    if (estadoR2ir == 0 && R2irestado == LOW) {
      irsend.sendRaw(rawData, 72, 38);  // Envía la señal IR ajustada con frecuencia de 38 kHz
      R2irestado = HIGH;
      esp_task_wdt_reset();
    }
  }

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

  if (modoR1 == AUTO) {
  if (paramR1 == H) {
    if (isnan(humedad) || humedad < 0 || humedad > 99.9) {
      bot.sendMessage(chat_id, "⚠️ Alerta: Valor de *humedad* fuera de rango o inválido. Humidificador apagado por seguridad.", "");
      if (R1estado == HIGH) {
        digitalWrite(RELAY1, LOW);
        R1estado = LOW;
      }
    } else {
      if (humedad < minR1 && R1estado == HIGH) {
        digitalWrite(RELAY1, LOW);
        R1estado = LOW;
      }
      if (humedad > maxR1 && R1estado == LOW) {
        digitalWrite(RELAY1, HIGH);
        R1estado = HIGH;
      }
    }
  }

  if (paramR1 == T) {
    if (isnan(temperature) || temperature < -10 || temperature > 50) {
      bot.sendMessage(chat_id, "⚠️ Alerta: Valor de *temperatura* fuera de rango o inválido. Humidificador apagado por seguridad.", "");
      if (R1estado == HIGH) {
        digitalWrite(RELAY1, LOW);
        R1estado = LOW;
      }
    } else {
      if (temperature < minR1 && R1estado == HIGH) {
        digitalWrite(RELAY1, LOW);
        R1estado = LOW;
      }
      if (temperature > minR1 && R1estado == LOW) {
        digitalWrite(RELAY1, HIGH);
        R1estado = HIGH;
      }
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

  if (modoR2ir == AUTO) {
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

  }


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

if (modoR4 == SUPERCICLO) {
  // 1. Convertir horas a minutos para cálculos
  unsigned long inicio = horaOnR4 * 60 + minOnR4;
  unsigned long fin = horaOffR4 * 60 + minOffR4;
  
  // Debug: Mostrar valores actuales
  Serial.print("SUPERCICLO - Inicio: "); Serial.print(horaOnR4); Serial.print(":"); Serial.print(minOnR4);
  Serial.print(" ("); Serial.print(inicio); Serial.print("), Fin: ");
  Serial.print(horaOffR4); Serial.print(":"); Serial.print(minOffR4);
  Serial.print(" ("); Serial.print(fin); Serial.print("), Current: "); Serial.println(currentTime);

  // 2. Verificar si necesitamos calcular nuevo ciclo
  if (R4estado == LOW) {
    // Si está encendido, verificar si es hora de apagar
    if ((inicio < fin && currentTime >= fin) || 
        (inicio > fin && (currentTime >= fin && currentTime < inicio))) {
      // Calcular nuevo encendido
      unsigned long nuevoInicio = fin + (horasOscuridad * 60);
      if (nuevoInicio >= 1440) nuevoInicio -= 1440;
      
      horaOnR4 = nuevoInicio / 60;
      minOnR4 = nuevoInicio % 60;
      inicio = nuevoInicio;
      
      // Calcular nuevo fin
      fin = inicio + (horasLuz * 60);
      if (fin >= 1440) fin -= 1440;
      
      horaOffR4 = fin / 60;
      minOffR4 = fin % 60;
      
      Guardado_General();
      Serial.println("SUPERCICLO - Calculado nuevo ciclo");
    }
  } else {
    // Si está apagado, verificar si es hora de encender
    if ((inicio < fin && currentTime >= inicio) || 
        (inicio > fin && (currentTime >= inicio || currentTime < fin))) {
      // Calcular nuevo fin
      fin = inicio + (horasLuz * 60);
      if (fin >= 1440) fin -= 1440;
      
      horaOffR4 = fin / 60;
      minOffR4 = fin % 60;
      
      Guardado_General();
      Serial.println("SUPERCICLO - Calculado nuevo apagado");
    }
  }

  // 3. Control del relay (LÓGICA IDÉNTICA AL MODO AUTO)
  if (inicio < fin) {
    // Caso normal: encendido antes que apagado
    if (currentTime >= inicio && currentTime < fin) {
      if (R4estado != LOW) {
        digitalWrite(RELAY4, LOW);
        R4estado = LOW;
        Serial.println("SUPERCICLO - Encendiendo (caso normal)");
      }
    } else {
      if (R4estado != HIGH) {
        digitalWrite(RELAY4, HIGH);
        R4estado = HIGH;
        Serial.println("SUPERCICLO - Apagando (caso normal)");
      }
    }
  } else {
    // Caso cruzando medianoche: encendido después que apagado
    if (currentTime >= inicio || currentTime < fin) {
      if (R4estado != LOW) {
        digitalWrite(RELAY4, LOW);
        R4estado = LOW;
        Serial.println("SUPERCICLO - Encendiendo (cruce medianoche)");
      }
    } else {
      if (R4estado != HIGH) {
        digitalWrite(RELAY4, HIGH);
        R4estado = HIGH;
        Serial.println("SUPERCICLO - Apagando (cruce medianoche)");
      }
    }
  }
}




 // Crear la variable 'dateTime' para usar en mostrarEnPantallaOLED
String hora = formatoHora(hour, now.minute());

mostrarEnPantallaOLED(temperature, humedad, DPV, hora);
esp_task_wdt_reset();
  delay(2000);

}



double avergearray(int* arr, int number) {
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
}


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
  chat_id = EEPROM.get(200, chat_id);
  minR3 = EEPROM.get(240, minR3);
  maxR3 = EEPROM.get(245, maxR3);
  minR2ir = EEPROM.get(250, minR2ir);
  maxR2ir = EEPROM.get(255, maxR2ir);
  paramR2ir = EEPROM.get(260, paramR2ir);
  estadoR2ir = EEPROM.get(265, estadoR2ir);
  modoR2ir = EEPROM.get(270, modoR2ir);
  R2irestado = EEPROM.get(272, R2irestado);
  horaOnR1 = EEPROM.get(276, horaOnR1);
  horaOffR1 = EEPROM.get(280, horaOffR1);
  minOnR1 = EEPROM.get(284, minOnR1);
  minOffR1 = EEPROM.get(288, minOffR1);
  tiempoRiego = EEPROM.get(292, tiempoRiego);
  tiempoNoRiego = EEPROM.get(296, tiempoNoRiego);
  cantidadRiegos = EEPROM.get(300, cantidadRiegos);
  currentPosition = EEPROM.get(304, currentPosition);
  horaAmanecer = EEPROM.get(308, horaAmanecer);
  horaAtardecer = EEPROM.get(312, horaAtardecer);
  modoWiFi = EEPROM.get(316, modoWiFi);
  R1name = EEPROM.get(320, R1name);
  //proximoCambioR4 = EEPROM.get(324, proximoCambioR4);
  //luzEncendida = EEPROM.get(328, luzEncendida);
  minTR2 = EEPROM.get(330, minTR2);
  maxTR2 = EEPROM.get(334, maxTR2);
  //proximoEncendidoR4 = EEPROM.get(338, proximoEncendidoR4);
  //proximoApagadoR4 = EEPROM.get(342, proximoApagadoR4);
  horasLuz = EEPROM.get(346, horasLuz);
  horasOscuridad = EEPROM.get(350, horasOscuridad);

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
  EEPROM.put(200, chat_id);
  EEPROM.put(240, minR3);
  EEPROM.put(245, maxR3);
  EEPROM.put(250, minR2ir);
  EEPROM.put(255, maxR2ir);
  EEPROM.put(260, paramR2ir);
  EEPROM.put(265, estadoR2ir);
  EEPROM.put(270, modoR2ir);
  EEPROM.put(272, R2irestado);
  EEPROM.put(276, horaOnR1);
  EEPROM.put(280, horaOffR1);
  EEPROM.put(284, minOnR1);
  EEPROM.put(288, minOffR1);
  EEPROM.put(292, tiempoRiego);
  EEPROM.put(296, tiempoNoRiego);
  EEPROM.put(300, cantidadRiegos);
  EEPROM.put(304, currentPosition);
  EEPROM.put(308, horaAmanecer);
  EEPROM.put(312, horaAtardecer);
  EEPROM.put(316, modoWiFi);
  EEPROM.put(320, R1name);
  //EEPROM.put(324, proximoCambioR4);
  //EEPROM.put(328, luzEncendida);
  EEPROM.put(330, minTR2);
  EEPROM.put(334, maxTR2);
  //EEPROM.put(338, proximoEncendidoR4 );
  //EEPROM.put(342, proximoApagadoR4 );
  EEPROM.put(346, horasLuz );
  EEPROM.put(350, horasOscuridad );

  
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
  // Inicia la conexión WiFi dependiendo del uso de contraseña
  if (conPW == 1) {
    WiFi.begin(ssid, password);
    Serial.print("Conectando a la red WiFi con contraseña...");
    Serial.println(ssid);
  } else {
    WiFi.begin(ssid);
    Serial.print("Conectando a la red WiFi sin contraseña...");
    Serial.println(ssid);
  }

  unsigned long startAttemptTime = millis();
  bool isConnected = false;

  // Intentar conectar durante 20 segundos
  while (millis() - startAttemptTime < 15000) {
    if (WiFi.status() == WL_CONNECTED) {
      isConnected = true;
      break;
    }
    delay(500);
    Serial.print(".");
  }

  if (isConnected) {
    Serial.println("\nWiFi conectado.");
    Serial.print("Dirección IP: ");
    Serial.println(WiFi.localIP());

// Mostrar mensaje de éxito en la pantalla OLED (versión mejorada)
display.clearDisplay();

// Primero mostrar "WiFi conectado" centrado (2 segundos)
display.setTextSize(1);  // Texto más grande
display.setTextColor(SSD1306_WHITE);
display.setCursor(16, 24);  // Centrado para texto 2x
display.println("WiFi conectado");
display.display();
delay(2000);  // Mostrar durante 2 segundos
display.clearDisplay();
display.setCursor(12, 32);
display.print("IP: ");
display.println(WiFi.localIP());
display.display();
delay(2000);  // Mostrar durante 2 segundos
display.clearDisplay();

  } else {
    Serial.println("\nNo se pudo conectar a WiFi. Reintentando...");

    // Reintentar conexión
    for (int attempt = 1; attempt <= 4; ++attempt) {
      Serial.print("Intento ");
      Serial.print(attempt);
      Serial.println(" de reconexión...");

      if (conPW == 1) {
        WiFi.begin(ssid, password);
      } else {
        WiFi.begin(ssid);
      }

      startAttemptTime = millis();
      while (millis() - startAttemptTime < 10000) { // Intento de reconexión por 10 segundos
        if (WiFi.status() == WL_CONNECTED) {
          isConnected = true;
          break;
        }
        delay(500);
        Serial.print(".");
      }

      if (isConnected) {
        break;
      }
    }

    if (isConnected) {
      Serial.println("\nWiFi conectado tras reintento.");
      Serial.print("Dirección IP: ");
      Serial.println(WiFi.localIP());

// Mostrar mensaje de éxito en la pantalla OLED (versión mejorada)
display.clearDisplay();

// Primero mostrar "WiFi conectado" centrado (2 segundos)
display.setTextSize(1);  // Texto más grande
display.setTextColor(SSD1306_WHITE);
display.setCursor(16, 24);  // Centrado para texto 2x
display.println("WiFi conectado");
display.display();
delay(2000);  // Mostrar durante 2 segundos

display.clearDisplay();
display.setTextSize(1);  // Texto normal
display.setCursor(16, 28);  // Centrado para texto 1x
delay(500);
startWebServer();
delay(500);
display.print("IP: ");
display.println(WiFi.localIP());
display.display();
delay(2000);  // Mostrar durante 2 segundos

// Limpiar pantalla al final
display.clearDisplay();
display.display();

    } else {
      Serial.println("\nError: No se pudo conectar a WiFi tras múltiples intentos.");
            // Mostrar mensaje de falla wifi en la pantalla OLED
      display.clearDisplay();
      display.setTextSize(1);
      display.setTextColor(SSD1306_WHITE);
      display.setCursor(0, 0);
      display.println("WiFi AP");
      display.print("IP: 192.168.4.1");
      startAccessPoint(); // Inicia el punto de acceso
    }
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
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi connection lost. Reconnecting...");
    connectToWiFi(ssid.c_str(), password.c_str());
    startWebServer();
  } else {
    Serial.println("WiFi connected.");
  }
}

void modificarValoresArray(bool manual) {
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
}


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
    server.on("/configIR", handleConfigIR);
    server.on("/captureIR", handleCaptureIR);
    server.on("/saveIRConfig", handleSaveIRConfig);
    server.on("/controlR2ir", handleControlR2ir);
    server.on("/controlR2irOn", HTTP_POST, handleControlR2irOn);
    server.on("/controlR2irOff", HTTP_POST, handleControlR2irOff);
    server.on("/controlR2irAuto", HTTP_POST, handleControlR2irAuto);
    server.on("/checkIRCapture", handleCheckIRCapture);
    server.on("/emitIR", handleEmitIR);

    server.begin();
    Serial.println("Servidor web iniciado");
}


void mostrarSensores() {
  Serial.print("Temperatura: ");
  Serial.print(temperature);
  Serial.println("° C");
  Serial.print("Humedad: ");
  Serial.print(humedad);
  Serial.println(" %");

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
    String dateTime = String(now.day()) + "/" + String(now.month()) + "/" + String(now.year()) + " " + horaBot + ":" + String(now.minute()) + ":" + String(now.second());

    // Construir mensaje de estado
    String statusMessage = "<div class='line'>Temp: " + String(temperature, 1) + " C</div>";
    statusMessage += "<div class='line'>Hum: " + String(humedad, 1) + " %</div>";
    statusMessage += "<div class='line'>DPV: " + String(DPV, 1) + " hPa</div>";
    statusMessage += "<div class='line'>" + dateTime + "</div>";

// Generar el HTML
String html = "<html><head><style>";
html += "body { background-color: #00264d; color: white; font-family: Arial, sans-serif; margin: 0; height: 100vh; display: flex; flex-direction: column; justify-content: space-between; align-items: center; text-align: center; }";
html += "header { margin-top: 100px; }"; // Separador superior
html += "header h1 { font-size: 4rem; margin: 0; line-height: 1.2; color: #00ff00; font-family: 'Courier New', Courier, monospace; background-color: #000000; padding: 10px 20px; border: 2px solid #00ff00; border-radius: 10px; text-align: center; white-space: pre-line; opacity: 0; animation: fadeInHeader 2s ease-in-out forwards; }";
html += "@keyframes fadeInHeader { from { opacity: 0; transform: translateY(-10px); } to { opacity: 1; transform: translateY(0); } }"; // Efecto de aparición
html += ".info-box { background-color: #004080; border: 2px solid #00bfff; padding: 15px; border-radius: 10px; position: absolute; top: 25%; transform: translateY(-50%); font-size: 1.5rem; line-height: 1.5; text-align: center; width: 80%; max-width: 600px; }";
html += ".info-box .line { opacity: 0; animation: fadeInLine 2s ease-in-out forwards; margin-bottom: 10px; }";
html += ".info-box .line:nth-child(1) { animation-delay: 0.5s; }";
html += ".info-box .line:nth-child(2) { animation-delay: 1s; }";
html += ".info-box .line:nth-child(3) { animation-delay: 1.5s; }";
html += ".info-box .line:nth-child(4) { animation-delay: 2s; }";
html += "@keyframes fadeInLine { from { opacity: 0; transform: translateY(-10px); } to { opacity: 1; transform: translateY(0); } }";
html += "main { flex-grow: 1; display: flex; flex-direction: column; justify-content: center; align-items: center; }";
html += "main a button { background-color: #1c75bc; color: white; border: 2px solid #00ff00; padding: 30px 80px; font-size: 48px; border-radius: 20px; cursor: pointer; margin: 20px; display: inline-block; animation: fadeInScale 1s ease-in-out forwards; }";
html += "main a button:hover { background-color: #004080; border-color: #00cc00; }";
html += "@keyframes fadeInScale { 0% { opacity: 0; transform: scale(0.9); } 100% { opacity: 1; transform: scale(1); } }";
html += "footer { margin-bottom: 30px; font-size: 1.5rem; color: #00bfff; }";
html += "</style>";
html += "<link href='https://fonts.googleapis.com/css2?family=Press+Start+2P&display=swap' rel='stylesheet'>";
html += "</head><body>";

html += "<header><h1>Druida\nBot</h1></header>"; // Manteniendo el formato tabulado

    html += "<div class=\"info-box\">";
    html += statusMessage;
    html += "</div>";
    html += "<main>";
    html += "<a href=\"/config\"><button>Configuracion</button></a>";
    html += "<a href=\"/control\"><button>Control</button></a>";
    html += "</main>";
    html += "<footer><p>bmurphy1.618@gmail.com<br>BmuRphY</p></footer>";

    html += "</body></html>";

    server.send(200, "text/html", html);
}











void handleControl() {
    String html = "<html><head><style>";
    html += "body { background-color: #00264d; color: white; font-family: Arial, sans-serif; margin: 0; height: 100vh; display: flex; flex-direction: column; justify-content: flex-start; align-items: center; text-align: center; }";
    html += "header { margin-top: 100px; }"; // Separador superior reducido
    html += "header h1 { font-size: 4rem; margin: 0; line-height: 1.2; color: #00ff00; font-family: 'Courier New', Courier, monospace; background-color: #000000; padding: 10px 20px; border: 2px solid #00ff00; border-radius: 10px; text-align: center; white-space: pre-line; opacity: 0; animation: fadeInHeader 2s ease-in-out forwards; }";
    html += "@keyframes fadeInHeader { from { opacity: 0; transform: translateY(-10px); } to { opacity: 1; transform: translateY(0); } }"; // Efecto de aparición
    html += "h1 { color: #00bfff; margin: 20px 0; font-size: 2.5rem; }"; // Espaciado ajustado para el título
    html += ".button-container { display: flex; flex-direction: column; align-items: center; gap: 100px; margin-top: 20px; }"; // Separador de 100px entre botones
    html += "button { background-color: #1c75bc; color: white; border: 2px solid #00ff00; padding: 30px 80px; font-size: 36px; border-radius: 20px; cursor: pointer; }";
    html += "button:hover { background-color: #004080; border-color: #00cc00; }";
    html += "</style>";
    html += "<link href='https://fonts.googleapis.com/css2?family=Press+Start+2P&display=swap' rel='stylesheet'>";
    html += "</head><body>";

    // Encabezado "Data Druida"
    html += "<header><h1>Druida\nBot</h1></header>";

    html += "<h1>Panel de Control</h1>";

    // Contenedor de botones
    html += "<div class=\"button-container\">";
    html += "<a href=\"/controlR1\"><button>Control de " + getRelayName(R1name) + "</button></a>";
    html += "<a href=\"/controlR2\"><button>Control de " + getRelayName(R2name) + "</button></a>";
    html += "<a href=\"/controlR3\"><button>Control de " + getRelayName(R3name) + "</button></a>";
    html += "<a href=\"/controlR4\"><button>Control de " + getRelayName(R4name) + "</button></a>";
    //html += "<a href=\"/controlR2ir\"><button>Control de " + getRelayName(R2irname) + "</button></a>";
    html += "<a href=\"/\"><button>Volver</button></a>"; // Botón de volver incluido en el mismo contenedor
    html += "</div>";

    html += "</body></html>";

    server.send(200, "text/html", html);
}





void handleConfirmation(const String& mensaje, const String& redireccion) {
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

void handleControlR2ir() {
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
}



void handleControlR1() {
    String html = "<html><head><style>";
    html += "body { background-color: #00264d; color: white; font-family: Arial, sans-serif; margin: 0; height: 100vh; display: flex; justify-content: center; align-items: center; flex-direction: column; text-align: center; }";
    html += "h1 { color: #00bfff; margin-bottom: 30px; }";
    html += "button, input[type='submit'] { background-color: #1c75bc; color: white; border: 2px solid #00ff00; padding: 20px 60px; font-size: 36px; border-radius: 20px; cursor: pointer; margin: 20px; display: inline-block; }";
    html += "button:hover, input[type='submit']:hover { background-color: #004080; border-color: #00cc00; }";
    html += "</style></head><body>";
    html += "<h1>Control de " + getRelayName(R1name) + "</h1>";

    // Botón de Encender
    html += "<form action=\"/controlR1On\" method=\"POST\">";
    html += "<input type=\"submit\" value=\"Encender\">";
    html += "</form>";

    // Botón de Apagar
    html += "<form action=\"/controlR1Off\" method=\"POST\">";
    html += "<input type=\"submit\" value=\"Apagar\">";
    html += "</form>";

    // Modo automático
    html += "<form action=\"/controlR1Auto\" method=\"POST\">";
    html += "<input type=\"submit\" value=\"Automatico\">";
    html += "</form>";

    // Botón para volver al panel de control
    html += "<a href=\"/control\"><button>Volver</button></a>";
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
    String html = "<html><head><style>";
    html += "body { background-color: #00264d; color: white; font-family: Arial, sans-serif; margin: 0; height: 100vh; display: flex; justify-content: center; align-items: center; flex-direction: column; text-align: center; }";
    html += "h1 { color: #00bfff; margin-bottom: 30px; }";
    html += "button, input[type='submit'] { background-color: #1c75bc; color: white; border: 2px solid #00ff00; padding: 20px 60px; font-size: 36px; border-radius: 20px; cursor: pointer; margin: 20px; display: inline-block; }";
    html += "button:hover, input[type='submit']:hover { background-color: #004080; border-color: #00cc00; }";
    html += "</style></head><body>";
    html += "<h1>Control de " + getRelayName(R2name) + "</h1>";

    // Botón de Encender
    html += "<form action=\"/controlR2On\" method=\"POST\">";
    html += "<input type=\"submit\" value=\"Encender\">";
    html += "</form>";

    // Botón de Apagar
    html += "<form action=\"/controlR2Off\" method=\"POST\">";
    html += "<input type=\"submit\" value=\"Apagar\">";
    html += "</form>";

    // Modo automático
    html += "<form action=\"/controlR2Auto\" method=\"POST\">";
    html += "<input type=\"submit\" value=\"Automatico\">";
    html += "</form>";

    // Botón para volver al panel de control
    html += "<a href=\"/control\"><button>Volver</button></a>";
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
    String html = "<html><head><style>";
    html += "body { background-color: #00264d; color: white; font-family: Arial, sans-serif; margin: 0; height: 100vh; display: flex; justify-content: center; align-items: center; flex-direction: column; text-align: center; }";
    html += "h1 { color: #00bfff; margin-bottom: 30px; }";
    html += "button, input[type='submit'] { background-color: #1c75bc; color: white; border: 2px solid #00ff00; padding: 20px 60px; font-size: 36px; border-radius: 20px; cursor: pointer; margin: 20px; display: inline-block; }";
    html += "button:hover, input[type='submit']:hover { background-color: #004080; border-color: #00cc00; }";
    html += "input[type='number'] { padding: 10px; font-size: 24px; width: 120px; text-align: center; border: 2px solid #00bfff; border-radius: 10px; margin: 10px 0; }";
    html += "</style></head><body>";
    html += "<h1>Control de " + getRelayName(R3name) + "</h1>";

    // Botón de Encender
    html += "<form action=\"/controlR3On\" method=\"POST\">";
    html += "<input type=\"submit\" value=\"Encender\">";
    html += "</form>";

    // Botón de Apagar
    html += "<form action=\"/controlR3Off\" method=\"POST\">";
    html += "<input type=\"submit\" value=\"Apagar\">";
    html += "</form>";

    // Modo automático
    html += "<form action=\"/controlR3Auto\" method=\"POST\">";
    html += "<input type=\"submit\" value=\"Automatico\">";
    html += "</form>";

    // Encender por tiempo
    html += "<form action=\"/controlR3OnFor\" method=\"POST\">";
    html += "Encender " + getRelayName(R3name) + " por <input type=\"number\" name=\"duration\" min=\"1\" step=\"1\" required> segundos ";
    html += "<input type=\"submit\" value=\"ON\">";
    html += "</form>";

    // Botón para volver al panel de control
    html += "<a href=\"/control\"><button>Volver</button></a>";
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
    String html = "<html><head><style>";
    html += "body { background-color: #00264d; color: white; font-family: Arial, sans-serif; margin: 0; height: 100vh; display: flex; justify-content: center; align-items: center; flex-direction: column; text-align: center; }";
    html += "h1 { color: #00bfff; margin-bottom: 30px; }";
    html += "button, input[type='submit'] { background-color: #1c75bc; color: white; border: 2px solid #00ff00; padding: 20px 60px; font-size: 36px; border-radius: 20px; cursor: pointer; margin: 20px; display: inline-block; }";
    html += "button:hover, input[type='submit']:hover { background-color: #004080; border-color: #00cc00; }";
    html += "</style></head><body>";
    html += "<h1>Control de " + getRelayName(R4name) + "</h1>";

    // Botón de Encender
    html += "<form action=\"/controlR4On\" method=\"POST\">";
    html += "<input type=\"submit\" value=\"Encender\">";
    html += "</form>";

    // Botón de Apagar
    html += "<form action=\"/controlR4Off\" method=\"POST\">";
    html += "<input type=\"submit\" value=\"Apagar\">";
    html += "</form>";

    // Modo automático
    html += "<form action=\"/controlR4Auto\" method=\"POST\">";
    html += "<input type=\"submit\" value=\"Automatico\">";
    html += "</form>";

    // Modo Superciclo
    html += "<form action=\"/controlR4Superciclo\" method=\"POST\">";
    html += "<input type=\"submit\" value=\"Superciclo\">";
    html += "</form>";

    // Botón Nube
html += "<form action=\"/controlR4Nube\" method=\"POST\">";
html += "<input type=\"submit\" value=\"Nube\">";
html += "</form>";

// Botón Medio Día
html += "<form action=\"/controlR4Mediodia\" method=\"POST\">";
html += "<input type=\"submit\" value=\"Medio Dia\">";
html += "</form>";

    // Botón para volver al panel de control
    html += "<a href=\"/control\"><button>Volver</button></a>";
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
    String html = "<html><head><style>";
    html += "body { background-color: #00264d; color: white; font-family: Arial, sans-serif; margin: 0; height: 100vh; display: flex; flex-direction: column; align-items: center; justify-content: flex-start; text-align: center; }";
    html += "header { margin-top: 100px; }";
    html += "header h1 { font-size: 4rem; margin: 0; line-height: 1.2; color: #00ff00; font-family: 'Courier New', Courier, monospace; background-color: #000000; padding: 10px 20px; border: 2px solid #00ff00; border-radius: 10px; text-align: center; white-space: pre-line; opacity: 0; animation: fadeInHeader 2s ease-in-out forwards; }";
    html += "@keyframes fadeInHeader { from { opacity: 0; transform: translateY(-10px); } to { opacity: 1; transform: translateY(0); } }";
    html += "h1 { color: #00bfff; margin: 20px 0; font-size: 2.5rem; }";
    html += ".container { display: flex; flex-direction: column; align-items: center; gap: 100px; margin-top: 20px; }";
    html += "button { background-color: #1c75bc; color: white; border: 2px solid #00ff00; padding: 30px 80px; font-size: 36px; border-radius: 20px; cursor: pointer; }";
    html += "button:hover { background-color: #004080; border-color: #00cc00; }";
    html += "</style>";
    html += "<link href='https://fonts.googleapis.com/css2?family=Press+Start+2P&display=swap' rel='stylesheet'>";
    html += "</head><body>";

    html += "<header><h1>Druida\nBot</h1></header>";

    html += "<div class=\"container\">";
    html += "<h1>Configuracion de Druida BOT</h1>";

    html += "<a href=\"/configR1\"><button>" + getRelayName(R1name) + "</button></a>";
    html += "<a href=\"/configR2\"><button>" + getRelayName(R2name) + "</button></a>";
    html += "<a href=\"/configR3\"><button>" + getRelayName(R3name) + "</button></a>";
    html += "<a href=\"/configR4\"><button>" + getRelayName(R4name) + "</button></a>";

    // Nuevo botón para configuración IR
    //html += "<a href=\"/configIR\"><button>IR Config</button></a>";

    html += "<a href=\"/configWiFi\"><button>WiFi</button></a>";
    html += "<a href=\"/\"><button>Volver</button></a>";
    html += "</div>";

    html += "</body></html>";

    server.send(200, "text/html", html);
}

void handleConfigIR() {
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
}

void handleConfigWiFi() {
    String html = "<html><head><style>";
    html += "body { background-color: #00264d; color: white; font-family: Arial, sans-serif; margin: 0; padding: 0; display: flex; justify-content: center; align-items: center; height: 100vh; text-align: center; }";
    html += ".container { background-color: #004080; border: 2px solid #00bfff; border-radius: 20px; padding: 30px; width: 50%; max-width: 600px; box-shadow: 0 0 20px rgba(0, 0, 0, 0.5); }";
    html += "h1 { color: #00bfff; font-size: 2.5rem; margin-bottom: 20px; }";
    html += "form { display: flex; flex-direction: column; align-items: center; }";
    html += "form label { font-size: 1.2rem; margin: 15px 0 5px; text-align: left; width: 100%; }";
    html += "input { width: 100%; padding: 15px; margin: 10px 0; border: 1px solid #00bfff; border-radius: 10px; font-size: 1.2rem; box-sizing: border-box; }";
    html += "input[type=\"submit\"], button { background-color: #007bff; color: white; border: none; padding: 15px 30px; font-size: 1.5rem; cursor: pointer; border-radius: 10px; margin-top: 20px; }";
    html += "input[type=\"submit\"]:hover, button:hover { background-color: #0056b3; }";
    html += "</style></head><body>";

    html += "<div class=\"container\">";
    html += "<h1>Configuracion WiFi</h1>";
    html += "<form action=\"/saveConfigWiFi\" method=\"POST\">";
    html += "<label for=\"ssid\">SSID:</label>";
    html += "<input type=\"text\" id=\"ssid\" name=\"ssid\" value=\"" + ssid + "\">";

    html += "<label for=\"password\">Password:</label>";
    html += "<input type=\"password\" id=\"password\" name=\"password\" value=\"" + password + "\">";

    html += "<label for=\"chat_id\">Chat ID:</label>";
    html += "<input type=\"text\" id=\"chat_id\" name=\"chat_id\" value=\"" + chat_id + "\">";

  html += "<label for=\"modoWiFi\">Modo WiFi:</label>";
  html += "<input type=\"number\" id=\"modoWiFi\" name=\"modoWiFi\" value=\"" + String(modoWiFi) + "\">";

    html += "<input type=\"submit\" value=\"Guardar\">";

    html += "</form>";

    // Botón "Conectar WiFi"
    html += "<form action=\"/connectWiFi\" method=\"POST\">";
    html += "<button type=\"submit\">Conectar WiFi</button>";
    html += "</form>";

    html += "<button onclick=\"window.location.href='/config'\">Volver</button>";
    html += "</div>";

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
        // Verificar y asignar cada parámetro recibido
        if (server.hasArg("ssid")) {
            ssid = server.arg("ssid");
        }
        if (server.hasArg("password")) {
            password = server.arg("password");
        }
        if (server.hasArg("chat_id")) {
            chat_id = server.arg("chat_id");
        }

        // Guardar cambios y mostrar confirmación
        handleSaveConfig();
    } else {
        // Si no es un método POST, devolver error
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
    String html = "<html><head><style>";
    html += "body { background-color: #00264d; color: white; font-family: Arial, sans-serif; margin: 0; padding: 0; display: flex; justify-content: center; align-items: center; height: 100vh; text-align: center; }";
    html += ".container { background-color: #004080; border: 2px solid #00bfff; border-radius: 20px; padding: 30px; width: 50%; max-width: 600px; box-shadow: 0 0 20px rgba(0, 0, 0, 0.5); }";
    html += "h1 { color: #00bfff; font-size: 2.5rem; margin-bottom: 20px; }";
    html += "form { display: flex; flex-direction: column; align-items: center; }";
    html += "form label { font-size: 1.2rem; margin: 15px 0 5px; text-align: left; width: 100%; }";
    html += "input { width: 100%; padding: 15px; margin: 10px 0; border: 1px solid #00bfff; border-radius: 10px; font-size: 1.2rem; box-sizing: border-box; }";
    html += "input[type=\"submit\"] { background-color: #007bff; color: white; border: none; padding: 15px 30px; font-size: 1.5rem; cursor: pointer; border-radius: 10px; margin-top: 20px; }";
    html += "input[type=\"submit\"]:hover { background-color: #0056b3; }";
    html += "button { background-color: #007bff; color: white; border: none; padding: 15px 30px; font-size: 1.5rem; cursor: pointer; border-radius: 10px; margin-top: 20px; }";
    html += "button:hover { background-color: #0056b3; }";
    html += "</style></head><body>";

    html += "<div class=\"container\">";
    html += "<h1>Configuracion de " + getRelayName(R1name) + "</h1>";
    html += "<form action=\"/saveConfigR1\" method=\"POST\">";
    html += "<label for=\"modoR1\">MODO (1: Manual - 2: Auto - 3: Config) </label>";
    html += "<input type=\"number\" id=\"modoR1\" name=\"modoR1\" value=\"" + String(modoR1) + "\">";

    html += "<label for=\"minR1\">MINIMO</label>";
    html += "<input type=\"number\" step=\"0.01\" id=\"minR1\" name=\"minR1\" value=\"" + String(minR1) + "\">";

    html += "<label for=\"maxR1\">MAXIMO</label>";
    html += "<input type=\"number\" step=\"0.01\" id=\"maxR1\" name=\"maxR1\" value=\"" + String(maxR1) + "\">";

    html += "<label for=\"paramR1\">PARAMETRO (1: Humedad - 2: Temperatura - 3: DPV):</label>";
    html += "<input type=\"number\" id=\"paramR1\" name=\"paramR1\" value=\"" + String(paramR1) + "\">";

    html += "<label for=\"horaOnR1\">HORA DE ENCENDIDO (HH:MM)</label>";
    html += "<input type=\"text\" id=\"horaOnR1\" name=\"horaOnR1\" value=\"" + formatTwoDigits(horaOnR1) + ":" + formatTwoDigits(minOnR1) + "\">";

    html += "<label for=\"horaOffR1\">HORA DE APAGADO (HH:MM)</label>";
    html += "<input type=\"text\" id=\"horaOffR1\" name=\"horaOffR1\" value=\"" + formatTwoDigits(horaOffR1) + ":" + formatTwoDigits(minOffR1) + "\">";

    //html += "<label for=\"estadoR1\">ESTADO (0: Apagado - 1: Encendido)</label>";
    //html += "<input type=\"number\" id=\"estadoR1\" name=\"estadoR1\" value=\"" + String(estadoR1) + "\">";

    html += "<label for=\"R1name\">ETIQUETA</label>";
    html += "<input type=\"number\" id=\"R1name\" name=\"R1name\" value=\"" + String(R1name) + "\">";

    html += "<input type=\"submit\" value=\"Guardar\">";
    html += "</form>";
    html += "<button onclick=\"window.location.href='/config'\">Volver</button>";
    html += "</div>";

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
    String html = "<html><head><style>";
    html += "body { background-color: #00264d; color: white; font-family: Arial, sans-serif; margin: 0; padding: 0; display: flex; justify-content: center; align-items: center; height: 100vh; text-align: center; }";
    html += ".container { background-color: #004080; border: 2px solid #00bfff; border-radius: 20px; padding: 30px; width: 50%; max-width: 600px; box-shadow: 0 0 20px rgba(0, 0, 0, 0.5); }";
    html += "h1 { color: #00bfff; font-size: 2.5rem; margin-bottom: 20px; }";
    html += "form { display: flex; flex-direction: column; align-items: center; }";
    html += "form label { font-size: 1.2rem; margin: 15px 0 5px; text-align: left; width: 100%; }";
    html += "input { width: 100%; padding: 15px; margin: 10px 0; border: 1px solid #00bfff; border-radius: 10px; font-size: 1.2rem; box-sizing: border-box; }";
    html += "input[type=\"submit\"] { background-color: #007bff; color: white; border: none; padding: 15px 30px; font-size: 1.5rem; cursor: pointer; border-radius: 10px; margin-top: 20px; }";
    html += "input[type=\"submit\"]:hover { background-color: #0056b3; }";
    html += "button { background-color: #007bff; color: white; border: none; padding: 15px 30px; font-size: 1.5rem; cursor: pointer; border-radius: 10px; margin-top: 20px; }";
    html += "button:hover { background-color: #0056b3; }";
    html += "</style></head><body>";

    html += "<div class=\"container\">";
    html += "<h1>Configuracion de " + getRelayName(R2name) + "</h1>";
    html += "<form action=\"/saveConfigR2\" method=\"POST\">";
    html += "<label>MODO (1: Manual - 2: Automatico - 3: Config)</label><input type=\"number\" name=\"modoR2\" value=\"" + String(modoR2) + "\">";
    html += "<label>MINIMO</label><input type=\"number\" step=\"0.01\" name=\"minR2\" value=\"" + String(minR2) + "\">";
    html += "<label>MAXIMO</label><input type=\"number\" step=\"0.01\" name=\"maxR2\" value=\"" + String(maxR2) + "\">";
    html += "<label>MINIMO (Temp)</label><input type=\"number\" step=\"0.01\" name=\"minTR2\" value=\"" + String(minTR2) + "\">";
    html += "<label>MAXIMO (Temp)</label><input type=\"number\" step=\"0.01\" name=\"maxTR2\" value=\"" + String(maxTR2) + "\">";
    html += "<label>PARAMETRO (1: Humedad - 2: Temperatura - 3: DPV - 4: H + T)</label><input type=\"number\" name=\"paramR2\" value=\"" + String(paramR2) + "\">";
    //html += "<label>ESTADO (0: Apagado - 1: Encendido)</label><input type=\"number\" name=\"estadoR2\" value=\"" + String(estadoR2) + "\">";
    html += "<input type=\"submit\" value=\"Guardar\">";
    html += "</form>";
    html += "<button onclick=\"window.location.href='/config'\">Volver</button>";
    html += "</div>";

    html += "</body></html>";

    server.send(200, "text/html", html);
}

void handleConfigR3() {
    String html = "<html><head><style>";
    html += "body { background-color: #00264d; color: white; font-family: Arial, sans-serif; margin: 0; padding: 0; display: flex; justify-content: center; align-items: center; height: 100vh; text-align: center; }";
    html += ".container { background-color: #004080; border: 2px solid #00bfff; border-radius: 20px; padding: 30px; width: 50%; max-width: 600px; box-shadow: 0 0 20px rgba(0, 0, 0, 0.5); }";
    html += "h1 { color: #00bfff; font-size: 2.5rem; margin-bottom: 20px; }";
    html += "form { display: flex; flex-direction: column; align-items: center; }";
    html += "form label { font-size: 1.2rem; margin: 15px 0 5px; text-align: left; width: 100%; }";
    html += "input { width: 100%; padding: 15px; margin: 10px 0; border: 1px solid #00bfff; border-radius: 10px; font-size: 1.2rem; box-sizing: border-box; }";
    html += "input[type=\"submit\"] { background-color: #007bff; color: white; border: none; padding: 15px 30px; font-size: 1.5rem; cursor: pointer; border-radius: 10px; margin-top: 20px; }";
    html += "input[type=\"submit\"]:hover { background-color: #0056b3; }";
    html += "button { background-color: #007bff; color: white; border: none; padding: 15px 30px; font-size: 1.5rem; cursor: pointer; border-radius: 10px; margin-top: 20px; }";
    html += "button:hover { background-color: #0056b3; }";
    html += "</style></head><body>";

    html += "<div class=\"container\">";
    html += "<h1>Configuracion de " + getRelayName(R3name) + "</h1>";
    html += "<form action=\"/saveConfigR3\" method=\"POST\">";
    html += "<label>MODO (1: Manual - 2: Automatico - 3: Config)</label><input type=\"number\" name=\"modoR3\" value=\"" + String(modoR3) + "\">";
    html += "<label>HORA DE ENCENDIDO (HH:MM)</label><input type=\"text\" name=\"horaOnR3\" value=\"" + formatTwoDigits(horaOnR3) + ":" + formatTwoDigits(minOnR3) + "\">";
    html += "<label>HORA DE APAGADO (HH:MM)</label><input type=\"text\" name=\"horaOffR3\" value=\"" + formatTwoDigits(horaOffR3) + ":" + formatTwoDigits(minOffR3) + "\">";
    html += "<label>DURACION (En segundos)</label><input type=\"number\" name=\"tiempoRiego\" value=\"" + String(tiempoRiego) + "\">";
    html += "<label>INTERVALO (En segundos)</label><input type=\"number\" name=\"tiempoNoRiego\" value=\"" + String(tiempoNoRiego) + "\">";
    //html += "<label>ESTADO (0: Apagado - 1: Encendido)</label><input type=\"number\" name=\"estadoR3\" value=\"" + String(estadoR3) + "\">";

    html += "<div style=\"width: 100%; text-align: center; margin-top: 20px;\">";
    html += "<label><strong>DIAS DE RIEGO</strong></label>";
    html += "</div>";

    html += "<div style=\"display: grid; grid-template-columns: 1fr auto; row-gap: 10px; width: 100%; max-width: 400px; text-align: left; margin-top: 10px;\">";


    const char* diasSemana[] = {"DOMINGO", "LUNES", "MARTES", "MIERCOLES", "JUEVES", "VIERNES", "SABADO"};
    for (int i = 0; i < 7; i++) {
        html += "<div style=\"font-size: 1rem;\">" + String(diasSemana[i]) + "</div>";
        html += "<div><input type=\"checkbox\" name=\"diaRiego" + String(i) + "\" value=\"1\" style=\"transform: scale(1.4);\"";
        if (diasRiego[i]) html += " checked";
        html += "></div>";
    }

    html += "</div>";

    html += "<input type=\"submit\" value=\"Guardar\">";
    html += "</form>";
    html += "<button onclick=\"window.location.href='/config'\">Volver</button>";
    html += "</div>";

    html += "</body></html>";

    server.send(200, "text/html", html);
}

void saveConfigR3() {
    if (server.method() == HTTP_POST) {
        // Verificar y asignar cada parámetro recibido
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
        if (server.hasArg("tiempoRiego")) {
            tiempoRiego = server.arg("tiempoRiego").toInt();
        }
        if (server.hasArg("tiempoNoRiego")) {
            tiempoNoRiego = server.arg("tiempoNoRiego").toInt();
        }
        if (server.hasArg("estadoR3")) {
            estadoR3 = server.arg("estadoR3").toInt();
        }
// Reiniciar todos los días de riego a 0
for (int i = 0; i < 7; i++) {
    diasRiego[i] = 0;
}

// Verificar cuáles fueron tildados y ponerlos en 1
for (int i = 0; i < 7; i++) {
    String paramName = "diaRiego" + String(i);
    if (server.hasArg(paramName)) {
        diasRiego[i] = 1;
    }
}



        // Guardar cambios y mostrar confirmación
        handleSaveConfig();
    } else {
        // Si no es un método POST, devolver error
        server.send(405, "text/plain", "Método no permitido");
    }
}

void handleConfigR4() {

        // Calcular hora de apagado para mostrar
    String horaApagado, minApagado;
    if (modoR4 == SUPERCICLO) {
        unsigned long apagadoReal = (horaOnR4 * 60 + minOnR4) + (horasLuz * 60);
        if (apagadoReal >= 1440) apagadoReal -= 1440;
        horaApagado = formatTwoDigits(apagadoReal / 60);
        minApagado = formatTwoDigits(apagadoReal % 60);
    } else {
        horaApagado = formatTwoDigits(horaOffR4);
        minApagado = formatTwoDigits(minOffR4);
    }
    String html = "<html><head><style>";
    html += "body { background-color: #00264d; color: white; font-family: Arial, sans-serif; margin: 0; padding: 0; display: flex; justify-content: center; align-items: center; height: 100vh; text-align: center; }";
    html += ".container { background-color: #004080; border: 2px solid #00bfff; border-radius: 20px; padding: 30px; width: 50%; max-width: 600px; box-shadow: 0 0 20px rgba(0, 0, 0, 0.5); }";
    html += "h1 { color: #00bfff; font-size: 2.5rem; margin-bottom: 20px; }";
    html += "form { display: flex; flex-direction: column; align-items: center; }";
    html += "form label { font-size: 1.2rem; margin: 15px 0 5px; text-align: left; width: 100%; }";
    html += "input { width: 100%; padding: 15px; margin: 10px 0; border: 1px solid #00bfff; border-radius: 10px; font-size: 1.2rem; box-sizing: border-box; }";
    html += "input[type=\"submit\"] { background-color: #007bff; color: white; border: none; padding: 15px 30px; font-size: 1.5rem; cursor: pointer; border-radius: 10px; margin-top: 20px; }";
    html += "input[type=\"submit\"]:hover { background-color: #0056b3; }";
    html += "button { background-color: #007bff; color: white; border: none; padding: 15px 30px; font-size: 1.5rem; cursor: pointer; border-radius: 10px; margin-top: 20px; }";
    html += "button:hover { background-color: #0056b3; }";
    html += "</style></head><body>";

    html += "<div class=\"container\">";
    html += "<h1>Configuracion de " + getRelayName(R4name) + "</h1>";
    html += "<form action=\"/saveConfigR4\" method=\"POST\">";
    
    // Modo de operación (añadido Superciclo como opción 4)
    html += "<label>MODO (1: Manual - 2: Automatico - 3: Config - 4: Superciclo)</label>";
    html += "<input type=\"number\" name=\"modoR4\" value=\"" + String(modoR4) + "\" min=\"1\" max=\"4\">";
    
    // Configuración básica
    html += "<label>HORA DE ENCENDIDO (HH:MM)</label>";
    html += "<input type=\"text\" name=\"horaOnR4\" value=\"" + formatTwoDigits(horaOnR4) + ":" + formatTwoDigits(minOnR4) + "\">";
    
    html += "<label>HORA DE APAGADO (HH:MM)</label>";
        if (modoR4 == SUPERCICLO) {
        html += "<input type=\"text\" name=\"horaOffR4\" value=\"" + horaApagado + ":" + minApagado + "\" readonly>";
    } else {
        html += "<input type=\"text\" name=\"horaOffR4\" value=\"" + horaApagado + ":" + minApagado + "\">";
    }
    
    // Configuración para Superciclo
    html += "<h3 style=\"color:#00bfff; margin-top:20px;\">SUPERCICLO</h3>";
    html += "<label>HORAS DE LUZ (0-30)</label>";
    html += "<input type=\"number\" name=\"horasLuz\" value=\"" + String(horasLuz) + "\" min=\"0\" max=\"30\">";
    
    html += "<label>HORAS DE OSCURIDAD (0-30)</label>";
    html += "<input type=\"number\" name=\"horasOscuridad\" value=\"" + String(horasOscuridad) + "\" min=\"0\" max=\"30\">";
    
    // Configuración existente
    //html += "<label>ESTADO (0: Apagado - 1: Encendido)</label>";
    //html += "<input type=\"number\" name=\"estadoR4\" value=\"" + String(estadoR4) + "\" min=\"0\" max=\"1\">";
    
    html += "<input type=\"submit\" value=\"Guardar\">";
    html += "</form>";
    html += "<button onclick=\"window.location.href='/config'\">Volver</button>";
    html += "</div>";

    html += "</body></html>";

    server.send(200, "text/html", html);
}



void saveConfigR4() {
    if (server.method() == HTTP_POST) {
        // Configuración básica
        if (server.hasArg("modoR4")) {
            modoR4 = server.arg("modoR4").toInt();
        }
        if (server.hasArg("horaOnR4")) {
            String horaOn = server.arg("horaOnR4");
            int sepIndex = horaOn.indexOf(':');
            if (sepIndex != -1) {
                horaOnR4 = horaOn.substring(0, sepIndex).toInt();
                minOnR4 = horaOn.substring(sepIndex + 1).toInt();
            }
        }
        if (server.hasArg("horaOffR4")) {
            String horaOff = server.arg("horaOffR4");
            int sepIndex = horaOff.indexOf(':');
            if (sepIndex != -1) {
                horaOffR4 = horaOff.substring(0, sepIndex).toInt();
                minOffR4 = horaOff.substring(sepIndex + 1).toInt();
            }
        }
        if (server.hasArg("estadoR4")) {
            estadoR4 = server.arg("estadoR4").toInt();
        }

        // Configuración del Superciclo
        if (server.hasArg("horasLuz")) {
            horasLuz = server.arg("horasLuz").toInt();
            if(horasLuz < 0) horasLuz = 0;
            if(horasLuz > 24) horasLuz = 24;
        }
        if (server.hasArg("horasOscuridad")) {
            horasOscuridad = server.arg("horasOscuridad").toInt();
            if(horasOscuridad < 0) horasOscuridad = 0;
            if(horasOscuridad > 24) horasOscuridad = 24;
        }

                // En modo SUPERCICLO, forzar cálculo automático
        if (modoR4 == SUPERCICLO) {
            unsigned long nuevoApagado = (horaOnR4 * 60 + minOnR4) + (horasLuz * 60);
            if (nuevoApagado >= 1440) nuevoApagado -= 1440;
            horaOffR4 = nuevoApagado / 60;
            minOffR4 = nuevoApagado % 60;
        }

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
void requestSensorData() {
  Wire.requestFrom(8, 6);  // Solicita 6 bytes (2 bytes por sensor) desde el esclavo I2C (dirección 8)

  if (Wire.available() == 6) {
    sensor1Value = Wire.read() << 8;    // Parte alta del sensor 1
    sensor1Value |= Wire.read();        // Parte baja del sensor 1
    sensor2Value = Wire.read() << 8;    // Parte alta del sensor 2
    sensor2Value |= Wire.read();        // Parte baja del sensor 2
    sensor3Value = Wire.read() << 8;    // Parte alta del sensor 3
    sensor3Value |= Wire.read();        // Parte baja del sensor 3

    // Convertir los valores directamente a porcentaje (asumiendo que los valores van de 0 a 1023)
    sensor1Value = map(sensor1Value, 0, 1023, 100, 0);
    sensor2Value = map(sensor2Value, 0, 1023, 100, 0);
    sensor3Value = map(sensor3Value, 0, 1023, 100, 0);

  }
}

void mostrarEnPantallaOLED(float temperature, float humedad, float DPV, String hora) {
  display.clearDisplay();       // Limpia la pantalla
  display.setTextColor(WHITE);  // Color del texto
  
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
  display.setTextSize(3);       // Tamaño del texto más grande
  display.setTextColor(WHITE);  // Color del texto

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