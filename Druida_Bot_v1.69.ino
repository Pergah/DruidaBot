/* Druida BOT 1.69

El programa es capaz de controlar 4 reles independientes. 
Pueden funcionar en modo Manual, o Automatico.
Rele 1: Sube parametro (H, T, D)
Rele 2: Baja parametro (H, T, D) 
Rele 3: Timer diario (Ej: Luz)
Rele 4: Timer diario + semanal (Ej: Riego)

La red WiFi se puede cambiar desde el serial
El chat ID se puede cambiar desde el serial
Solucionado, no guardaba bien horarios R3 y R4
Se agrego la funcion "/resetDruidaBot" para reiniciar a distancia el dispositivo
Se mejoro sistema anti caida de internet (antes se bugeaba al caerse el internet)
Envia datos a una hoja de calculo de google

*/

#include "esp_system.h"
#include <DHT.h>
#include <Wire.h>
#include <RTClib.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <WiFiUdp.h>
#include <NTPClient.h>
#include <UniversalTelegramBot.h>
#include <EEPROM.h>
#include <Time.h>
#include <HTTPClient.h>

#define sensor1PIN 19
#define sensor2PIN 34
#define RELAY1 18
#define RELAY2 17
#define RELAY3 16
#define RELAY4 15
#define H 1
#define T 2
#define D 3
#define MANUAL 1
#define AUTO 2
#define CONFIG 3
#define STATUS 4

const String botToken = "6920896340:AAEdvJl1v67McffACbdNXLhjMe00f_ji_ag"; //DRUIDA UNO (caba)
//const String botToken = "6867697701:AAHtaJ4YC3dDtk1RuFWD-_f72S5MYvlCV4w"; //DRUIDA DOS (rasta)
//const String botToken = "7273841170:AAHxWF33cIDcIoxgBm3x9tzn9ISJIKoy7X8"; //DRUIDA TRES (matheu)
//const String botToken = "7314697588:AAGJdgljHPSb47EWcfYUR1Rs-7ia0_domok"; //DRUIDA CUATRO (prueba)

const unsigned long BOT_MTBS = 1000;
const int MAX_STRING_LENGTH = 32;
unsigned long bot_lasttime;
const unsigned long wifiCheckInterval = 600000; //WiFi CheckStatus cada 10 minutos
unsigned long previousMillis = 0;

WiFiClientSecure secured_client;
UniversalTelegramBot bot(botToken, secured_client);

DHT dht(sensor1PIN, DHT11);
//DHT dht(sensor1PIN, DHT22);

RTC_DS3231 rtc;

String chat_id = "";
String ssid = "";
String password = "";

//String scriptId = "AKfycbwXhUu15DVEI4b1BDf8Y8Up_qKIXDUvfWgHLKppNL6rUMOnfiQRDfxGXtCt3_n0NXt_Nw"; //Druida UNO (Caba)
String scriptId = "AKfycbwUlj-gk1NNDHwxxebIqH7vS0N8qbu9LZydo4QeyAwmULEQ8JcSGNt8RRxRLdoIRRTA"; //Druida TRES (Matheu)

int conPW = 1;
int reset = 0;

byte modoR1 = 0; 
float minR1 = 0;
float maxR1 = 0;
byte paramR1 = 0;

byte modoR2 = 0;
float minR2 = 0;
float maxR2 = 0;
byte paramR2 = 0;

byte modoR3 = 0;
int timeOnR3 = 0;
int timeOffR3 = 0;
byte diasRiego[7];

byte modoR4 = 0;
int timeOnR4 = 0;
int timeOffR4 = 0;

byte modoMenu = -1;

int R1config = -1;
int R2config = -1;
int R3config = -1;
int R4config = -1;

byte estadoR1 = 0;
byte estadoR2 = 0;
byte estadoR3 = 0;
byte estadoR4 = 0;

float DPV = 0;

int diaNumero;
int diaHoy;
int cantCon = 0;

int horaOnR3, minOnR3, horaOffR3, minOffR3, horaOnR4, minOnR4, horaOffR4, minOffR4;
int horaWifi = 0;

float humidity; // Variables globales
float temperature;

float maxHum = -999;
float minHum = 999;
float maxTemp = -999;
float minTemp = 999;

int lastHourSent = -1;

void setup() {
  Wire.begin();
  Serial.begin(115200);
  EEPROM.begin(512);
  rtc.begin();
  dht.begin();
  
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

    const char *days[] = {"Domingo", "Lunes", "Martes", "Miércoles", "Jueves", "Viernes", "Sábado"};
    int dayNumber = timeinfo.tm_wday;
    diaNumero = dayNumber;

    Serial.print("Dia de hoy: ");
    Serial.println(days[dayNumber]);
  
  } else {
    Serial.println("Menu Serial: ");
    Serial.println("1. Modificar red WiFi");
    Serial.println("2. Modificar Chat ID");
    Serial.println("3. Modificar Script ID (google)");
    Serial.println("4. Enviar data a Google Sheets");
  }
}

void loop() {

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
  } else {
    if (horaWifi != rtc.now().hour()) {
      connectToWiFi(ssid.c_str(), password.c_str());
      horaWifi = rtc.now().hour();
    }
  }


  float temperature = dht.readTemperature();
  float humidity = dht.readHumidity();

  if (temperature > maxTemp){
    maxTemp = temperature;
  }

  if (temperature < minTemp){
    minTemp = temperature;
  }

  if (humidity > maxHum){
    maxHum = humidity;
  }

  if (humidity < minHum){
    minHum = humidity;
  }

int serial = Serial.read();
//ACA SE PONE LA HORA EN EL RTC

  DateTime now = rtc.now();
  int hour = now.hour(); 
  int minute = now.minute();
  int second = now.second();
  int day = now.dayOfTheWeek();
  
   hour -= 3;
  if (hour < 0){ 
    hour = 24 + hour;
  }

  if (WiFi.status() != WL_CONNECTED && horaWifi != hour){
    connectToWiFi(ssid.c_str(), password.c_str());
    horaWifi = hour;
  }
if (rtc.now().minute() == 0 && hour != lastHourSent){
  if (WiFi.status() == WL_CONNECTED) {
    sendDataToGoogleSheets();
    lastHourSent = hour;
  
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

  DPV = calcularDPV(temperature, humidity);
  

  if (serial == '1') {
    Serial.println("Cambiar de red WiFi: ");
    modificarWifi();
    serial = 0;
  }

  if (serial == '2') {
    modificarChatID();
    serial = 0;
  }

  if (serial == '3'){
    modificarScriptId();
    serial = 0;
  }

  

  if (reset == 1) {
    esp_restart();
  }

  //manejarReles();

  //MODO MANUAL R1
  if (modoR1 == MANUAL){ 
    if (estadoR1 == 1){
    digitalWrite(RELAY1, LOW); 
  } else {
    digitalWrite(RELAY1, HIGH);
  }
  }

 //MODO MANUAL R2
  if (modoR2 == MANUAL){ 
    if (estadoR2 == 1){
    digitalWrite(RELAY2, LOW); 
  } else {
    digitalWrite(RELAY2, HIGH);
  }
  }

 //MODO MANUAL R3
  if (modoR3 == MANUAL){ 
    if (estadoR3 == 1){
    digitalWrite(RELAY3, LOW);
  } else {
    digitalWrite(RELAY3, HIGH);
  }
  }

  //MODO MANUAL R4
  if (modoR4 == MANUAL){ 
    if (estadoR4 == 1){
    digitalWrite(RELAY4, LOW); 
  } else {
    digitalWrite(RELAY4, HIGH);
  }
  }
  


  //MODO AUTO R1 (UP) :

  if (modoR1 == AUTO){
    //Serial.print("Rele (UP) Automatico");

     if (paramR1 == H){
       if (humidity < minR1){
        digitalWrite(RELAY1, LOW);
      }  if (humidity > maxR1){
        digitalWrite(RELAY1, HIGH);

      }
    }
     if(paramR1 == T){
       if (temperature < minR1){
        digitalWrite(RELAY1, LOW);

      }  if (temperature > minR1){
        digitalWrite(RELAY1, HIGH);

      }
    }

     if(paramR1 == D){
       if (DPV < minR1){
        digitalWrite(RELAY1, LOW);

         if(DPV > maxR1){
          digitalWrite(RELAY1, HIGH);

        }
      }
    }

   
  }

  //MODO AUTO R2 (DOWN)

if (modoR2 == AUTO){
    //Serial.print("Rele 2 (Down) Automatico");

     if (paramR2 == H){
       if (humidity > maxR2){
        digitalWrite(RELAY2, LOW);

      }  if (humidity < minR2){
        digitalWrite(RELAY2, HIGH);

      }
    }
     if(paramR2 == T){
       if (temperature > maxR2){
        digitalWrite(RELAY2, LOW);

      }  if (temperature < minR2){
        digitalWrite(RELAY2, HIGH);

      }
    }

     if(paramR2 == D){
       if (DPV > maxR2){
        digitalWrite(RELAY2, LOW);
         if(DPV < minR2){
          digitalWrite(RELAY2, HIGH);
        }
      }
    }

   
  }

  

  timeOnR3 = horaOnR3 * 60 + minOnR3;
  timeOffR3 = horaOffR3 * 60 + minOffR3;
  timeOnR4 = horaOnR4 * 60 + minOnR4;
  timeOffR4 = horaOffR4 * 60 + minOffR4;

  // MODO AUTO R3 (Riego)

  // Convierte todo a minutos para facilitar la comparación
  int currentTime = hour * 60 + minute;
  int startR3 = timeOnR3;
  int offR3 = timeOffR3;
  int startR4 = timeOnR4; // *60
  int offR4 = timeOffR4;  // *60


  int c;

  if (modoR3 == AUTO){ 
  for (c = 0; c < 7 ; c++){
   if (diasRiego[c] == 1){
     if (c == day){
  if (currentTime >= startR3 && currentTime < offR3){
    digitalWrite(RELAY3, LOW);
  } else {
    digitalWrite(RELAY3, HIGH);
  }
    }
  }
}
  }

//MODO AUTO R4 (Luz)

if (modoR4 == AUTO){

  if (currentTime >= startR4 && currentTime < offR4){
    digitalWrite(RELAY4, LOW);
  } else {
    digitalWrite(RELAY4, HIGH);
  }
}

  delay(1000);
}

/*void manejarReles() {
  // Modo manual para cada relé
  if (modoR1 == MANUAL) {
    digitalWrite(RELAY1, estadoR1 == 1 ? LOW : HIGH);
  }
  if (modoR2 == MANUAL) {
    digitalWrite(RELAY2, estadoR2 == 1 ? LOW : HIGH);
  }
  if (modoR3 == MANUAL) {
    digitalWrite(RELAY3, estadoR3 == 1 ? LOW : HIGH);
  }
  if (modoR4 == MANUAL) {
    digitalWrite(RELAY4, estadoR4 == 1 ? LOW : HIGH);
  }

  // Modo automático para cada relé
  if (modoR1 == AUTO) {
    manejarAutoR1();
  }
  if (modoR2 == AUTO) {
    manejarAutoR2();
  }
  if (modoR3 == AUTO) {
    manejarAutoR3();
  }
  if (modoR4 == AUTO) {
    manejarAutoR4();
  }
}

void manejarAutoR1() {
  if (paramR1 == H) {
    if (humidity < minR1) digitalWrite(RELAY1, LOW);
    if (humidity > maxR1) digitalWrite(RELAY1, HIGH);
  }
  if (paramR1 == T) {
    if (temperature < minR1) digitalWrite(RELAY1, LOW);
    if (temperature > minR1) digitalWrite(RELAY1, HIGH);
  }
  if (paramR1 == D) {
    if (DPV < minR1) digitalWrite(RELAY1, LOW);
    if (DPV > maxR1) digitalWrite(RELAY1, HIGH);
  }
}

void manejarAutoR2() {
  if (paramR2 == H) {
    if (humidity > maxR2) digitalWrite(RELAY2, LOW);
    if (humidity < minR2) digitalWrite(RELAY2, HIGH);
  }
  if (paramR2 == T) {
    if (temperature > maxR2) digitalWrite(RELAY2, LOW);
    if (temperature < minR2) digitalWrite(RELAY2, HIGH);
  }
  if (paramR2 == D) {
    if (DPV > maxR2) digitalWrite(RELAY2, LOW);
    if (DPV < minR2) digitalWrite(RELAY2, HIGH);
  }
}

void manejarAutoR3() {
  DateTime now = rtc.now();
  int currentTime = now.hour() * 60 + now.minute();
  int startR3 = horaOnR3 * 60 + minOnR3;
  int offR3 = horaOffR3 * 60 + minOffR3;

  if (diasRiego[now.dayOfTheWeek()] == 1) {
    if (currentTime >= startR3 && currentTime < offR3) {
      digitalWrite(RELAY3, LOW);
    } else {
      digitalWrite(RELAY3, HIGH);
    }
  }
}

void manejarAutoR4() {
  DateTime now = rtc.now();
  int currentTime = now.hour() * 60 + now.minute();
  int startR4 = horaOnR4 * 60 + minOnR4;
  int offR4 = horaOffR4 * 60 + minOffR4;

  if (currentTime >= startR4 && currentTime < offR4) {
    digitalWrite(RELAY4, LOW);
  } else {
    digitalWrite(RELAY4, HIGH);
  }
}

*/


float calcularDPV(float temperature, float humidity) {
  const float VPS_values[] = {
      6.57, 7.06, 7.58, 8.13, 8.72, 9.36, 10.02, 10.73, 11.48, 12.28,
      13.12, 14.02, 14.97, 15.98, 17.05, 18.18, 19.37, 20.54, 21.97, 23.38,
      24.86, 26.43, 28.09, 29.83, 31.67, 33.61, 35.65, 37.79, 40.05, 42.42,
      44.92, 47.54, 50.29, 53.18, 56.21, 59.40, 62.73, 66.23, 69.90, 73.74,
      77.76};

  if (temperature < 1 || temperature > 41) {
    Serial.println("Error: Temperatura fuera de rango válido (1-41)");
    bot.sendMessage(chat_id, "Temperatura fuera de rango válido (1-41)", "");

    return 0.0; // Otra opción podría ser devolver un valor de error
  }

  float VPS = VPS_values[static_cast<int>(temperature) - 1];
  float DPV = 100 - humidity;
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
  chat_id = EEPROM.get(174, chat_id);
  //scriptId = EEPROM.get(224, scriptId);
  
  
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
  EEPROM.put(174, chat_id);
  EEPROM.put(224, scriptId);
  EEPROM.commit();
  
  Serial.println("Guardado realizado con exito.");
}



void handleNewMessages(int numNewMessages)
{
  Serial.print("handleNewMessages ");
  Serial.println(numNewMessages);

  for (int i = 0; i < numNewMessages; i++)
  {
    String chat_id = bot.messages[i].chat_id;
    String text = bot.messages[i].text;
    int valor = text.toInt();

    String from_name = bot.messages[i].from_name;
    if (from_name == "")
      from_name = "Guest";


    //MENU PRINCIPAL

    if (text == "/start")
    {
      String welcome = "Bienvenido al Druida Bot " + from_name + ".\n";
      welcome += "Ingrese un comando: \n";
      welcome += "/config \n";
      welcome += "/manual \n";
      welcome += "/auto \n";
      welcome += "/status \n";
      welcome += "/infoconfig \n";
      welcome += "/DiasRiego \n";
      welcome += "/resetDruidaBot \n";
      welcome += "/enviarData";
      bot.sendMessage(chat_id, welcome, "Markdown");
      delay(500);
    }

    //MODO MANUAL


    if (text == "/manual" || modoMenu == MANUAL){
      modoMenu = MANUAL;
    String modoManu = "MODO MANUAL \n";
      modoManu += "/R1on - /R1off \n";
      modoManu += "/R2on - /R2off \n";
      modoManu += "/R3on - /R3off \n";
      modoManu += "/R4on - /R4off \n";
      bot.sendMessage(chat_id, modoManu, "Markdown");
      delay(500);


    
    if (text == "/R1on")
    {
      
      modoR1 = MANUAL;
      estadoR1 = 1;
      
      bot.sendMessage(chat_id, "Rele 1 is ON", "");
      delay(500);
    }

    if (text == "/R1off")
    {
      modoR1 = MANUAL;
      estadoR1 = 0;
      bot.sendMessage(chat_id, "Rele 1 is OFF", "");
      delay(500);
    }

    if (text == "/R2on")
    {
      
      modoR2 = MANUAL;
      estadoR2 = 1;
      bot.sendMessage(chat_id, "Rele 2 is ON", "");
      delay(500);
    }

    if (text == "/R2off")
    {
      modoR2 = MANUAL;
      estadoR2 = 0;
      bot.sendMessage(chat_id, "Rele 2 is OFF", "");
      delay(500);
    }

    if (text == "/R3on")
    {
      
      modoR3 = MANUAL;
      estadoR3 = 1; 
      bot.sendMessage(chat_id, "Rele 3 is ON", "");
      delay(500);
    }

    if (text == "/R3off")
    {
      modoR3 = MANUAL;
      estadoR3 = 0;
      bot.sendMessage(chat_id, "Rele 3 is OFF", "");
      delay(500);
    }

    if (text == "/R4on")
    {
      
      modoR4 = MANUAL;
      estadoR4 = 1;
      bot.sendMessage(chat_id, "Rele 4 is ON", "");
      delay(500);
    }

    if (text == "/R4off")
    {
      modoR4 = MANUAL;
      estadoR4 = 0;
      bot.sendMessage(chat_id, "Rele 4 is OFF", "");
      delay(500);
    }

    }


// MODO AUTOMATICO

if (text == "/auto" || modoMenu == AUTO){
      modoMenu = AUTO;
    String modoManu = "MODO AUTOMATICO: \n";
      modoManu += "/R1auto\n";
      modoManu += "/R2auto\n";
      modoManu += "/R3auto\n";
      modoManu += "/R4auto\n";
      bot.sendMessage(chat_id, modoManu, "Markdown");
      delay(500);

    
    if (text == "/R1auto")
    {
      
      modoR1 = AUTO;
      bot.sendMessage(chat_id, "Rele 1 Automatico", "");
      delay(500);
    }

    if (text == "/R2auto")
    {
      modoR2 = AUTO;
      bot.sendMessage(chat_id, "Rele 2 Automatico", "");
      delay(500);
    }

    if (text == "/R3auto")
    {
      
      modoR3 = AUTO; 
      bot.sendMessage(chat_id, "Rele 3 Automatico", "");
      delay(500);
    }

    if (text == "/R4auto")
    {
      modoR4 = AUTO;
      bot.sendMessage(chat_id, "Rele 4 Automatico", "");
  
      delay(500);
    }
}


//MODO CONFIG

String modoConf = "MODO CONFIG: \n";
      modoConf += "/infoconfig\n";
      modoConf += "/minR1config\n";
      modoConf += "/maxR1config\n";
      modoConf += "/paramR1config\n";
      modoConf += "/minR2config\n";
      modoConf += "/maxR2config\n";
      modoConf += "/paramR2config\n";
      modoConf += "/horaOnR3config\n";
      modoConf += "/minOnR3config\n";
      modoConf += "/horaOffR3config\n";
      modoConf += "/minOffR3config\n";
      modoConf += "/horaOnR4config\n";
      modoConf += "/minOnR4config\n";
      modoConf += "/horaOffR4config\n";
      modoConf += "/minOffR4config\n";


if (text == "/config"){
  bot.sendMessage(chat_id, modoConf, "Markdown");

  modoMenu = CONFIG;
  }

  /// R1

 if (text == "/minR1config"){
  modoR1 = CONFIG;
  modoMenu = CONFIG;
  R1config = 1;
  bot.sendMessage(chat_id, "Ingrese valor Min R1: ");

  }
if (R1config == 1){
  minR1 = text.toFloat(); //Ingresa valor por mensaje de telegram

  if (minR1 > 0 && minR1 < 100){ 
  Serial.print("Valor min R1: ");
  Serial.println(minR1);
  bot.sendMessage(chat_id, "Valor min R1 guardado");
  bot.sendMessage(chat_id, modoConf, "Markdown");
  R1config = 0;
  }

}

///

if (text == "/maxR1config"){
  modoR1 = CONFIG;
  modoMenu = CONFIG;
  R1config = 2;
  bot.sendMessage(chat_id, "Ingrese valor Max R1: ");

  }
if (R1config == 2){
  maxR1 = text.toFloat();

  if (maxR1 > 0){ 
  Serial.print("Valor max R1: ");
  Serial.println(maxR1);
  bot.sendMessage(chat_id, "Valor max R1 guardado");
  bot.sendMessage(chat_id, modoConf, "Markdown");
  R1config = 0;
  }

}

///

if (text == "/paramR1config"){
  modoR1 = CONFIG;
  modoMenu = CONFIG;
  R1config = 3;
  bot.sendMessage(chat_id, "Ingrese parametro R1: \n1- Humedad.\n2- Temperatura.\n3- DPV.");

  }
if (R1config == 3){
  paramR1 = text.toInt();

  if (paramR1 > 0){ 
  Serial.print("Param R1: ");
  Serial.println(paramR1);
  bot.sendMessage(chat_id, "Valor param R1 guardado");
  bot.sendMessage(chat_id, modoConf, "Markdown");
  R1config = 0;
  }
}

/// R2

if (text == "/minR2config"){
  modoR2 = CONFIG;
  modoMenu = CONFIG;
  R2config = 1;
  bot.sendMessage(chat_id, "Ingrese valor Min R2: ");

  }
if (R2config == 1){
  minR2 = text.toFloat();

  if (minR2 > 0){ 
  Serial.print("Valor min R2: ");
  Serial.println(minR2);
  bot.sendMessage(chat_id, "Valor min R2 guardado");
  bot.sendMessage(chat_id, modoConf, "Markdown");

  R2config = 0;
  }

}


if (text == "/maxR2config"){
  modoR2 = CONFIG;
  modoMenu = CONFIG;
  R2config = 2;
  bot.sendMessage(chat_id, "Ingrese valor Max R2: ");

  }
if (R2config == 2){
  maxR2 = text.toFloat();

  if (maxR2 > 0){ 
  Serial.print("Valor max R2: ");
  Serial.println(maxR2);
  bot.sendMessage(chat_id, "Valor max R2 guardado");
  bot.sendMessage(chat_id, modoConf, "Markdown");
  
  R2config = 0;
  }

}

///

if (text == "/paramR2config"){
  modoR2 = CONFIG;
  modoMenu = CONFIG;
  R2config = 3;
  bot.sendMessage(chat_id, "Ingrese parametro R2: ");

  }
if (R2config == 3){
  paramR2 = text.toInt();

  if (paramR2 > 0){ 
  Serial.print("Param R2: ");
  Serial.println(paramR2);
  bot.sendMessage(chat_id, "Valor param R2 guardado");
  bot.sendMessage(chat_id, modoConf, "Markdown");
  
  R2config = 0;
  }
}

  /// RELE 3 CONFIG

if (text == "/horaOnR3config"){
  modoR3 = CONFIG;
  modoMenu = CONFIG;
  R3config = 1;
  bot.sendMessage(chat_id, "Ingrese hora de Encendido R3: ");

  }
if (R3config == 1){
  horaOnR3 = text.toInt();
if (horaOnR3 > 0){ 
  Serial.print("Hora encendido R3: ");
  Serial.println(horaOnR3);
  bot.sendMessage(chat_id, "Valor hora Encendido Rele 3 guardado");
  bot.sendMessage(chat_id, modoConf, "Markdown");
  R3config = 0;
}
}

if (text == "/minOnR3config"){
  modoR3 = CONFIG;
  modoMenu = CONFIG;
  R3config = 2;
  bot.sendMessage(chat_id, "Ingrese minuto de encendido R3: ");

  }
if (R3config == 2){
  minOnR3 = text.toInt();
if (minOnR3 > 0){ 
  Serial.print("Minuto de encendido: ");
  Serial.println(minOnR3);
  bot.sendMessage(chat_id, "Valor minuto de encendido R3 guardado");
  bot.sendMessage(chat_id, modoConf, "Markdown");
  R3config = 0;
}
}

if (text == "/horaOffR3config"){
  modoR3 = CONFIG;
  modoMenu = CONFIG;
  R3config = 3;
  bot.sendMessage(chat_id, "Ingrese hora de apagado R3: ");

  }
if (R3config == 3){
  horaOffR3 = text.toInt();
if (horaOffR3 > 0){ 
  Serial.print("Hora de apagado: ");
  Serial.println(horaOffR3);
  bot.sendMessage(chat_id, "Hora de apagado R3 modificado");
  bot.sendMessage(chat_id, modoConf, "Markdown");

  R3config = 0;
}
}

if (text == "/minOffR3config"){
  modoR3 = CONFIG;
  modoMenu = CONFIG;
  R3config = 4;
  bot.sendMessage(chat_id, "Ingrese minuto de apagado R3: ");

  }
if (R3config == 4){
  minOffR3 = text.toInt();
if (minOffR3 > 0){ 
  Serial.print("Minuto de apagado: ");
  Serial.println(minOffR3);
  bot.sendMessage(chat_id, "Valor minuto de apagado R3 guardado");
  bot.sendMessage(chat_id, modoConf, "Markdown");

  R3config = 0;
}
}



String modoRieg = "MODO RIEGO: \n";
      modoRieg += "/DiasRiegoInfo\n";
      modoRieg += "/LunesRiego\n";
      modoRieg += "/LunesNoRiego\n";
      modoRieg += "/MartesRiego\n";
      modoRieg += "/MartesNoRiego\n";
      modoRieg += "/MiercolesRiego\n";
      modoRieg += "/MiercolesNoRiego\n";
      modoRieg += "/JuevesRiego\n";
      modoRieg += "/JuevesNoRiego\n";
      modoRieg += "/ViernesRiego\n";
      modoRieg += "/ViernesNoRiego\n";
      modoRieg += "/SabadoRiego\n";
      modoRieg += "/SabadoNoRiego\n";
      modoRieg += "/DomingoRiego\n";
      modoRieg += "/DomingoNoRiego\n";

      //bot.sendMessage(chat_id, modoConf, "Markdown");

if (text == "/DiasRiego"){
   
    bot.sendMessage(chat_id, modoRieg, "Markdown");
}
int d;
if (text == "/DiasRiegoInfo"){
  for (d = 0; d < 7; d++){
    if (diasRiego[d] == 1){
    bot.sendMessage(chat_id, "Riego dia:  " + String(d) + "." );
    }
  }
}

if (text == "/LunesRiego"){
  diasRiego[1] = 1;
  bot.sendMessage(chat_id, "Lunes configurado: Riego");
  bot.sendMessage(chat_id, modoRieg, "Markdown");
  

}

if (text == "/LunesNoRiego"){
  diasRiego[1] = 0;
  bot.sendMessage(chat_id, "Lunes configurado: No Riego");
  bot.sendMessage(chat_id, modoRieg, "Markdown");
  

}

if (text == "/MartesRiego"){
  diasRiego[2] = 1;
  bot.sendMessage(chat_id, "Martes configurado: Riego");
  bot.sendMessage(chat_id, modoRieg, "Markdown");
  

}

if (text == "/MartesNoRiego"){
  diasRiego[2] = 0;
  bot.sendMessage(chat_id, "Martes configurado: No Riego");
  bot.sendMessage(chat_id, modoRieg, "Markdown");
  

}

if (text == "/MiercolesRiego"){
  diasRiego[3] = 1;
  bot.sendMessage(chat_id, "Miercoles configurado: Riego");
  bot.sendMessage(chat_id, modoRieg, "Markdown");
  

}

if (text == "/MiercolesNoRiego"){
  diasRiego[3] = 0;
  bot.sendMessage(chat_id, "Miercoles configurado: No Riego");
  bot.sendMessage(chat_id, modoRieg, "Markdown");
  

}

if (text == "/JuevesRiego"){
  diasRiego[4] = 1;
  bot.sendMessage(chat_id, "Jueves configurado: Riego");
  bot.sendMessage(chat_id, modoRieg, "Markdown");

}

if (text == "/JuevesNoRiego"){
  diasRiego[4] = 0;
  bot.sendMessage(chat_id, "Jueves configurado: No Riego");
  bot.sendMessage(chat_id, modoRieg, "Markdown");

}

if (text == "/ViernesRiego"){
  diasRiego[5] = 1;
  bot.sendMessage(chat_id, "Viernes configurado: Riego");
  bot.sendMessage(chat_id, modoRieg, "Markdown");

}

if (text == "/ViernesNoRiego"){
  diasRiego[5] = 0;
  bot.sendMessage(chat_id, "Viernes configurado: No Riego");
  bot.sendMessage(chat_id, modoRieg, "Markdown");

}

if (text == "/SabadoRiego"){
  diasRiego[6] = 1;
  bot.sendMessage(chat_id, "Sabado configurado: Riego");
  bot.sendMessage(chat_id, modoRieg, "Markdown");

}

if (text == "/SabadoNoRiego"){
  diasRiego[6] = 0;
  bot.sendMessage(chat_id, "Sabado configurado: No Riego");
  bot.sendMessage(chat_id, modoRieg, "Markdown");

}

if (text == "/DomingoRiego"){
  diasRiego[0] = 1;
  bot.sendMessage(chat_id, "Domingo configurado: Riego");
  bot.sendMessage(chat_id, modoRieg, "Markdown");

}

if (text == "/DomingoNoRiego"){
  diasRiego[0] = 0;
  bot.sendMessage(chat_id, "Domingo configurado: No Riego");
  bot.sendMessage(chat_id, modoRieg, "Markdown");
}


  // RELE 4 CONFIG

  if (text == "/horaOnR4config"){
  modoR4 = CONFIG;
  modoMenu = CONFIG;
  R4config = 1;
  bot.sendMessage(chat_id, "Ingrese hora de Encendido R4: ");

  }
if (R4config == 1){
  horaOnR4 = text.toInt();

if (horaOnR4 > 0){ 
  Serial.print("Hora encendido R4: ");
  Serial.println(horaOnR4);
  bot.sendMessage(chat_id, "Valor hora Encendido Rele 4 guardado");
  bot.sendMessage(chat_id, modoConf, "Markdown");

  R4config = 0;
}
  
}


  if (text == "/minOnR4config"){
  modoR4 = CONFIG;
  modoMenu = CONFIG;
  R4config = 2;
  bot.sendMessage(chat_id, "Ingrese minuto de Encendido R4: ");

  }
if (R4config == 2){
  minOnR4 = text.toInt();
if (minOnR4 > 0){ 
  Serial.print("Tiempo Minuto de encendido de Rele 4: ");
  Serial.println(minOnR4);
  bot.sendMessage(chat_id, "Valor minuto de encendido de R4 guardado");
  bot.sendMessage(chat_id, modoConf, "Markdown");

  R4config = 0;
}

}

if (text == "/horaOffR4config"){
  modoR4 = CONFIG;
  modoMenu = CONFIG;
  R4config = 3;
  bot.sendMessage(chat_id, "Ingrese hora de Apagado R4: ");

  }
if (R4config == 3){
  horaOffR4 = text.toInt();
if (horaOffR4 > 0){ 
  Serial.print("Tiempo hora de apagado Rele 4: ");
  Serial.println(horaOffR4);
  bot.sendMessage(chat_id, "Valor hora de Apagado R4 guardado");
  bot.sendMessage(chat_id, modoConf, "Markdown");

  R4config = 0;
}

}

if (text == "/minOffR4config"){
  modoR4 = CONFIG;
  modoMenu = CONFIG;
  R4config = 4;
  bot.sendMessage(chat_id, "Ingrese minuto de Apagado R4 (en minutos): ");

  }
if (R4config == 4){
  minOffR4 = text.toInt();
if (minOffR4 > 0){ 
  Serial.print("Tiempo minuto de apagado Rele 4: ");
  Serial.println(minOffR4);
  bot.sendMessage(chat_id, "Valor minuto de Apagado R4 guardado");
  bot.sendMessage(chat_id, modoConf, "Markdown");

  R4config = 0;
}

}



//  MOSTRAR PARAMETROS


if (text == "/infoconfig"){



  String infoConfig = "INFO CONFIG: \n";
      infoConfig += "Rele 1: \n";
      infoConfig += "minR1: " + String(minR1) + ".\n";
      infoConfig += "maxR1: " + String(maxR1) + ".\n";
      infoConfig += "paramR1: " + String(paramR1) + ".\n";
      infoConfig += "modoR1: " + String(modoR1) + ".\n";
      infoConfig += "Rele 2: \n";
      infoConfig += "minR2: " + String(minR2) + ".\n";
      infoConfig += "maxR2: " + String(maxR2) + ".\n";
      infoConfig += "modoR2: " + String(modoR2) + ".\n";
      infoConfig += "modoR3: " + String(modoR3) + ".\n";
      infoConfig += "Rele 3: \n";
      infoConfig += "Hora de encendido: " + String(horaOnR3) + ":" + String(minOnR3) + "\n";
      infoConfig += "Hora de apagado: " + String(horaOffR3) + ":" + String(minOffR3) + "\n";
      infoConfig += "Rele 4: \n";
      infoConfig += "Hora de encendido: " + String(horaOnR4) + ":" + String(minOnR4) + "\n";
      infoConfig += "Hora de apagado: " + String(horaOffR4) + ":" + String(minOffR4) + "\n";

      bot.sendMessage(chat_id, infoConfig, "Markdown");
  
}

if (text == "/status" || modoMenu == STATUS)
{
    modoMenu = STATUS;

    // Leer datos del sensor DHT
    float temperature = dht.readTemperature();
    float humidity = dht.readHumidity();
    DateTime now = rtc.now();
    int horaBot = now.hour();

       horaBot -= 3;
  if (horaBot < 0)
    horaBot = 24 + horaBot;

    int currentTimeBot = horaBot * 60 + now.minute();

    // Leer fecha y hora del RTC
    
    String dateTime = "Fecha y Hora: " + String(now.day()) + "/" + String(now.month()) + "/" + String(now.year()) +
                      " " + horaBot + ":" + String(now.minute()) + ":" + String(now.second()) + "\n";

    String statusMessage = "Temperatura: " + String(temperature, 1) + " °C\n";
    statusMessage += "Humedad: " + String(humidity, 1) + " %\n";
    statusMessage += "DPV: " + String(DPV, 1) + " kPa\n";
    statusMessage += dateTime; // Agrega la fecha y hora al mensaje
    //statusMessage += "Rele 1: " + String(estadoR1) + "\n";
    //statusMessage += "Rele 2: " + String(estadoR2) + "\n";
    //statusMessage += "Rele 3: " + String(estadoR3) + "\n";
    //statusMessage += "Rele 4: " + String(estadoR4) + "\n";


    bot.sendMessage(chat_id, statusMessage, "");
}

if (text == "/resetDruidaBot") {
      String resetMsg = "Reiniciando druida..\n";
      bot.sendMessage(chat_id, resetMsg, "Markdown");
      delay(2000);
      reset = 1;


      
    }

    if (text == "/enviarData") {
      String dataMsg = "Enviando Data\n";
      bot.sendMessage(chat_id, dataMsg, "Markdown");
      delay(2000);
      sendDataToGoogleSheets();


      
    }


  
  delay(500);

}
  Guardado_General();
}




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
  while (!Serial.available()){}
  if (Serial.available() > 0) {
    serial = Serial.read();
    Serial.print("conPW: ");
    Serial.println(serial);
  }

  Serial.println("Por favor, introduce el SSID:");
  while (!Serial.available()){}
  if (Serial.available() > 0) {
    ssid = readSerialInput();
    if (serial == '1'){ 
    Serial.println("Por favor, introduce la contraseña:");
    while (Serial.available() == 0) {
      delay(100);
    }
    }
    if (serial == '1'){ 
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
          break; // Romper el bucle si ya se ha leído algo
        }
        continue; // Ignorar '\n' y '\r' si aún no se ha leído nada
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
  if (conPW == 1){
  WiFi.begin(ssid, password); 
  Serial.print("Red WiFi: ");
  Serial.println(ssid);
  Serial.print("Password: ");
  Serial.println(password);
  Serial.print("Chat ID: ");
  Serial.println(chat_id);
  } 
  if (conPW == 0){
  WiFi.begin(ssid); 
  Serial.print("Red WiFi: ");
  Serial.println(ssid);
  }

  unsigned long startAttemptTime = millis();

  while (cantCon < 6){// Esperar hasta que la conexión se establezca (10 segundos máximo)
  while (millis() - startAttemptTime < 10000) {
    delay(500);
    Serial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("");
    Serial.println("WiFi conectado.");
    Serial.println("Dirección IP: ");
    Serial.println(WiFi.localIP());
    cantCon = 6;
  } else {
    Serial.println("");
    Serial.println("No se pudo conectar al WiFi.");
    cantCon++;
    Serial.print("Intentos: (");
    Serial.print(cantCon);
    Serial.println("/5)");
    while (millis() - startAttemptTime < 10000){
      delay(500);
      Serial.print(".");
    }
      connectToWiFi(ssid, password);

  }

  } 
  
  
}

void writeStringToEEPROM(int addrOffset, const String &strToWrite) {
  byte len = strToWrite.length();
  if (len > 32) len = 32;  // Limitar longitud a 32 caracteres
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
  data[newStrLen] = '\0'; // Asegurar terminación nula
  return String(data);
}

void modificarChatID(){
  while (Serial.available() > 0) {
  Serial.read();
  }
  Serial.println("Porfavor ingrese el nuevo Chat ID:");
  while (!Serial.available()){}
  if (Serial.available() > 0){ 
  chat_id = Serial.readStringUntil('\n');
  Serial.println("Chat ID Modificado: ");
  Serial.print("Nuevo Chat ID: ");
  Serial.println(chat_id);
  Guardado_General();
  }

}


void modificarScriptId(){
  while (Serial.available() > 0) {
  Serial.read();
  }
  Serial.println("Porfavor ingrese el nuevo ID Google:");
  while (!Serial.available()){}
  if (Serial.available() > 0){ 
  scriptId = Serial.readStringUntil('\n');
  Serial.println("Script ID Modificado: ");
  Serial.print("Nuevo Script ID: ");
  Serial.println(scriptId);
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

  Serial.print("Enviando datos a Google Sheets: ");
  Serial.println(url);

  // Realizar la solicitud HTTP GET
  http.begin(url);
  int httpResponseCode = http.GET();

  if (httpResponseCode > 0) {
    String payload = http.getString();
    Serial.print("Payload recibido: ");
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

