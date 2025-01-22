
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
    server.handleClient();
    esp_task_wdt_reset(); // Maneja las solicitudes HTTP en modo AP
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
        esp_task_wdt_reset();
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
      esp_task_wdt_reset();
    }

    if (estadoR1 == 0 && R1estado == LOW) {
      digitalWrite(RELAY1, HIGH);
      R1estado = HIGH;
      esp_task_wdt_reset();
    }
  }

  //MODO MANUAL R2
  if (modoR2 == MANUAL) {
    if (estadoR2 == 1 && R2estado == HIGH) {
      digitalWrite(RELAY2, LOW);
      R2estado = LOW;
      esp_task_wdt_reset();
    }
    if (estadoR2 == 0 && R2estado == LOW) {
      digitalWrite(RELAY2, HIGH);
      R2estado = HIGH;
      esp_task_wdt_reset();
    }
  }

    //MODO MANUAL R2 IR
 /* if (modoR2ir == MANUAL) {
    if (estadoR2ir == 1 && R2irestado == HIGH) {
      irsend.sendRaw(IRsignal, 72, 38);  // Envía la señal IR ajustada con frecuencia de 38 kHz
      R2irestado = LOW;
      esp_task_wdt_reset();
    }
    if (estadoR2ir == 0 && R2irestado == LOW) {
      irsend.sendRaw(IRsignal, 72, 38);  // Envía la señal IR ajustada con frecuencia de 38 kHz
      R2irestado = HIGH;
      esp_task_wdt_reset();
    }
  }*/

  //MODO MANUAL R3
  if (modoR3 == MANUAL) {
    if (estadoR3 == 1 && R3estado == HIGH) {
      digitalWrite(RELAY3, LOW);
      R3estado = LOW;
      esp_task_wdt_reset();
    }
    if (estadoR3 == 0 && R3estado == LOW) {
      digitalWrite(RELAY3, HIGH);
      R3estado = HIGH;
      esp_task_wdt_reset();
    }
  }

  //MODO MANUAL R4
  if (modoR4 == MANUAL) {
    if (estadoR4 == 1 && R4estado == HIGH) {
      digitalWrite(RELAY4, LOW);
      R4estado = LOW;
      esp_task_wdt_reset();
    }
    if (estadoR4 == 0 && R4estado == LOW) {
      digitalWrite(RELAY4, HIGH);
      R4estado = HIGH;
      esp_task_wdt_reset();
    }
  }

  //MODO AUTO R1 (UP) :

  if (modoR1 == AUTO) {
    //Serial.print("Rele (UP) Automatico");

    if (paramR1 == H) {
      if (humedad < minR1 && R1estado == HIGH) {
        digitalWrite(RELAY1, LOW);
        R1estado = LOW;
        esp_task_wdt_reset();
      }
      if (humedad > maxR1 && R1estado == LOW) {
        digitalWrite(RELAY1, HIGH);
        R1estado = HIGH;
        esp_task_wdt_reset();
      }
    }
    if (paramR1 == T) {
      if (temperature < minR1 && R1estado == HIGH) {
        digitalWrite(RELAY1, LOW);
        R1estado = LOW;
        esp_task_wdt_reset();
      }
      if (temperature > minR1 && R1estado == LOW) {
        digitalWrite(RELAY1, HIGH);
        R1estado = HIGH;
        esp_task_wdt_reset();
      }
    }

    if (paramR1 == D) {
      if (DPV < minR1 && R1estado == HIGH) {
        digitalWrite(RELAY1, LOW);
        R1estado = LOW;
        esp_task_wdt_reset();
      }


        if (DPV > maxR1 && R1estado == LOW) {
          digitalWrite(RELAY1, HIGH);
          R1estado = HIGH;
          esp_task_wdt_reset();
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
        esp_task_wdt_reset();
      }
    } else {
      if (R1estado == LOW) {
        digitalWrite(RELAY1, HIGH);
        R1estado = HIGH;
        esp_task_wdt_reset();
      }
    }
  } else { 
    // Caso cruzando medianoche: encendido después que apagado
    if (currentTime >= startR1 || currentTime < offR1) {
      if (R1estado == HIGH){ 
        digitalWrite(RELAY1, LOW);
        R1estado = LOW;
        esp_task_wdt_reset();
      }
    } else {
      if (R1estado == LOW) {
        digitalWrite(RELAY1, HIGH);
        R1estado = HIGH;
        esp_task_wdt_reset();
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
        esp_task_wdt_reset();
        delay(200);
      }
      if (humedad < minR2 && R2estado == LOW) {
        digitalWrite(RELAY2, HIGH);
        R2estado = HIGH;
        esp_task_wdt_reset();
        delay(200);
      }
    }
    if (paramR2 == T) {
      if (temperature > maxR2 && R2estado == HIGH) {
        digitalWrite(RELAY2, LOW);
        R2estado = LOW;
        esp_task_wdt_reset();
        delay(200);
      }
      if (temperature < minR2 && R2estado == LOW) {
        digitalWrite(RELAY2, HIGH);
        R2estado = HIGH;
        esp_task_wdt_reset();
        delay(200);
      }
    }

    if (paramR2 == D) {
      if (DPV > maxR2 && R2estado == HIGH) {
        digitalWrite(RELAY2, LOW);
        R2estado = LOW;
        esp_task_wdt_reset();
        delay(200);
      }
      if (DPV < minR2 && R2estado == LOW) {
        digitalWrite(RELAY2, HIGH);
        R2estado = HIGH;
        esp_task_wdt_reset();
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


  /*//MODO AUTO R2   IR    (DOWN)

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
    }
  }*/


  /*if (modoR3 == AUTO) {
    for (c = 0; c < 7; c++) {
      if (diasRiego[c] == 1) {
        if (c == day) {
          if (startR3 < offR3) { 
            // Caso normal: encendido antes que apagado
            if (currentTime >= startR3 && currentTime < offR3) {
              if (R3estado == HIGH){ 
                digitalWrite(RELAY3, LOW);
                R3estado = LOW;
                esp_task_wdt_reset();
              }
            } else {
              if (R3estado == LOW) {
                digitalWrite(RELAY3, HIGH);
                R3estado = HIGH;
                esp_task_wdt_reset();
              }
            }
          } else { 
            // Caso cruzando medianoche: encendido después que apagado
            if (currentTime >= startR3 || currentTime < offR3) {
              if (R3estado == HIGH){ 
                digitalWrite(RELAY3, LOW);
                R3estado = LOW;
                esp_task_wdt_reset();
              }
            } else {
              if (R3estado == LOW) {
                digitalWrite(RELAY3, HIGH);
                R3estado = HIGH;
                esp_task_wdt_reset();
              }
            }
          }
        }
      }
    }
  }*/

//MODO RIEGO R3

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
            esp_task_wdt_reset();
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
            esp_task_wdt_reset();
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
        esp_task_wdt_reset();
      }
    } else {
      if (R4estado == LOW) {
        digitalWrite(RELAY4, HIGH);
        R4estado = HIGH;
        esp_task_wdt_reset();
      }
    }
  } else {
    // Caso cruzando medianoche: encendido después que apagado
    if (currentTime >= startR4 || currentTime < offR4) {
      if (R4estado == HIGH) {
        digitalWrite(RELAY4, LOW);
        R4estado = LOW;
        esp_task_wdt_reset();
      }
    } else {
      if (R4estado == LOW) {
        digitalWrite(RELAY4, HIGH);
        R4estado = HIGH;
        esp_task_wdt_reset();
      }
    }
  }

  // Control del servomotor (manejo de horarios cruzados)
  bool dentroAmanecer = (horaAmanecer < horaAtardecer) 
                          ? (currentTime >= horaAmanecer && currentTime < horaAtardecer)
                          : (currentTime >= horaAmanecer || currentTime < horaAtardecer);

  if (dentroAmanecer) {
    moveServoSlowly(180); // Simula el mediodía
    esp_task_wdt_reset();
  } else {
    moveServoSlowly(0); // Simula amanecer o atardecer
    esp_task_wdt_reset();
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
  esp_task_wdt_reset();
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
  //server.on("/modificarValoresArray", handleModificarArray);
  server.on("/control", handleControl); // Ruta para el panel de control
  server.on("/controlR1", handleControlR1);       // Menú de control para R1
  server.on("/controlR1On", handleControlR1On);   // Cambiar estado a encendido
  server.on("/controlR1Off", handleControlR1Off); // Cambiar estado a apagado
  server.on("/controlR1Auto", handleControlR1Auto);   // Activar modo automático
  server.on("/controlR2", handleControlR2);       // Menú de control para R2
  server.on("/controlR2On", handleControlR2On);   // Cambiar estado a encendido
  server.on("/controlR2Off", handleControlR2Off); // Cambiar estado a apagado
  server.on("/controlR2Auto", handleControlR2Auto);   // Activar modo automático
  server.on("/controlR3", handleControlR3);       // Menú de control para R3
  server.on("/controlR3On", handleControlR3On);   // Cambiar estado a encendido
  server.on("/controlR3Off", handleControlR3Off); // Cambiar estado a apagado
  server.on("/controlR3Auto", handleControlR3Auto);   // Activar modo automático
  server.on("/controlR3OnFor", handleControlR3OnFor); // Encender por tiempo
  server.on("/controlR4", handleControlR4);       // Menú de control para R4
  server.on("/controlR4On", handleControlR4On);   // Cambiar estado a encendido
  server.on("/controlR4Off", handleControlR4Off); // Cambiar estado a apagado
  server.on("/controlR4Auto", handleControlR4Auto);   // Activar modo automático
  server.on("/configR1", handleConfigR1);
  server.on("/configR2", handleConfigR2);
  server.on("/configR3", handleConfigR3);
  server.on("/configR4", handleConfigR4);






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
    String dateTime = "Fecha y Hora: " + String(now.day()) + "/" + String(now.month()) + "/" + String(now.year()) + " " + horaBot + ":" + String(now.minute()) + ":" + String(now.second());

    // Construir mensaje de estado
    String statusMessage = "<div class='line'>Temp: " + String(temperature, 1) + " °C</div>";
    statusMessage += "<div class='line'>Hum: " + String(humedad, 1) + " %</div>";
    statusMessage += "<div class='line'>DPV: " + String(DPV, 1) + " hPa</div>";
    statusMessage += "<div class='line'>" + dateTime + "</div>";

    // Generar el HTML
    String html = "<html><head><style>";
    html += "body { background-color: #00264d; color: white; font-family: Arial, sans-serif; margin: 0; height: 100vh; display: flex; flex-direction: column; justify-content: space-between; align-items: center; text-align: center; }";
    html += "header { margin-top: 20px; }";
    html += "header h1 { font-size: 8rem; margin: 0; line-height: 1.2; color: white; font-family: 'Press Start 2P', monospace; animation: fadeIn 2s ease-in-out; }";
    html += "@keyframes fadeIn { from { opacity: 0; } to { opacity: 1; } }";
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

    html += "<header><h1>Data Druida</h1></header>";
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
    html += "body { background-color: #00264d; color: white; font-family: Arial, sans-serif; margin: 0; height: 100vh; display: flex; justify-content: center; align-items: center; flex-direction: column; text-align: center; }";
    html += "h1 { color: #00bfff; margin-bottom: 30px; }";
    html += "button { background-color: #1c75bc; color: white; border: 2px solid #00ff00; padding: 30px 80px; font-size: 48px; border-radius: 20px; cursor: pointer; margin: 15px; display: inline-block; }";
    html += "button:hover { background-color: #004080; border-color: #00cc00; }";
    html += "</style></head><body>";
    html += "<h1>Panel de Control</h1>";

    // Botones dinámicos para cada relé
    html += "<a href=\"/controlR1\"><button>Control de " + getRelayName(R1name) + "</button></a>";
    html += "<a href=\"/controlR2\"><button>Control de " + getRelayName(R2name) + "</button></a>";
    html += "<a href=\"/controlR3\"><button>Control de " + getRelayName(R3name) + "</button></a>";
    html += "<a href=\"/controlR4\"><button>Control de " + getRelayName(R4name) + "</button></a>";

    // Botón para volver al menú principal
    html += "<a href=\"/\"><button>Volver</button></a>";
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

    // Botón para volver al panel de control
    html += "<a href=\"/control\"><button>Volver</button></a>";
    html += "</body></html>";

    server.send(200, "text/html", html);
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
    estadoR4 = 2; // Modo automático
    Guardado_General();
    handleConfirmation(getRelayName(R4name) + " en modo automatico", "/controlR4"); // Mostrar confirmación y redirigir
}






void handleConfig() {
    String html = "<html><head><style>";
    html += "body { background-color: #00264d; color: white; font-family: Arial, sans-serif; margin: 0; height: 100vh; display: flex; justify-content: center; align-items: center; }";
    html += "h1 { color: #00bfff; margin-bottom: 30px; font-size: 36px; }";
    html += ".container { text-align: center; }";
    html += "button { background-color: #1c75bc; color: white; border: 2px solid #00ff00; padding: 20px 60px; font-size: 36px; border-radius: 20px; cursor: pointer; margin: 20px; display: inline-block; }";
    html += "button:hover { background-color: #004080; border-color: #00cc00; }";
    html += "</style></head><body>";

    
    html += "<div class=\"container\">";
    html += "<h1>Configuracion de Druida BOT</h1>";

    // Botones de submenú para cada relé
    html += "<a href=\"/configR1\"><button>" + getRelayName(R1name) + "</button></a><br>";
    html += "<a href=\"/configR2\"><button>" + getRelayName(R2name) + "</button></a><br>";
    html += "<a href=\"/configR3\"><button>" + getRelayName(R3name) + "</button></a><br>";
    html += "<a href=\"/configR4\"><button>" + getRelayName(R4name) + "</button></a><br>";

    // Botón de volver
    html += "<a href=\"/\"><button>Volver</button></a>";
    html += "</div>";

    html += "</body></html>";
    server.send(200, "text/html", html);
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
    html += "<label for=\"modoR1\">Modo:</label>";
    html += "<input type=\"number\" id=\"modoR1\" name=\"modoR1\" value=\"" + String(modoR1) + "\">";

    html += "<label for=\"minR1\">Min:</label>";
    html += "<input type=\"number\" step=\"0.01\" id=\"minR1\" name=\"minR1\" value=\"" + String(minR1) + "\">";

    html += "<label for=\"maxR1\">Max:</label>";
    html += "<input type=\"number\" step=\"0.01\" id=\"maxR1\" name=\"maxR1\" value=\"" + String(maxR1) + "\">";

    html += "<label for=\"paramR1\">Param:</label>";
    html += "<input type=\"number\" id=\"paramR1\" name=\"paramR1\" value=\"" + String(paramR1) + "\">";

    html += "<label for=\"horaOnR1\">Hora On (HH:MM):</label>";
    html += "<input type=\"text\" id=\"horaOnR1\" name=\"horaOnR1\" value=\"" + formatTwoDigits(horaOnR1) + ":" + formatTwoDigits(minOnR1) + "\">";

    html += "<label for=\"horaOffR1\">Hora Off (HH:MM):</label>";
    html += "<input type=\"text\" id=\"horaOffR1\" name=\"horaOffR1\" value=\"" + formatTwoDigits(horaOffR1) + ":" + formatTwoDigits(minOffR1) + "\">";

    html += "<label for=\"estadoR1\">Estado:</label>";
    html += "<input type=\"number\" id=\"estadoR1\" name=\"estadoR1\" value=\"" + String(estadoR1) + "\">";

    html += "<input type=\"submit\" value=\"Guardar\">";
    html += "</form>";
    html += "<button onclick=\"window.location.href='/config'\">Volver</button>";
    html += "</div>";

    html += "</body></html>";

    server.send(200, "text/html", html);
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
    html += "<label>Modo:</label><input type=\"number\" name=\"modoR2\" value=\"" + String(modoR2) + "\">";
    html += "<label>Min:</label><input type=\"number\" step=\"0.01\" name=\"minR2\" value=\"" + String(minR2) + "\">";
    html += "<label>Max:</label><input type=\"number\" step=\"0.01\" name=\"maxR2\" value=\"" + String(maxR2) + "\">";
    html += "<label>Param:</label><input type=\"number\" name=\"paramR2\" value=\"" + String(paramR2) + "\">";
    html += "<label>Estado:</label><input type=\"number\" name=\"estadoR2\" value=\"" + String(estadoR2) + "\">";
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
    html += "<label>Modo:</label><input type=\"number\" name=\"modoR3\" value=\"" + String(modoR3) + "\">";
    html += "<label>Hora On (HH:MM):</label><input type=\"text\" name=\"horaOnR3\" value=\"" + formatTwoDigits(horaOnR3) + ":" + formatTwoDigits(minOnR3) + "\">";
    html += "<label>Hora Off (HH:MM):</label><input type=\"text\" name=\"horaOffR3\" value=\"" + formatTwoDigits(horaOffR3) + ":" + formatTwoDigits(minOffR3) + "\">";
    html += "<label>Duracion de Riego (seg):</label><input type=\"number\" name=\"tiempoRiego\" value=\"" + String(tiempoRiego) + "\">";
    html += "<label>Intervalo de Riego (seg):</label><input type=\"number\" name=\"tiempoNoRiego\" value=\"" + String(tiempoNoRiego) + "\">";
    html += "<label>Estado:</label><input type=\"number\" name=\"estadoR3\" value=\"" + String(estadoR3) + "\">";
    html += "<input type=\"submit\" value=\"Guardar\">";
    html += "</form>";
    html += "<button onclick=\"window.location.href='/config'\">Volver</button>";
    html += "</div>";

    html += "</body></html>";

    server.send(200, "text/html", html);
}

void handleConfigR4() {
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
    html += "<label>Modo:</label><input type=\"number\" name=\"modoR4\" value=\"" + String(modoR4) + "\">";
    html += "<label>Hora On (HH:MM):</label><input type=\"text\" name=\"horaOnR4\" value=\"" + formatTwoDigits(horaOnR4) + ":" + formatTwoDigits(minOnR4) + "\">";
    html += "<label>Hora Off (HH:MM):</label><input type=\"text\" name=\"horaOffR4\" value=\"" + formatTwoDigits(horaOffR4) + ":" + formatTwoDigits(minOffR4) + "\">";
    html += "<label>Estado:</label><input type=\"number\" name=\"estadoR4\" value=\"" + String(estadoR4) + "\">";
    html += "<input type=\"submit\" value=\"Guardar\">";
    html += "</form>";
    html += "<button onclick=\"window.location.href='/config'\">Volver</button>";
    html += "</div>";

    html += "</body></html>";

    server.send(200, "text/html", html);
}





void handleSaveConfig() {
    // Guardar configuraciones (ya definido en tu código)
    Guardado_General();

    // Mostrar mensaje de confirmación
    handleConfirmation("Configuracion guardada correctamente", "/config");
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

  if (!enRiego) { // Si está en pausa
    if (currentMillis - previousMillisRiego >= tiempoNoRiego * 1000) {
      // Encender el relé
      digitalWrite(RELAY3, LOW);
      R3estado = LOW;
      esp_task_wdt_reset();
      previousMillisRiego = currentMillis;
      enRiego = true;
    }
  } else { // Si está en riego
    if (currentMillis - previousMillisRiego >= tiempoRiego * 1000) {
      // Apagar el relé
      digitalWrite(RELAY3, HIGH);
      R3estado = HIGH;
      esp_task_wdt_reset();
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