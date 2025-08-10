
// Proyecto: Druida BOT de DataDruida
// Autor: Bryan Murphy
// Año: 2025
// Licencia: MIT

#include "esp_system.h"
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
//#include <OneWire.h>
//#include <DallasTemperature.h>
#include <Arduino.h>
//#include <IRrecv.h>
//#include <IRremoteESP8266.h>
//#include <IRutils.h>
//#include <IRsend.h>
#include <Adafruit_AHTX0.h>
#include <WebServer.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
//#include <Adafruit_SH110X.h>
#include <ESP32Servo.h>
#include "esp_task_wdt.h"



#define H 1
#define T 2
#define D 3
#define HT 4
#define HS 5
#define MANUAL 1
#define AUTO 2
#define CONFIG 3
#define SUPERCICLO 4
#define STATUS 5
#define TIMER 6
#define RIEGO 7
#define AUTORIEGO 8
#define AUTOINT 9


// Aca se muestra como van conectados los componentes

//#define sensorTempAgua 5  //Sensor DS
//#define sensorIRpin 13    //Sensor Emisor IR
//#define sensorHS 2 //humedad Suelo
//#define sensorIRreceptor 12
#define SERVO 23
#define RELAY4 19
#define RELAY3 5
#define RELAY5 18
#define RELAY2 17
#define RELAY1 16

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define WDT_TIMEOUT 300 //(Diez minutos)

#define SDA_NANO 33
#define SCL_NANO 27

TwoWire I2CNano = TwoWire(1);  // Usamos el bus I2C número 1

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
//Adafruit_SH1106G display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1); // -1 = sin pin RESET
Servo dimmerServo; // Objeto del servomotor


//const uint16_t kRecvPin = sensorIRreceptor;
//const uint16_t kCaptureBufferSize = 1024;
//IRrecv irrecv(kRecvPin, kCaptureBufferSize);
//decode_results results;

//uint16_t rawData[150] = { 0 };
//uint16_t rawDataLen = 0;
//IRsend irsend(sensorIRpin);


//ACA VAN LOS TOKENS


//const String botToken = "6920896340:AAEdvJl1v67McffACbdNXLhjMe00f_ji_ag"; //DRUIDA UNO (-)
//const String botToken = "6867697701:AAHtaJ4YC3dDtk1RuFWD-_f72S5MYvlCV4w"; //DRUIDA DOS (60x60)
//const String botToken = "7273841170:AAHxWF33cIDcIoxgBm3x9tzn9ISJIKoy7X8"; //DRUIDA TRES (80x80)
//const String botToken = "7314697588:AAGJdgljHPSb47EWcfYUR1Rs-7ia0_domok"; //DRUIDA CUATRO (100x100)
//const String botToken = "7357647743:AAFPD1Tc099-2o-E2-Ph7SZluzwHubrl700";  //DRUIDA CINCO (matheu)
//const String botToken = "7611244980:AAEQUDIUZwR4nZBsjEEHPEieyc3k90PxVxI"; //DRUIDA SEIS (nuevop)
//const String botToken = "7883822127:AAHkWG4Gw2VfQDKk4UbTw8THZg2oxng3fM0"; //DRUIDA SIETE
//const String botToken = "7876802825:AAEVAKdmkVRLLaCJu4nJz4230q9Wjsj1UQw"; //DRUIDA OCHO 
//const String botToken = "7728657762:AAHfdY7BAYy4DZv5OY8_EH6962uXr6MnQRc"; //DRUIDA NUEVE(IVANA)
const String botToken = "7775586310:AAFDG0S8gI3V_130QM5059ktEwtCw-UXQ1U"; //DRUIDA DIEZ 
//const String botToken = "7645489798:AAEwwyvYupDuc75KQDf6zlat35fqb1pYKoU"; //DRUIDA ONCE 
//const String botToken = "7746698472:AAG4o49c73Q7H-o1moMMmFJrcvNbBZ5Mk0Y"; //DRUIDA DOCE
//const String botToken = "7622091905:AAGq10bmEioS73QYo7k3b_1mtMSvkHMj6aM"; //DRUIDA TRECE //(MATI PERGA)
//const String botToken = "7715561134:AAFAKOD8MWk3Z5LsBrlnzwCczFqEr5fmvCU"; //DRUIDA CATORCE 





//ACA VAN LOS TOKENS



const char* password_AP = "12345678";          // Contraseña de la red AP

// ID: 1308350088 

String scriptId = "AKfycbwUlj-gk1NNDHwxxebIqH7vS0N8qbu9LZydo4QeyAwmULEQ8JcSGNt8RRxRLdoIRRTA";  //Druida DOS (Matheu)
//String scriptId = "AKfycbwUlj-gk1NNDHwxxebIqH7vS0N8qbu9LZydo4QeyAwmULEQ8JcSGNt8RRxRLdoIRRTA"; //Druida CUATRO (Caba)


const unsigned long BOT_MTBS = 1000;
const int MAX_STRING_LENGTH = 32;
unsigned long bot_lasttime;
const unsigned long wifiCheckInterval = 120000;  //WiFi CheckStatus cada 2 minutos
unsigned long previousMillis = 0;

//bool irCaptureDone = false;


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
float minTR2 = 0;
float maxTR2 = 0;
byte paramR2 = 0;

//byte modoR2ir = 0;
//float minR2ir = 0;
//float maxR2ir = 0;
//byte paramR2ir = 0;

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
//int R2irconfig = -1;
int R3config = -1;
int R4config = -1;

byte estadoR1 = 0;
byte estadoR2 = 0;
//byte estadoR2ir = 0;
byte estadoR3 = 0;
byte estadoR4 = 0;

bool R1estado = HIGH;
bool R2estado = HIGH;
//bool R2irestado = HIGH;
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
//int tiempoR2ir = 0;
int tiempoR3 = 0;
int tiempoR4 = 0; 
bool esperandoTiempoR1 = false;
bool esperandoTiempoR2 = false;
bool esperandoTiempoR2ir = false;
bool esperandoTiempoR3 = false;
bool esperandoTiempoR4 = false;


WebServer server(80);


int parametroActual = 0;  // Variable global para controlar qué parámetro mostrar
unsigned long lastUpdate = 0;  // Para manejar el tiempo entre actualizaciones
const unsigned long displayInterval = 2000;  // Intervalo de cambio (2 segundos)

int tiempoRiego = -1;       // Tiempo de riego en segundos
int tiempoNoRiego = -1;     // Tiempo de pausa entre riegos en segundos
int cantidadRiegos = 1;     // Número de ciclos de riego
// Variables globales para el modo RIEGO
unsigned long previousMillisRiego = 0;  // Variable para manejar el tiempo
int cicloRiegoActual = 0;               // Contador de ciclos de riego
bool enRiego = false;   
bool riegoActivo = false;

int horaAmanecer = -1; // Hora de amanecer en minutos (04:00 -> 240 minutos)
int horaAtardecer = -1;
int currentPosition = 0; // Posición inicial del servo

int previousSecondRiego = 0; // Inicialización con 0
int previousSeconds = 0; 

String relayNames[] = {"Humidificacion", "Extraccion", "Irrigacion", "Iluminacion", "Aire Acondicionado", "Calefaccion"};
String relayAssignedNames[5] = {"R1", "R2", "R3", "R4", "R2ir"}; // Nombres actuales para cada relé

int R1name = 0;   // (Humidificacion)
int R2name = 1;   // (Extraccion)
int R3name = 2;   // (Irrigacion)
int R4name = 3;   // (Iluminacion)
//int R2irname = 4; // (Aire acondicionado)


int modoWiFi = 0;

//SUPERCICLO

unsigned int proximoCambioR4 = 60; // Hora del primer cambio, en minutos (ej. 01:00)
bool luzEncendida = false;

unsigned long previousMillisWD = 0;
const unsigned long interval = 20000; // 20 segundos

// Nuevas variables a añadir SUPERCICLO
int horasLuz = -1;             // Ahora en minutos
int horasOscuridad = -1;       // Ahora en minutos
unsigned long proximoEncendidoR4; // Próxima hora de encendido (minutos desde medianoche)
unsigned long proximoApagadoR4;   // Próxima hora de apagado (minutos desde medianoche)

//int intervaloDatos = 60;  // Intervalo en minutos (por defecto 1 hora)
unsigned long previousMillisTelegram = 0;
unsigned long previousMillisGoogle = 0;

// Variables para el modo AUTORIEGO del relay 1
int tiempoEncendidoR1 = 5; // en minutos
int tiempoApagadoR1 = 10;  // en minutos
unsigned long previousMillisR1 = 0;
bool enHumidificacion = false;

int tiempoGoogle = -1;

int tiempoTelegram = -1; // En minutos, configurable desde la web

int unidadRiego = 60;     // 1 = seg, 60 = min, 3600 = h
int unidadNoRiego = 3600; // valores cargados desde EEPROM o lo que uses

byte direccionR1 = 0;

unsigned long tiempoInicioR2 = 0;
unsigned long tiempoEsperaR2 = 0;
bool enEsperaR2 = false;
float humedadReferenciaR2 = 0;
float temperaturaReferenciaR2 = 0;
float dpvReferenciaR2 = 0;

/***** Ajustes generales para el modo AUTO de R2 *****/
const unsigned long R2_WAIT_MS = 10UL * 60UL * 1000UL;   // 10 min
const float HUM_MIN_VALID      =   0.0;
const float HUM_MAX_VALID      =  99.9;
const float TMP_MIN_VALID      = -10.0;
const float TMP_MAX_VALID      =  50.0;

// ---------- Tiempos absolutos ----------
unsigned long tiempoProxEncendido = 0;  // En minutos desde epoch Unix
unsigned long tiempoProxApagado = 0;


static bool apMode = false;
static unsigned long lastRetryTime = 0;

int sensor1Value, sensor2Value, sensor3Value;
float sensorPH = 0.0;

bool sensorDataValid = false;

static bool superR4_Inicializado = false;
