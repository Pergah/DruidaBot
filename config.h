
/* Druida BOT 1.619

El programa es capaz de controlar 4 reles independientes. 
Pueden funcionar en modo Manual, o Automatico.
Rele 1: Sube parametro (Hum, Temp, DPV, TempA)
Rele 2: Baja parametro (Hum, Temp, DPV, TempA) 
Rele 3: Timer diario (Ej: Luz) (Hora encendido / Hora Apagado)
Rele 4: Timer diario + semanal (Ej: Riego) (Hora Enc / Hora Apag) + (DiaRiego / DiaNoRiego)

Conexion de sensores: 

Sensor Emisor IR -> 4
Temperatura de Agua -> 5
Sensor IR Receptor -> 14
Temperatura y Humedad ambiente -> 19
Sensor PH -> 34

Chenge Log:

* La red WiFi se puede cambiar desde el serial
* El chat ID se puede cambiar desde el serial
* Solucionado, no guardaba bien horarios R3 y R4
* Se agrego la funcion "/resetDruidaBot" para reiniciar a distancia el dispositivo
* Se mejoro sistema anti caida de internet (antes se bugeaba al caerse el internet)
* Envia datos a una hoja de calculo de google
* Se agrego la funcion de medir PH
* Se agrego funcion de Medir temperatura de Agua
* Se agrego funcion para enviar señal IR en el R2, se cargan los valores manualmente.
* Se optimizo la parte del codigo donde se encienden y apagan los Rele.
* Se agrego la funcion de "clonar" la señal IR con un sensor Receptor. (carga automatica)
* Se arreglo la logica de R3 y R4, fallaba cuando HoraEnc > HoraApag
* Se agrego la funcion de encender los reles por X segundos en modo Manual (/R1onTime)
* Se agrego un nuevo Rele, llamado "Rele 2 IR", en vez de encender un Rele, manda una señal por infrarojo. (Especial, aire acondicionado).
* Falta modificar logica del R3 para que pueda accionar o no, en funcion del sensor de HumedadSuelo
*/

#include "esp_system.h"
//#include <DHT.h>
#include <Wire.h>
#include <RTClib.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <WiFiUdp.h>
#include <NTPClient.h>
#include <UniversalTelegramBot.h>
#include <EEPROM.h>
#include <Time.h>
//#include <TimeAlarms.h>
#include <HTTPClient.h>
//#include <OneWire.h>
//#include <DallasTemperature.h>
#include <Arduino.h>
#include <IRrecv.h>
#include <IRremoteESP8266.h>
#include <IRutils.h>
#include <IRsend.h>
#include <Adafruit_AHTX0.h>
#include <WebServer.h>
//#include <HardwareSerial.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>


#define H 1
#define T 2
#define D 3
#define TA 4
#define HS 5
#define MANUAL 1
#define AUTO 2
#define CONFIG 3
#define STATUS 4
#define AUTOINT 5
#define TIMER 6

// Aca se muestra como van conectados los componentes

//#define sensorTempAgua 5  //Sensor DS
#define sensorIRpin 32    //Sensor Emisor IR
//#define sensorHS 2 //humedad Suelo
#define sensorIRreceptor 33
#define RELAY4 19
#define RELAY3 5
#define RELAY2 17
#define RELAY1 16

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);


const uint16_t kRecvPin = sensorIRreceptor;
const uint16_t kCaptureBufferSize = 1024;
IRrecv irrecv(kRecvPin, kCaptureBufferSize);
decode_results results;

uint16_t IRsignal[150] = { 0 };
uint16_t IRsignalLength = 0;
IRsend irsend(sensorIRpin);

//const String botToken = "6920896340:AAEdvJl1v67McffACbdNXLhjMe00f_ji_ag"; //DRUIDA UNO (caba y roge)
//const String botToken = "6867697701:AAHtaJ4YC3dDtk1RuFWD-_f72S5MYvlCV4w"; //DRUIDA DOS (matheu 2)
//const String botToken = "7273841170:AAHxWF33cIDcIoxgBm3x9tzn9ISJIKoy7X8"; //DRUIDA TRES (brai e ivana)
const String botToken = "7357647743:AAFPD1Tc099-2o-E2-Ph7SZluzwHubrl700";  //DRUIDA CINCO
//const String botToken = "7314697588:AAGJdgljHPSb47EWcfYUR1Rs-7ia0_domok"; //DRUIDA CUATRO (matheu)

//const char* ssid_AP = "Druida Uno";
//const char* ssid_AP = "Druida Dos"; 
//const char* ssid_AP = "Druida Cuatro";  
const char* ssid_AP = "Druida Cinco";           // Nombre de la red AP creada por el ESP32
const char* password_AP = "12345678";          // Contraseña de la red AP

//String scriptId = "AKfycbwXhUu15DVEI4b1BDf8Y8Up_qKIXDUvfWgHLKppNL6rUMOnfiQRDfxGXtCt3_n0NXt_Nw"; //Druida UNO (Caba)
String scriptId = "AKfycbwUlj-gk1NNDHwxxebIqH7vS0N8qbu9LZydo4QeyAwmULEQ8JcSGNt8RRxRLdoIRRTA";  //Druida TRES (Matheu)

const unsigned long BOT_MTBS = 1000;
const int MAX_STRING_LENGTH = 32;
unsigned long bot_lasttime;
const unsigned long wifiCheckInterval = 600000;  //WiFi CheckStatus cada 10 minutos
unsigned long previousMillis = 0;


WiFiClientSecure secured_client;
UniversalTelegramBot bot(botToken, secured_client);


Adafruit_AHTX0 aht;


RTC_DS3231 rtc;

String chat_id = "";
String ssid = "";
String password = "";


int conPW = 1;
int reset = 0;

byte modoR1 = 0;
float minR1 = 0;
float maxR1 = 0;
byte paramR1 = 0;
int timeOnR1 = 0;
int timeOffR1 = 0;
int horaOnR1 = 0;
int minOnR1 = 0;
int horaOffR1 = 0;
int minOffR1 = 0;

byte modoR2 = 0;
float minR2 = 0;
float maxR2 = 0;
byte paramR2 = 0;

byte modoR2ir = 0;
float minR2ir = 0;
float maxR2ir = 0;
byte paramR2ir = 0;

byte modoR3 = 0;
int minR3 = 0;
int maxR3 = 0;
byte paramR3 = HS;
byte diasRiego[7];
int timeOnR3 = 0;
int timeOffR3 = 0;

byte modoR4 = 0;
int timeOnR4 = 0;
int timeOffR4 = 0;

byte modoMenu = -1;

int R1config = -1;
int R2config = -1;
int R2irconfig = -1;
int R3config = -1;
int R4config = -1;

byte estadoR1 = 0;
byte estadoR2 = 0;
byte estadoR2ir = 0;
byte estadoR3 = 0;
byte estadoR4 = 0;

bool R1estado = HIGH;
bool R2estado = HIGH;
bool R2irestado = HIGH;
bool R3estado = HIGH;
bool R4estado = HIGH;

float DPV = 0;

int diaNumero;
int diaHoy;
int cantCon = 0;

int horaOnR3, minOnR3, horaOffR3, minOffR3, horaOnR4, minOnR4, horaOffR4, minOffR4;
int horaWifi = 0;

float humedad;  // Variables globales
float temperature;

float maxHum = -999;
float minHum = 999;
float maxTemp = -999;
float minTemp = 999;

int lastHourSent = -1;

byte estadoRTC = 0;

int tiempoR1 = 0;
int tiempoR2 = 0;
int tiempoR2ir = 0;
int tiempoR3 = 0;
int tiempoR4 = 0; 
bool esperandoTiempoR1 = false;
bool esperandoTiempoR2 = false;
bool esperandoTiempoR2ir = false;
bool esperandoTiempoR3 = false;
bool esperandoTiempoR4 = false;


WebServer server(80);

//Humedad Suelo
int sensor1Value; 
int sensor2Value;
int sensor3Value;

int parametroActual = 0;  // Variable global para controlar qué parámetro mostrar
unsigned long lastUpdate = 0;  // Para manejar el tiempo entre actualizaciones
const unsigned long displayInterval = 2000;  // Intervalo de cambio (2 segundos)