
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


  connectToWiFi(ssid.c_str(), password.c_str());

  secured_client.setCACert(TELEGRAM_CERTIFICATE_ROOT);


  if (WiFi.status() == WL_CONNECTED) {
    String motivoReinicio = obtenerMotivoReinicio();
  
  // Enviar mensaje con el motivo del reinicio
  String message = "Druida Bot is ON (" + motivoReinicio + ")";
  bot.sendMessage(chat_id, message);
    String keyboardJson = "[[\"STATUS\"], [\"MANUAL\", \"AUTO\"], [\"CONFIG\", \"INFO CONFIG\"], [\"ENVIAR DATA GOOGLE\"], [\"RESET DRUIDA\"]]";
    bot.sendMessageWithReplyKeyboard(chat_id, "MENU PRINCIPAL:", "", keyboardJson, true);
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

  // Inicia el servidor web
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

//MODO RIEGO R3

unsigned long previousMillisRiego = 0;  // Variable para manejar el tiempo
int cicloRiegoActual = 0;               // Contador de ciclos de riego
bool enRiego = false;                   // Flag para saber si está en riego

if (modoR3 == RIEGO) {
  for (c = 0; c < 7; c++) {
    if (diasRiego[c] == 1) {
      if (c == day) {
        if (startR3 < offR3) { 
          // Caso normal: encendido antes que apagado
          if (currentTime >= startR3 && currentTime < offR3) {
            riegoIntermitente();  // Llamada a la función de riego
          } else {
            // Apagar el relé fuera del horario
            digitalWrite(RELAY3, HIGH);
            R3estado = HIGH;
            cicloRiegoActual = 0; // Reiniciar ciclos
            enRiego = false;
          }
        } else { 
          // Caso cruzando medianoche: encendido después que apagado
          if (currentTime >= startR3 || currentTime < offR3) {
            riegoIntermitente();  // Llamada a la función de riego
          } else {
            // Apagar el relé fuera del horario
            digitalWrite(RELAY3, HIGH);
            R3estado = HIGH;
            cicloRiegoActual = 0; // Reiniciar ciclos
            enRiego = false;
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




 // Crear la variable 'dateTime' para usar en mostrarEnPantallaOLED
String hora = formatoHora(hour, now.minute());

mostrarEnPantallaOLED(temperature, humedad, DPV, hora);
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

  for (int i = 0; i < 100; i++) {
    IRsignal[i] = EEPROM.get(350 + i * sizeof(uint16_t), IRsignal[i]);
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

  
  for (int i = 0; i < 100; i++) {
    EEPROM.put(350 + i * sizeof(uint16_t), IRsignal[i]);
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
  while (millis() - startAttemptTime < 20000) {
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

    // Mostrar mensaje de éxito en la pantalla OLED
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.println("WiFi conectado");
    display.print("IP: ");
    display.println(WiFi.localIP());

    // Dibujar el logo WiFi
    display.drawCircle(110, 20, 10, SSD1306_WHITE);
    display.drawCircle(110, 20, 7, SSD1306_WHITE);
    display.drawCircle(110, 20, 4, SSD1306_WHITE);
    display.drawLine(108, 30, 112, 30, SSD1306_WHITE);
    display.display();

  } else {
    Serial.println("\nNo se pudo conectar a WiFi. Reintentando...");

    // Reintentar conexión
    for (int attempt = 1; attempt <= 3; ++attempt) {
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

      // Mostrar mensaje de éxito en la pantalla OLED
      display.clearDisplay();
      display.setTextSize(1);
      display.setTextColor(SSD1306_WHITE);
      display.setCursor(0, 0);
      display.println("WiFi conectado");
      display.print("IP: ");
      display.println(WiFi.localIP());

      // Dibujar el logo WiFi
      display.drawCircle(110, 20, 10, SSD1306_WHITE);
      display.drawCircle(110, 20, 7, SSD1306_WHITE);
      display.drawCircle(110, 20, 4, SSD1306_WHITE);
      display.drawLine(108, 30, 112, 30, SSD1306_WHITE);
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

// Cambios en handleRoot
void handleRoot() {
    String html = "<html><head><style>";
    html += "body { background-color: #00264d; color: white; font-family: Arial, sans-serif; text-align: center; padding-top: 15%; margin: 0; }";
    html += "h1 { font-size: 800%; margin: 0 auto; line-height: 1.2; animation: fadeIn 2s ease-in-out; }";
    html += "h1 span { display: block; }";
    html += "button { background-color: #1c75bc; color: white; border: 2px solid #00ff00; padding: 15px 40px; font-size: 24px; border-radius: 10px; cursor: pointer; display: inline-block; opacity: 0; transform: scale(0.9); animation: fadeInScale 1s ease-in-out forwards; margin-top: 30px; }";
    html += "button:hover { background-color: #004080; border-color: #00cc00; }";
    html += "@keyframes fadeIn { from { opacity: 0; } to { opacity: 1; } }";
    html += "@keyframes fadeInScale { 0% { opacity: 0; transform: scale(0.9); } 100% { opacity: 1; transform: scale(1); } }";
    html += "</style></head><body>";
    html += "<h1><span>Data</span><span>Druida</span></h1>";
    html += "<a href=\"/config\"><button>Configuracion</button></a>";
    html += "</body></html>";

    server.send(200, "text/html", html);
}



// Cambios en handleConfig
void handleConfig() {
  String html = "<html><head><style>";
  html += "body { background-color: #00264d; color: white; font-family: Arial, sans-serif; text-align: center; padding: 20px; }";
  html += "h1, h2 { color: #00bfff; }";
  html += "form { display: inline-block; text-align: left; margin: 10px auto; padding: 20px; border: 1px solid #00bfff; border-radius: 10px; background-color: #004080; }";
  html += "input, textarea { width: 90%; padding: 10px; margin: 5px 0; border: 1px solid #00bfff; border-radius: 5px; font-size: 16px; }";
  html += "input[type=\"submit\"] { width: auto; background-color: #007bff; color: white; border: none; padding: 10px 20px; font-size: 16px; cursor: pointer; border-radius: 5px; }";
  html += "input[type=\"submit\"]:hover { background-color: #0056b3; }";
  html += "</style></head><body>";

  html += "<h1>Configuracion de Druida BOT</h1>";

  // Sección de configuración de WiFi
  html += "<h2>Configuracion de WiFi</h2>";
  html += "<form action=\"/saveConfig\" method=\"POST\">";
  html += "SSID: <input type=\"text\" name=\"ssid\" value=\"" + ssid + "\"><br>";
  html += "Password: <input type=\"password\" name=\"password\" value=\"" + password + "\"><br>";
  html += "Chat ID: <input type=\"text\" name=\"chat_id\" value=\"" + chat_id + "\"><br>";
  html += "<input type=\"submit\" name=\"save_wifi\" value=\"Guardar WiFi\">";
  html += "</form><br>";

  // Configuración R1
  html += "<h2>Configuracion R1 (Up)</h2>";
  html += "<form action=\"/saveConfig\" method=\"POST\">";
  html += "Modo R1: <input type=\"number\" name=\"modoR1\" value=\"" + String(modoR1) + "\"><br>";
  html += "Min R1: <input type=\"number\" step=\"0.01\" name=\"minR1\" value=\"" + String(minR1) + "\"><br>";
  html += "Max R1: <input type=\"number\" step=\"0.01\" name=\"maxR1\" value=\"" + String(maxR1) + "\"><br>";
  html += "Param R1: <input type=\"number\" name=\"paramR1\" value=\"" + String(paramR1) + "\"><br>";
  html += "Hora On R1 (HH:MM): <input type=\"text\" name=\"horaOnR1\" value=\"" + String(horaOnR1) + ":" + String(minOnR1) + "\"><br>";
  html += "Hora Off R1 (HH:MM): <input type=\"text\" name=\"horaOffR1\" value=\"" + String(horaOffR1) + ":" + String(minOffR1) + "\"><br>";
  html += "Estado R1: <input type=\"number\" name=\"estadoR1\" value=\"" + String(estadoR1) + "\"><br>";
  html += "<input type=\"submit\" name=\"save_R1\" value=\"Guardar R1\">";
  html += "</form><br>";

  // Configuración R2
  html += "<h2>Configuracion R2 (Down)</h2>";
  html += "<form action=\"/saveConfig\" method=\"POST\">";
  html += "Modo R2: <input type=\"number\" name=\"modoR2\" value=\"" + String(modoR2) + "\"><br>";
  html += "Min R2: <input type=\"number\" step=\"0.01\" name=\"minR2\" value=\"" + String(minR2) + "\"><br>";
  html += "Max R2: <input type=\"number\" step=\"0.01\" name=\"maxR2\" value=\"" + String(maxR2) + "\"><br>";
  html += "Param R2: <input type=\"number\" name=\"paramR2\" value=\"" + String(paramR2) + "\"><br>";
  //html += "Hora On R2 (HH:MM): <input type=\"text\" name=\"horaOnR2\" value=\"" + String(horaOnR2) + ":" + String(minOnR2) + "\"><br>";
  //html += "Hora Off R2 (HH:MM): <input type=\"text\" name=\"horaOffR2\" value=\"" + String(horaOffR2) + ":" + String(minOffR2) + "\"><br>";
  html += "Estado R2: <input type=\"number\" name=\"estadoR2\" value=\"" + String(estadoR2) + "\"><br>";
  html += "<input type=\"submit\" name=\"save_R2\" value=\"Guardar R2\">";
  html += "</form><br>";

  // Configuración R2 IR
  html += "<h2>Configuracion R2 IR (Down)</h2>";
  html += "<form action=\"/saveConfig\" method=\"POST\">";
  html += "Modo R2 (IR): <input type=\"number\" name=\"modoR2ir\" value=\"" + String(modoR2ir) + "\"><br>";
  html += "Min R2 (IR): <input type=\"number\" step=\"0.01\" name=\"minR2ir\" value=\"" + String(minR2ir) + "\"><br>";
  html += "Max R2 (IR): <input type=\"number\" step=\"0.01\" name=\"maxR2ir\" value=\"" + String(maxR2ir) + "\"><br>";
  html += "Param R2 (IR): <input type=\"number\" name=\"paramR2ir\" value=\"" + String(paramR2ir) + "\"><br>";
  //html += "Hora On R2 (IR) (HH:MM): <input type=\"text\" name=\"horaOnR2ir\" value=\"" + String(horaOnR2ir) + ":" + String(minOnR2ir) + "\"><br>";
  //html += "Hora Off R2 (IR) (HH:MM): <input type=\"text\" name=\"horaOffR2ir\" value=\"" + String(horaOffR2ir) + ":" + String(minOffR2ir) + "\"><br>";
  html += "Estado R2 (IR): <input type=\"number\" name=\"estadoR2ir\" value=\"" + String(estadoR2ir) + "\"><br>";
  html += "<input type=\"submit\" name=\"save_R2IR\" value=\"Guardar R2 IR\">";
  html += "</form><br>";

  html += "<h2>Configuracion R3 (Riego)</h2>";
  html += "<form action=\"/saveConfig\" method=\"POST\">";
  html += "Modo R3: <input type=\"number\" name=\"modoR3\" value=\"" + String(modoR3) + "\"><br>";
  html += "Min R3: <input type=\"number\" step=\"0.01\" name=\"minR3\" value=\"" + String(minR3) + "\"><br>";
  html += "Max R3: <input type=\"number\" step=\"0.01\" name=\"maxR3\" value=\"" + String(maxR3) + "\"><br>";
  html += "Param R3: <input type=\"number\" name=\"paramR3\" value=\"" + String(paramR3) + "\"><br>";
  html += "Hora On R3 (HH:MM): <input type=\"text\" name=\"horaOnR3\" value=\"" + String(horaOnR3) + ":" + String(minOnR3) + "\"><br>";
  html += "Hora Off R3 (HH:MM): <input type=\"text\" name=\"horaOffR3\" value=\"" + String(horaOffR3) + ":" + String(minOffR3) + "\"><br>";
  html += "Estado R3: <input type=\"number\" name=\"estadoR3\" value=\"" + String(estadoR3) + "\"><br>";
  html += "Tiempo Riego (seg): <input type=\"number\" name=\"tiempoRiego\" value=\"" + String(tiempoRiego) + "\"><br>";
  html += "Tiempo No Riego (seg): <input type=\"number\" name=\"tiempoNoRiego\" value=\"" + String(tiempoNoRiego) + "\"><br>";
  html += "Cantidad de Riegos: <input type=\"number\" name=\"cantidadRiegos\" value=\"" + String(cantidadRiegos) + "\"><br>";
  html += "<input type=\"submit\" name=\"save_R3\" value=\"Guardar R3\">";
  html += "</form><br>";

  // Configuración R4
  html += "<h2>Configuracion R4 (Luz)</h2>";
  html += "<form action=\"/saveConfig\" method=\"POST\">";
  html += "Modo R4: <input type=\"number\" name=\"modoR4\" value=\"" + String(modoR4) + "\"><br>";
  //html += "Min R4: <input type=\"number\" step=\"0.01\" name=\"minR4\" value=\"" + String(minR4) + "\"><br>";
  //html += "Max R4: <input type=\"number\" step=\"0.01\" name=\"maxR4\" value=\"" + String(maxR4) + "\"><br>";
  //html += "Param R4: <input type=\"number\" name=\"paramR4\" value=\"" + String(paramR4) + "\"><br>";
  html += "Hora On R4 (HH:MM): <input type=\"text\" name=\"horaOnR4\" value=\"" + String(horaOnR4) + ":" + String(minOnR4) + "\"><br>";
  html += "Hora Off R4 (HH:MM): <input type=\"text\" name=\"horaOffR4\" value=\"" + String(horaOffR4) + ":" + String(minOffR4) + "\"><br>";
  html += "Estado R4: <input type=\"number\" name=\"estadoR4\" value=\"" + String(estadoR4) + "\"><br>";
  html += "<input type=\"submit\" name=\"save_R4\" value=\"Guardar R4\">";
  html += "</form><br>";

    // Modificar valores del Array IR
  html += "<h2>Modificar Valores del Array IR</h2>";
  html += "<form action=\"/modificarArray\" method=\"POST\">";
  html += "<textarea name=\"arrayIR\" rows=\"4\" cols=\"50\" placeholder=\"Ingrese los valores separados por comas\"></textarea><br>";
  html += "<input type=\"submit\" value=\"Actualizar Array IR\">";
  html += "</form><br>";

  html += "</body></html>";
  server.send(200, "text/html", html);
}

// Función para descomponer el formato HH:MM y guardar en variables separadas
void guardarHora(String horaStr, int &hora, int &minuto) {
  int separador = horaStr.indexOf(':');
  if (separador != -1) {
    hora = horaStr.substring(0, separador).toInt();
    minuto = horaStr.substring(separador + 1).toInt();
  } else {
    hora = 0;
    minuto = 0;
  }
}




void handleModificarArray() {
  String html = "<h1>Modificación del Array IR</h1>";

  if (server.hasArg("modoArray")) {
    String modoArray = server.arg("modoArray");

    if (modoArray == "1" && server.hasArg("valoresArray")) {
      // Proceso de carga manual
      String valores = server.arg("valoresArray");
      // Separar los valores por comas y almacenarlos en el array
      int indice = 0;
      int posicion = 0;
      while ((posicion = valores.indexOf(',', indice)) != -1 && indice < 150) {
        IRsignal[indice] = valores.substring(indice, posicion).toInt();
        indice++;
      }
      html += "<p>Valores ingresados manualmente:</p><p>";
      for (int i = 0; i < indice; i++) {
        html += String(IRsignal[i]) + ", ";
      }
      html += "</p>";
      modificarValoresArray(true);
    } else if (modoArray == "2") {
      // Proceso de carga automática usando el sensor IR
      html += "<p>Modo de carga automática seleccionado.</p>";
      html += "<p>Apunte el control remoto hacia el sensor y presione el botón deseado.</p>";
      modificarValoresArray(false);  // Llama a la función para la carga automática
    }
  } else {
    html += "<p>Error: No se seleccionó un modo de carga.</p>";
  }

  html += "<a href=\"/\">Volver al inicio</a>";
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
    tiempoRiego = server.arg("tiempoRiego").toInt();  // Guardar tiempo de riego
    tiempoNoRiego = server.arg("tiempoNoRiego").toInt();  // Guardar tiempo sin riego
    cantidadRiegos = server.arg("cantidadRiegos").toInt();  // Guardar cantidad de riegos
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
  handleConfirmation();
  Guardado_General();  // Función para guardar en EEPROM o similar
}

// Mensaje de confirmación
void handleConfirmation() {
  String html = "<html><head><style>";
  html += "body { background-color: #00264d; color: white; font-family: Arial, sans-serif; text-align: center; padding-top: 20%; }";
  html += "div { background-color: #004080; border: 2px solid #00bfff; border-radius: 10px; padding: 20px; display: inline-block; text-align: center; }";
  html += "a { color: #00bfff; text-decoration: none; font-size: 20px; margin-top: 10px; display: inline-block; }";
  html += "a:hover { text-decoration: underline; }";
  html += "</style></head><body>";
  html += "<div><h1>Configuracion Guardada</h1><a href=\"/\">Volver al inicio</a></div>";
  html += "</body></html>";
  server.send(200, "text/html", html);
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
  display.print("DPV: ");
  display.print(dpvDisplay);
  display.setTextSize(1); 
  display.print("hPa");

  // Mostrar hora (solo horas y minutos, tamaño 1)
  display.setTextSize(1);       // Cambiar el tamaño a 1 para la hora
  display.setCursor(95, 57);     // Posición para la hora
  display.print(hora);
  
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
        case AUTOINT:
            return "Auto Intermitente";
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

  if (cicloRiegoActual < cantidadRiegos) {  // Verificar si aún no completamos los ciclos
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
        cicloRiegoActual++;  // Incrementar el contador de ciclos
      }
    }
  } else {
    // Finalizar todos los ciclos de riego
    digitalWrite(RELAY3, HIGH);
    R3estado = HIGH;
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
