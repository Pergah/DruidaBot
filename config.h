
/* Druida BOT 1.618

Conexion de sensores: 

Sensor Emisor IR -> 4
Temperatura de Agua -> 5
Sensor IR Receptor -> 14
Temperatura y Humedad ambiente -> 19
Sensor PH -> 34

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
#include <OneWire.h>
#include <DallasTemperature.h>
#include <Arduino.h>
#include <IRrecv.h>
#include <IRremoteESP8266.h>
#include <IRutils.h>
#include <IRsend.h>
#include <Adafruit_AHTX0.h>


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

// Aca se muestra como van conectados los componentes

#define sensorTempAgua 5  //Sensor DS
#define sensorIRpin 12    //Sensor Emisor IR
#define sensorHS 13
#define sensorIRreceptor 14
#define RELAY4 15
#define RELAY3 16
#define RELAY2 17
#define RELAY1 18
#define sensor1PIN 19  //Temperatura y humedad Ambiente
#define SensorPin 34   //PH

#define Offset 0.00  // deviation compensate
#define samplingInterval 20
#define printInterval 800
#define ArrayLenth 40  // times of collection

int pHArray[ArrayLenth];  // Store the average value of the sensor feedback
int pHArrayIndex = 0;

const uint16_t kRecvPin = sensorIRreceptor;
const uint16_t kCaptureBufferSize = 1024;
IRrecv irrecv(kRecvPin, kCaptureBufferSize);
decode_results results;

uint16_t IRsignal[150] = { 0 };
uint16_t IRsignalLength = 0;
IRsend irsend(sensorIRpin);

//const String botToken = "6920896340:AAEdvJl1v67McffACbdNXLhjMe00f_ji_ag"; //DRUIDA UNO (caba y roge)
//const String botToken = "6867697701:AAHtaJ4YC3dDtk1RuFWD-_f72S5MYvlCV4w"; //DRUIDA DOS (rasta)
//const String botToken = "7273841170:AAHxWF33cIDcIoxgBm3x9tzn9ISJIKoy7X8"; //DRUIDA TRES (brai e ivana)
const String botToken = "7314697588:AAGJdgljHPSb47EWcfYUR1Rs-7ia0_domok";  //DRUIDA CUATRO (matheu)

const unsigned long BOT_MTBS = 1000;
const int MAX_STRING_LENGTH = 32;
unsigned long bot_lasttime;
const unsigned long wifiCheckInterval = 600000;  //WiFi CheckStatus cada 10 minutos
unsigned long previousMillis = 0;

const int oneWireBus = sensorTempAgua;

WiFiClientSecure secured_client;
UniversalTelegramBot bot(botToken, secured_client);


//DHT dht(sensor1PIN, DHT11);
//DHT dht(sensor1PIN, DHT22);

Adafruit_AHTX0 aht;

OneWire oneWire(oneWireBus);
DallasTemperature sensors(&oneWire);

RTC_DS3231 rtc;

String chat_id = "";
String ssid = "";
String password = "";

//String scriptId = "AKfycbwXhUu15DVEI4b1BDf8Y8Up_qKIXDUvfWgHLKppNL6rUMOnfiQRDfxGXtCt3_n0NXt_Nw"; //Druida UNO (Caba)
String scriptId = "AKfycbwUlj-gk1NNDHwxxebIqH7vS0N8qbu9LZydo4QeyAwmULEQ8JcSGNt8RRxRLdoIRRTA";  //Druida TRES (Matheu)

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
int minR3 = 0;
int maxR3 = 0;
byte paramR3 = HS;
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

bool R1estado = HIGH;
bool R2estado = HIGH;
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

float PHval, PHvolt;
byte estadoRTC = 0;

int tiempoR1 = 0;
int tiempoR2 = 0;
int tiempoR3 = 0;
int tiempoR4 = 0; 
bool esperandoTiempoR1 = false;
bool esperandoTiempoR2 = false;
bool esperandoTiempoR3 = false;
bool esperandoTiempoR4 = false;


