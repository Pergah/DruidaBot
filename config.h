
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
//#include <Adafruit_SSD1306.h>
#include <Adafruit_SH110X.h>
#include <ESP32Servo.h>
#include "esp_task_wdt.h"
#include "math.h"



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
#define SUPERCICLO1313 13


// Aca se muestra como van conectados los componentes

//#define sensorTempAgua 5  //Sensor DS
//#define sensorIRpin 13    //Sensor Emisor IR
//#define sensorHS 2 //humedad Suelo
//#define sensorIRreceptor 12
#define SERVO 23
#define RELAY4 18
#define RELAY3 5
#define RELAY5 19
#define RELAY2 17
#define RELAY1 16

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define WDT_TIMEOUT 300 //(Diez minutos)

#define SDA_NANO 33
#define SCL_NANO 27

#define RELAY4_ACTIVE_LOW 1

#define ADDR_VEGE_START    384  // uint32_t
#define ADDR_FLORA_START   388  // uint32_t
#define ADDR_VEGE_DAYS     392  // int32_t
#define ADDR_FLORA_DAYS    396  // int32_t
#define ADDR_LAST_DATEKEY  400  // uint32_t (yyyymmdd local)
#define ADDR_VEGE_ACTIVE   404  // uint8_t
#define ADDR_FLORA_ACTIVE  405  // uint8_t

TwoWire I2CNano = TwoWire(1);  // Usamos el bus I2C número 1

//Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
Adafruit_SH1106G display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1); // -1 = sin pin RESET
Servo dimmerServo; // Objeto del servomotor


//const uint16_t kRecvPin = sensorIRreceptor;
//const uint16_t kCaptureBufferSize = 1024;
//IRrecv irrecv(kRecvPin, kCaptureBufferSize);
//decode_results results;

//uint16_t rawData[150] = { 0 };
//uint16_t rawDataLen = 0;
//IRsend irsend(sensorIRpin);


//ACA VAN LOS TOKENS


const String botToken = "8296049013:AAHz4rHxmmY1yiqyil3sahGzNWk41ARqFE8"; //DRUIDA 18




//ACA VAN LOS TOKENS


const char* ssid_AP = "DruidaBot (02)"; // 


const char* password_AP = "12345678";          // Contraseña de la red AP

// ID: 1308350088 

//String scriptId = "AKfycbwUlj-gk1NNDHwxxebIqH7vS0N8qbu9LZydo4QeyAwmULEQ8JcSGNt8RRxRLdoIRRTA"; //Druida 01 (SHASTIN)
String scriptId = "AKfycbwUlj-gk1NNDHwxxebIqH7vS0N8qbu9LZydo4QeyAwmULEQ8JcSGNt8RRxRLdoIRRTA";  //Druida 02 (Matheu)


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

//byte modoR5 = 0;
float minR5 = 0;
float maxR5 = 0;
byte paramR5 = 0;
int timeOnR5 = 0;
int timeOffR5 = 0;
int horaOnR5 = 0;
int minOnR5 = 0;
int horaOffR5 = 0;
int minOffR5 = 0;

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
int R5config = -1;
int R2config = -1;
//int R2irconfig = -1;
int R3config = -1;
int R4config = -1;

byte estadoR1 = 0;
//byte estadoR5 = 0;
byte estadoR2 = 0;
//byte estadoR2ir = 0;
byte estadoR3 = 0;
byte estadoR4 = 0;

bool R1estado = HIGH;
bool R2estado = HIGH;
//bool R2irestado = HIGH;
bool R3estado = HIGH;
bool R4estado = HIGH;
//bool R5estado = HIGH;

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
int tiempoR5 = 0;
int tiempoR2 = 0;
//int tiempoR2ir = 0;
int tiempoR3 = 0;
int tiempoR4 = 0; 
bool esperandoTiempoR1 = false;
bool esperandoTiempoR5 = false;
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

String relayNames[] = {"Humidificacion", "Extraccion", "Irrigacion", "Iluminacion", "Aire Acondicionado", "Calefaccion", "Deshumidificacion", "Intraccion"};
String relayAssignedNames[6] = {"R1", "R5", "R2", "R3", "R4", "R2ir"}; // Nombres actuales para cada relé

int R1name = 0;   // (Humidificacion)
int R5name = 0;   // (Humidificacion)
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
// Variables para el modo AUTORIEGO del relay 1
int tiempoEncendidoR5 = 5; // en minutos
int tiempoApagadoR5 = 10;  // en minutos
unsigned long previousMillisR5 = 0;
bool enHumidificacion = false;

int tiempoGoogle = -1;

int tiempoTelegram = -1; // En minutos, configurable desde la web

int unidadRiego = 60;     // 1 = seg, 60 = min, 3600 = h
int unidadNoRiego = 3600; // valores cargados desde EEPROM o lo que uses

byte direccionR1 = 0;
byte direccionR5 = 0;

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

// ====== Polaridad de R5 ======
//const bool R5_ACTIVO_EN_HIGH = true;  // R5 cierra con HIGH

// ====== Estado lógico y físico de R5 ======
// Lógico (se persiste): 0 = OFF, 1 = ON
uint8_t estadoR5 = 0;
// Modo (se persiste): 0 = AUTO, 1 = MANUAL (ajustá si tu enum difiere)
uint8_t modoR5   = 1; // MANUAL por defecto si no lo tenías
// Físico (cache del último nivel escrito al pin)
uint8_t R5estado = LOW;
const bool R5_ACTIVO_EN_HIGH = true;

static const size_t SSID_CAP   = 50; // ocupa [37..86]
static const size_t PASS_CAP   = 50; // ocupa [87..136]
static const size_t CHATID_CAP = 25; // ocupa [215..239]

// Zona segura post-boot para persistencia (anti-brownout/escrituras con datos vacíos)
bool canPersist = false;
unsigned long bootMs = 0;

// Validador de horarios H:M
inline bool horarioOK(int h, int m) { return (h >= 0 && h < 24 && m >= 0 && m < 60); }

// ===== SUPERCICLO R4 =====
// Variables de scheduling SIEMPRE en minutos [0..1439]
extern int16_t nextOnR4Abs  = -1;
extern int16_t nextOffR4Abs = -1;

// ===== Ciclos de cultivo (persistentes con Guardado_General) =====
uint32_t vegeStartEpoch  = 0;   // 0 = no iniciado
uint32_t floraStartEpoch = 0;   // 0 = no iniciado

bool vegeActive  = false;
bool floraActive = false;

int  vegeDays    = 0;           // 0 = sin iniciar / "--" en UI
int  floraDays   = 0;

uint32_t lastDateKey = 0;       // yyyymmdd local (-3h)

bool superEnabled = false;

int32_t superAnchorEpochR4 = 0; // ancla absoluta del ciclo (epoch local)

static int32_t nextOnEpoch_R4  = -1;
static int32_t nextOffEpoch_R4 = -1;

int32_t supercycleStartEpochR4 = 0;  // se guarda la fecha/hora de inicio en epoch

const int SUPERCYCLE_13H = 13 * 60; // 780 min
