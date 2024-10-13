
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

  unsigned long startMillis = millis();
  Carga_General();

  connectToWiFi(ssid.c_str(), password.c_str());

  secured_client.setCACert(TELEGRAM_CERTIFICATE_ROOT);

  if (WiFi.status() == WL_CONNECTED) {
    bot.sendMessage(chat_id, "Druida Bot is ON");
    configTime(0, 0, "pool.ntp.org");

    time_t now = time(nullptr);
    while (now < 24 * 3600 && millis() - startMillis < 15000) {
      Serial.print(".");
      delay(100);
      now = time(nullptr);
    }

    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) {
      Serial.println("No se pudo obtener la hora de Internet.");
    } else {
      rtc.adjust(DateTime(timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday, timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec));
      Serial.println("Hora de Internet obtenida y ajustada en el reloj RTC.");
    }

    const char* days[] = { "Domingo", "Lunes", "Martes", "Miércoles", "Jueves", "Viernes", "Sábado" };
    int dayNumber = timeinfo.tm_wday;
    diaNumero = dayNumber;

    Serial.print("Dia de hoy: ");
    Serial.println(days[dayNumber]);
  } else {
    Serial.println("\nNo se pudo conectar a Wi-Fi. Iniciando modo AP para configuración.");

    // Iniciar el modo AP para configuración
    startAccessPoint();}

  Serial.println("Menu Serial: ");
  Serial.println("1. Modificar Red WiFi");
  Serial.println("2. Modificar Chat ID");
  Serial.println("3. Modificar Señal IR");
  Serial.println("4. Mostrar sensores");
}

void loop() {
    if (WiFi.getMode() == WIFI_AP) {
    server.handleClient(); // Maneja las solicitudes HTTP en modo AP
  }
  unsigned long currentMillis = millis();

  // Verifica la conexión WiFi a intervalos regulares
  if (currentMillis - previousMillis >= wifiCheckInterval) {
    previousMillis = currentMillis;
    checkWiFiConnection();
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

  requestSensorData();

  sensors_event_t humidity, temp;
  aht.getEvent(&humidity, &temp);

  float temperature = (temp.temperature);
  float humedad = (humidity.relative_humidity);

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

  /*if (rtc.now().minute() == 0 && hour != lastHourSent) {
    if (WiFi.status() == WL_CONNECTED) {
      sendDataToGoogleSheets();
      lastHourSent = hour;

      maxHum = -999;
      minHum = 999;
      maxTemp = -999;
      minTemp = 999;
    }
  }*/

  if (temperature > 40) {
    temperature = 40;
    //if (WiFi.status() == WL_CONNECTED) {
    //  bot.sendMessage(chat_id, "Alerta, temperatura demasiado alta");
    //}
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
    irsend.sendRaw(IRsignal, 72, 38);
    delay(200);
    serial = 0;
  }


  if (reset == 1) {
    esp_restart();
  }

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
      irsend.sendRaw(IRsignal, 72, 38);  // Envía la señal IR ajustada con frecuencia de 38 kHz
      R2irestado = LOW;
    }
    if (estadoR2ir == 0 && R2irestado == LOW) {
      irsend.sendRaw(IRsignal, 72, 38);  // Envía la señal IR ajustada con frecuencia de 38 kHz
      R2irestado = HIGH;
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

  //MODO AUTO R1 (UP) :

  if (modoR1 == AUTO) {
    //Serial.print("Rele (UP) Automatico");

    if (paramR1 == H) {
      if (humedad < minR1 && R4estado == HIGH) {
        digitalWrite(RELAY1, LOW);
        R1estado = LOW;
      }
      if (humedad > maxR1 && R4estado == LOW) {
        digitalWrite(RELAY1, HIGH);
        R1estado = HIGH;
      }
    }
    if (paramR1 == T) {
      if (temperature < minR1 && R1estado == HIGH) {
        digitalWrite(RELAY1, LOW);
        R1estado = LOW;
      }
      if (temperature > minR1 && R1estado == LOW) {
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

    /*if (paramR1 == TA) {
      if (temperatureC < minR1 && R1estado == HIGH) {
        digitalWrite(RELAY1, LOW);
        R1estado = LOW;
      }

        if (temperatureC > maxR1 && R1estado == LOW) {
          digitalWrite(RELAY1, HIGH);
          R1estado = HIGH;
        }
      
    }*/
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

  //MODO AUTO R2 (DOWN)

  if (modoR2 == AUTO) {
    //Serial.print("Rele 2 (Down) Automatico");

    if (paramR2 == H) {
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
    if (paramR2 == T) {
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

    /*if (paramR2 == TA) {
      if (temperatureC > maxR2 && R2estado == HIGH) {
        digitalWrite(RELAY2, LOW);
        R2estado = LOW;
        delay(200);
      }
      if (temperatureC < minR2 && R2estado == LOW) {
        digitalWrite(RELAY2, HIGH);
        R2estado = HIGH;
        delay(200);
      }
    }*/
  }


  //MODO AUTO R2   IR    (DOWN)

  if (modoR2ir == AUTO) {
    //Serial.print("Rele 2 (Down) Automatico");

    if (paramR2ir == H) {
      if (humedad > maxR2ir && R2irestado == HIGH) {
        irsend.sendRaw(IRsignal, 72, 38);
        R2irestado = LOW;
        delay(200);
      }
      if (humedad < minR2ir && R2irestado == LOW) {
        irsend.sendRaw(IRsignal, 72, 38);
        R2irestado = HIGH;
        delay(200);
      }
    }
    if (paramR2ir == T) {
      if (temperature > maxR2ir && R2irestado == HIGH) {
        R2irestado = LOW;
        irsend.sendRaw(IRsignal, 72, 38);
        delay(200);
      }
      if (temperature < minR2ir && R2irestado == LOW) {
        R2irestado = HIGH;
        irsend.sendRaw(IRsignal, 72, 38);
        delay(200);
      }
    }

    if (paramR2ir == D) {
      if (DPV > maxR2ir && R2irestado == HIGH) {
        R2irestado = LOW;
        irsend.sendRaw(IRsignal, 72, 38);
        delay(200);
      }
      if (DPV < minR2ir && R2irestado == LOW) {
        R2irestado = HIGH;
        irsend.sendRaw(IRsignal, 72, 38);
        delay(200);
      }
    }

    /*if (paramR2ir == TA) {
      if (temperatureC > maxR2ir && R2irestado == HIGH) {
        R2irestado = LOW;
        irsend.sendRaw(IRsignal, 72, 38);
        delay(200);
      }
      if (temperatureC < minR2ir && R2irestado == LOW) {
        R2irestado = HIGH;
        irsend.sendRaw(IRsignal, 72, 38);
        delay(200);
      }
    }*/
  }


  

  if (modoR3 == AUTO) {
    for (c = 0; c < 7; c++) {
      if (diasRiego[c] == 1) {
        if (c == day) {
          if (startR3 < offR3) { 
            // Caso normal: encendido antes que apagado
            if (currentTime >= startR3 && currentTime < offR3) {
              if (R3estado == HIGH){ 
                digitalWrite(RELAY3, LOW);
                R3estado = LOW;
              }
            } else {
              if (R3estado == LOW) {
                digitalWrite(RELAY3, HIGH);
                R3estado = HIGH;
              }
            }
          } else { 
            // Caso cruzando medianoche: encendido después que apagado
            if (currentTime >= startR3 || currentTime < offR3) {
              if (R3estado == HIGH){ 
                digitalWrite(RELAY3, LOW);
                R3estado = LOW;
              }
            } else {
              if (R3estado == LOW) {
                digitalWrite(RELAY3, HIGH);
                R3estado = HIGH;
              }
            }
          }
        }
      }
    }
  }

// MODO R3 AUTO INTELIGENTE

/*if (modoR3 == AUTOINT) {
  for (c = 0; c < 7; c++) {
    if (diasRiego[c] == 1) {
      if (c == day) {
        // Verifica si está dentro del horario de encendido
        if ((startR3 < offR3 && currentTime >= startR3 && currentTime < offR3) ||
            (startR3 > offR3 && (currentTime >= startR3 || currentTime < offR3))) {

          // Verifica si la humedad es menor a minR3
          if (humedadSuelo < minR3) {
            // Enciende el relé si no estaba encendido
            if (R3estado == HIGH) {
              digitalWrite(RELAY3, LOW);
              R3estado = LOW;
            }
          }

          // Apaga el relé si la humedad supera los maxR3 o si se sale del horario
          if (humedadSuelo > maxR3 || currentTime >= offR3) {
            if (R3estado == LOW) {
              digitalWrite(RELAY3, HIGH);
              R3estado = HIGH;
            }
          }

        } else {
          // Fuera del horario de encendido: Apaga el relé si está encendido
          if (R3estado == LOW) {
            digitalWrite(RELAY3, HIGH);
            R3estado = HIGH;
          }
        }
      }
    }
  }
}
*/
  //MODO AUTO R4 (Luz)

  if (modoR4 == AUTO) {
  if (startR4 < offR4) { 
    // Caso normal: encendido antes que apagado
    if (currentTime >= startR4 && currentTime < offR4) {
      if (R4estado == HIGH){ 
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
      if (R4estado == HIGH){ 
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

  float VPS = VPS_values[static_cast<int>(temperature) - 1];
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
  horaOnR1 = EEPROM.get(274, horaOnR1);
  horaOffR1 = EEPROM.get(276, horaOffR1);
  horaOffR1 = EEPROM.get(278, minOnR1);
  horaOffR1 = EEPROM.get(280, minOffR1);

  for (int i = 0; i < 100; i++) {
    IRsignal[i] = EEPROM.get(300 + i * sizeof(uint16_t), IRsignal[i]);
  }


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
  EEPROM.put(274, horaOnR1);
  EEPROM.put(276, horaOffR1);
  EEPROM.put(278, minOnR1);
  EEPROM.put(280, minOffR1);

  
  for (int i = 0; i < 100; i++) {
    EEPROM.put(300 + i * sizeof(uint16_t), IRsignal[i]);
  }
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
  if (conPW == 1) {
    WiFi.begin(ssid, password);
    Serial.print("Red WiFi: ");
    Serial.println(ssid);
    Serial.print("Password: ");
    Serial.println(password);
    Serial.print("Chat ID: ");
    Serial.println(chat_id);
  }
  if (conPW == 0) {
    WiFi.begin(ssid);
    Serial.print("Red WiFi: ");
    Serial.println(ssid);
  }

  unsigned long startAttemptTime = millis();

  // Esperar hasta que la conexión se establezca (10 segundos máximo)
  while (millis() - startAttemptTime < 20000) {
    delay(500);
    Serial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("");
    Serial.println("WiFi conectado.");
    Serial.println("Dirección IP: ");
    Serial.println(WiFi.localIP());

  } else {
    if (conPW = 0) {
      WiFi.begin(ssid);
    }
    if (conPW = 1) {
      WiFi.begin(ssid, password);
    }
    Serial.println("Reintentando conexion..");
    while (millis() - startAttemptTime < 20000) {
      delay(500);
      Serial.print(".");
    }

    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("");
      Serial.println("WiFi conectado.");
      Serial.println("Dirección IP: ");
      Serial.println(WiFi.localIP());
    } else {
      Serial.println("Error conexion WiFi. ");
      startAccessPoint();
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
  } else {
    Serial.println("WiFi connected.");
  }
}

void modificarValoresArray(bool manual) {
  if (manual) {
    // Proceso de carga manual
    Serial.println("Ingrese los valores del array manualmente en el monitor serial.");
    // (El resto del código para la carga manual permanece igual)
  } else {
    // Proceso de carga automática usando el sensor IR
    Serial.println("Apunte el control remoto hacia el sensor IR y presione el botón deseado.");

    // Esperar hasta que se reciba una señal IR válida
    while (!irrecv.decode(&results)) {
      delay(100);  // Pequeño delay para evitar un bucle muy rápido
    }

    // Capturar los valores del buffer IR
    IRsignalLength = results.rawlen - 1;
    if (IRsignalLength > 150) {
      IRsignalLength = 150;  // Limitar la longitud a 150 si es mayor
    }

    for (uint16_t i = 1; i <= IRsignalLength; i++) {
      IRsignal[i - 1] = results.rawbuf[i] * kRawTick;
    }

    Serial.println("Valores del array capturados automáticamente.");
    irrecv.resume();  // Preparar el receptor para la siguiente señal
  }

  mostrarArray();
  Guardado_General();
}



void mostrarArray() {
  Serial.println("Señal IR:");
  for (int i = 0; i < 100; i++) {
    Serial.print(IRsignal[i]);
    if (i < 100 - 1) {
      Serial.print(", ");
    }
  }
  Serial.println();

  // Limpia el buffer serial para evitar lecturas erróneas
  while (Serial.available() > 0) {
    Serial.read();
  }
}

void encenderRele1PorTiempo(int tiempoSegundos) {
  digitalWrite(RELAY1, LOW); // Enciende el relé
  delay(tiempoSegundos * 1000); // Mantiene encendido por el tiempo indicado
  digitalWrite(RELAY1, HIGH); // Apaga el relé
  bot.sendMessage(chat_id, "Rele apagado después de " + String(tiempoSegundos) + " segundos", "");
  estadoR1 = 0;
  R1estado = HIGH;
  modoR1 = MANUAL;
  Guardado_General();
}

void encenderRele2PorTiempo(int tiempoSegundos) {
  digitalWrite(RELAY2, LOW); // Enciende el relé
  delay(tiempoSegundos * 1000); // Mantiene encendido por el tiempo indicado
  digitalWrite(RELAY2, HIGH); // Apaga el relé
  bot.sendMessage(chat_id, "Rele apagado después de " + String(tiempoSegundos) + " segundos", "");
  estadoR2 = 0;
  R2estado = HIGH;
  modoR2 = MANUAL;
  Guardado_General();
}

void encenderRele2irPorTiempo(int tiempoSegundos) {
  irsend.sendRaw(IRsignal, 72, 38);  // Envía la señal IR ajustada con frecuencia de 38 kHz (Enciende el relé)
  delay(tiempoSegundos * 1000); // Mantiene encendido por el tiempo indicado
  irsend.sendRaw(IRsignal, 72, 38);  // Envía la señal IR ajustada con frecuencia de 38 kHz (Apaga el relé)
  bot.sendMessage(chat_id, "Rele apagado después de " + String(tiempoSegundos) + " segundos", "");
  estadoR2ir = 0;
  R2irestado = HIGH;
  modoR2ir = MANUAL;
  Guardado_General();
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

void encenderRele4PorTiempo(int tiempoSegundos) {
  digitalWrite(RELAY4, LOW); // Enciende el relé
  delay(tiempoSegundos * 1000); // Mantiene encendido por el tiempo indicado
  digitalWrite(RELAY4, HIGH); // Apaga el relé
  bot.sendMessage(chat_id, "Rele apagado después de " + String(tiempoSegundos) + " segundos", "");
  estadoR4 = 0;
  R4estado = HIGH;
  modoR4 = MANUAL;
  Guardado_General();
}

void startAccessPoint() {
  WiFi.mode(WIFI_AP);
  WiFi.softAP(ssid_AP, password_AP);
  Serial.println("Red WiFi creada con éxito");

  // Mostrar la IP del Access Point
  IPAddress IP = WiFi.softAPIP();
  Serial.print("IP del Access Point: ");
  Serial.println(IP);

  // Configuración del servidor web
  server.on("/", handleRoot);
  server.on("/config", handleConfig);
  server.on("/saveConfig", handleSaveConfig);
  server.on("/modificarValoresArray", handleModificarArray);
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
  server.send(200, "text/html", "<h1>Bienvenido al configurador del Druida Bot</h1><a href=\"/config\">Ir a Configuracion</a>");
}

void handleConfig() {
  String html = "<h1>Configuracion de ESP32</h1>";

    // Agregar configuraciones de WiFi
  html += "<h2>Configuracion de WiFi</h2>";
  html += "<form action=\"/saveConfig\" method=\"POST\">";
  html += "SSID: <input type=\"text\" name=\"ssid\" value=\"" + ssid + "\"><br>";
  html += "Password: <input type=\"password\" name=\"password\" value=\"" + password + "\"><br>";
  html += "Chat ID: <input type=\"text\" name=\"chat_id\" value=\"" + chat_id + "\"><br>";
  html += "<input type=\"submit\" name=\"save_wifi\" value=\"Guardar WiFi\">";
  html += "</form><br>";
  
  // Variables relacionadas con R1
  html += "<h2>Configuracion R1</h2>";
  html += "<form action=\"/saveConfig\" method=\"POST\">";
  html += "Modo R1: <input type=\"number\" name=\"modoR1\" value=\"" + String(modoR1) + "\"><br>";
  html += "Min R1: <input type=\"number\" step=\"0.01\" name=\"minR1\" value=\"" + String(minR1) + "\"><br>";
  html += "Max R1: <input type=\"number\" step=\"0.01\" name=\"maxR1\" value=\"" + String(maxR1) + "\"><br>";
  html += "Param R1: <input type=\"number\" name=\"paramR1\" value=\"" + String(paramR1) + "\"><br>";
  html += "Hora On R1: <input type=\"number\" name=\"horaOnR1\" value=\"" + String(horaOnR1) + "\"><br>";
  html += "Min On R1: <input type=\"number\" name=\"minOnR1\" value=\"" + String(minOnR1) + "\"><br>";
  html += "Hora Off R1: <input type=\"number\" name=\"horaOffR1\" value=\"" + String(horaOffR1) + "\"><br>";
  html += "Min Off R1: <input type=\"number\" name=\"minOffR1\" value=\"" + String(minOffR1) + "\"><br>";
  html += "Estado R1: <input type=\"number\" name=\"estadoR1\" value=\"" + String(estadoR1) + "\"><br>";
  html += "<input type=\"submit\" name=\"save_R1\" value=\"Guardar R1\">";
  html += "</form><br>";

  // Variables relacionadas con R2
  html += "<h2>Configuracion R2</h2>";
  html += "<form action=\"/saveConfig\" method=\"POST\">";
  html += "Modo R2: <input type=\"number\" name=\"modoR2\" value=\"" + String(modoR2) + "\"><br>";
  html += "Min R2: <input type=\"number\" step=\"0.01\" name=\"minR2\" value=\"" + String(minR2) + "\"><br>";
  html += "Max R2: <input type=\"number\" step=\"0.01\" name=\"maxR2\" value=\"" + String(maxR2) + "\"><br>";
  html += "Param R2: <input type=\"number\" name=\"paramR2\" value=\"" + String(paramR2) + "\"><br>";
  html += "Modo R2 IR: <input type=\"number\" name=\"modoR2ir\" value=\"" + String(modoR2ir) + "\"><br>";
  html += "Min R2 IR: <input type=\"number\" step=\"0.01\" name=\"minR2ir\" value=\"" + String(minR2ir) + "\"><br>";
  html += "Max R2 IR: <input type=\"number\" step=\"0.01\" name=\"maxR2ir\" value=\"" + String(maxR2ir) + "\"><br>";
  html += "Param R2 IR: <input type=\"number\" name=\"paramR2ir\" value=\"" + String(paramR2ir) + "\"><br>";
  html += "Estado R2 IR: <input type=\"number\" name=\"estadoR2ir\" value=\"" + String(estadoR2ir) + "\"><br>";
  html += "<input type=\"submit\" name=\"save_R2\" value=\"Guardar R2\">";
  html += "</form><br>";

  // Variables relacionadas con R3
  html += "<h2>Configuracion R3</h2>";
  html += "<form action=\"/saveConfig\" method=\"POST\">";
  html += "Modo R3: <input type=\"number\" name=\"modoR3\" value=\"" + String(modoR3) + "\"><br>";
  html += "Min R3: <input type=\"number\" name=\"minR3\" value=\"" + String(minR3) + "\"><br>";
  html += "Max R3: <input type=\"number\" name=\"maxR3\" value=\"" + String(maxR3) + "\"><br>";
  html += "Param R3: <input type=\"number\" name=\"paramR3\" value=\"" + String(paramR3) + "\"><br>";
  html += "Hora On R3: <input type=\"number\" name=\"horaOnR3\" value=\"" + String(horaOnR3) + "\"><br>";
  html += "Min On R3: <input type=\"number\" name=\"minOnR3\" value=\"" + String(minOnR3) + "\"><br>";
  html += "Hora Off R3: <input type=\"number\" name=\"horaOffR3\" value=\"" + String(horaOffR3) + "\"><br>";
  html += "Min Off R3: <input type=\"number\" name=\"minOffR3\" value=\"" + String(minOffR3) + "\"><br>";
  html += "Estado R3: <input type=\"number\" name=\"estadoR3\" value=\"" + String(estadoR3) + "\"><br>";
  html += "Dias de Riego R3 (0 o 1): <br>";
  for (int i = 0; i < 7; i++) {
    html += "Dia " + String(i + 1) + ": <input type=\"number\" name=\"diasRiego" + String(i) + "\" value=\"" + String(diasRiego[i]) + "\"><br>";
  }
  html += "<input type=\"submit\" name=\"save_R3\" value=\"Guardar R3\">";
  html += "</form><br>";

  // Variables relacionadas con R4
  html += "<h2>Configuracion R4</h2>";
  html += "<form action=\"/saveConfig\" method=\"POST\">";
  html += "Modo R4: <input type=\"number\" name=\"modoR4\" value=\"" + String(modoR4) + "\"><br>";
  html += "Hora On R4: <input type=\"number\" name=\"horaOnR4\" value=\"" + String(horaOnR4) + "\"><br>";
  html += "Min On R4: <input type=\"number\" name=\"minOnR4\" value=\"" + String(minOnR4) + "\"><br>";
  html += "Hora Off R4: <input type=\"number\" name=\"horaOffR4\" value=\"" + String(horaOffR4) + "\"><br>";
  html += "Min Off R4: <input type=\"number\" name=\"minOffR4\" value=\"" + String(minOffR4) + "\"><br>";
  html += "Estado R4: <input type=\"number\" name=\"estadoR4\" value=\"" + String(estadoR4) + "\"><br>";
  html += "<input type=\"submit\" name=\"save_R4\" value=\"Guardar R4\">";
  html += "</form><br>";

  // Agregar sección para modificar valores del array IR
  /*html += "<h2>Modificar Valores del Array IR</h2>";
  html += "<form action=\"/modificarValoresArray\" method=\"POST\">";
  html += "Seleccione el método de carga: <br>";
  html += "<input type=\"radio\" name=\"modoArray\" value=\"1\"> Carga manual<br>";
  html += "<input type=\"radio\" name=\"modoArray\" value=\"2\"> Carga automática con sensor IR<br>";
  html += "<input type=\"submit\" name=\"modificarArray\" value=\"Modificar Array\">";
  html += "</form><br>";*/




  server.send(200, "text/html", html);
}

void handleModificarArray() {
  if (server.hasArg("modoArray")) {
    String modoArray = server.arg("modoArray");

    if (modoArray == "1") {
      // Proceso de carga manual desde el menú web
      Serial.println("Modo de carga manual seleccionado.");

      // Aquí puedes modificar para permitir la entrada de valores manuales desde el menú
      // O podrías redirigir al monitor serial si prefieres seguir utilizando Serial para el input
      modificarValoresArray(true);  // Llama a la función con parámetro true para carga manual
    } else if (modoArray == "2") {
      // Proceso de carga automática usando el sensor IR
      Serial.println("Modo de carga automática con sensor IR seleccionado.");
      modificarValoresArray(false);  // Llama a la función con parámetro false para carga automática
    }
  }

  // Enviar mensaje de confirmación
  String html = "<h1>Modificacion de Array Completada</h1><a href=\"/\">Volver al inicio</a>";
  server.send(200, "text/html", html);
}

void handleSaveConfig() {
  // Guardar las configuraciones individuales de R1
  if (server.hasArg("save_R1")) {
    modoR1 = server.arg("modoR1").toInt();
    minR1 = server.arg("minR1").toFloat();
    maxR1 = server.arg("maxR1").toFloat();
    paramR1 = server.arg("paramR1").toInt();
    horaOnR1 = server.arg("horaOnR1").toInt();
    minOnR1 = server.arg("minOnR1").toInt();
    horaOffR1 = server.arg("horaOffR1").toInt();
    minOffR1 = server.arg("minOffR1").toInt();
    estadoR1 = server.arg("estadoR1").toInt();
  }

  // Guardar las configuraciones individuales de R2
  if (server.hasArg("save_R2")) {
    modoR2 = server.arg("modoR2").toInt();
    minR2 = server.arg("minR2").toFloat();
    maxR2 = server.arg("maxR2").toFloat();
    paramR2 = server.arg("paramR2").toInt();
    modoR2ir = server.arg("modoR2ir").toInt();
    minR2ir = server.arg("minR2ir").toFloat();
    maxR2ir = server.arg("maxR2ir").toFloat();
    paramR2ir = server.arg("paramR2ir").toInt();
    estadoR2ir = server.arg("estadoR1").toInt();
  }

  // Guardar las configuraciones individuales de R3
  if (server.hasArg("save_R3")) {
    modoR3 = server.arg("modoR3").toInt();
    minR3 = server.arg("minR3").toInt();
    maxR3 = server.arg("maxR3").toInt();
    paramR3 = server.arg("paramR3").toInt();
    horaOnR3 = server.arg("horaOnR3").toInt();
    minOnR3 = server.arg("minOnR3").toInt();
    horaOffR3 = server.arg("horaOffR3").toInt();
    minOffR3 = server.arg("minOffR3").toInt();
    estadoR3 = server.arg("estadoR3").toInt();
    for (int i = 0; i < 7; i++) {
      diasRiego[i] = server.arg("diasRiego" + String(i)).toInt();
    }
  }

  // Guardar las configuraciones individuales de R4
  if (server.hasArg("save_R4")) {
    modoR4 = server.arg("modoR4").toInt();
    horaOnR4 = server.arg("horaOnR4").toInt();
    minOnR4 = server.arg("minOnR4").toInt();
    horaOffR4 = server.arg("horaOffR4").toInt();
    minOffR4 = server.arg("minOffR4").toInt();
    estadoR4 = server.arg("estadoR4").toInt();
  }

  // Guardar configuraciones de WiFi
  if (server.hasArg("save_wifi")) {
    ssid = server.arg("ssid");
    password = server.arg("password");
    chat_id = server.arg("chat_id");
  }


  // Mensaje de confirmación
  String html = "<h1>Configuración Guardada</h1><a href=\"/\">Volver al inicio</a>";
  server.send(200, "text/html", html);
  Guardado_General();  // Función para guardar en EEPROM o similar
}

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

