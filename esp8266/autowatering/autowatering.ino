// Pins
#define DHT_PIN D4  
#define VALVE1_PIN D5        
#define VALVE2_PIN D6        
#define VALVE3_PIN D7        
#define PUMP_PIN D8 

// Config
#include "config.h"
#ifdef ENABLE_DEBUG
#define DEBUG_PRINTLN(...) Serial.println(__VA_ARGS__);
#else
#define DEBUG_PRINTLN(...)
#endif

// WIFI&OTA&FS
#include <ESP8266WiFi.h>
#include <ArduinoOTA.h>
#include <FS.h>

// Ticker&Watchdog
#include <Ticker.h>
Ticker secondTick;
volatile int watchdogCount = 1;

// NTP
#include <NTPClient.h>
#include <WiFiUdp.h>
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "ntp.aliyun.com");
// FIXME: Sometime it can't connect to ntp server, may causeed by router.

// Json
#include <ArduinoJson.h>

// SocketIO
#include <SocketIoClient.h>
#ifdef ENABLE_SSL
#define beginwebsocket beginSSL
#else
#define beginwebsocket begin
#endif
SocketIoClient socketIO;

// DHT
#include <dht.h>
#ifdef DHT_VERSION_11
#define readdht read11
#endif
#ifdef DHT_VERSION_22
#define readdht read22
#endif
dht DHT;

// Status
unsigned long lastMillis = 0; // Upload Data Timer
float temperature;
float relative_humidity;
unsigned long data_readtime;
long wifi_signal;

// Pump&Valve
unsigned long valve1Millis = 0; // Valve Auto Close Timer
bool valve1_auto_close = false; // Valve Auto Close Switch
unsigned long valve2Millis = 0; // Valve Auto Close Timer
bool valve2_auto_close = false; // Valve Auto Close Switch
unsigned long valve3Millis = 0; // Valve Auto Close Timer
bool valve3_auto_close = false; // Valve Auto Close Switch
unsigned long pumpMillis = 0;  // Pump Auto Close Timer
bool pump_auto_close = false;  // Pump Auto Close Switch
bool valve1 = false;
bool valve2 = false;
bool valve3 = false;
bool pump = false;
unsigned long valve1_delay = 60; // Valve Auto Close Delay (seconds)
unsigned long valve2_delay = 60; // Valve Auto Close Delay (seconds)
unsigned long valve3_delay = 60; // Valve Auto Close Delay (seconds)
unsigned long pump_delay = 60;  // Pump Auto Close Delay (seconds)

bool need_save_config = false;

void event(const char *payload, size_t length)
{
  const size_t capacity = JSON_OBJECT_SIZE(8) + 150;
  DynamicJsonDocument doc(capacity);
  auto error = deserializeJson(doc, payload);
  // Test if parsing succeeds.
  if (error)
  {
    return;
  }

  if (doc["valve1"] != "null")
  {
    valve1 = doc["valve1"];
    if (valve1)
    {
      valve1_auto_close = true;
      valve1Millis = millis(); // Reset timer
    }
  }
  if (doc["valve2"] != "null")
  {
    valve2 = doc["valve2"];
    if (valve2)
    {
      valve2_auto_close = true;
      valve2Millis = millis(); // Reset timer
    }
  }
  if (doc["valve3"] != "null")
  {
    valve3 = doc["valve3"];
    if (valve3)
    {
      valve3_auto_close = true;
      valve3Millis = millis(); // Reset timer
    }
  }
  if (doc["pump"] != "null")
  {
    pump = doc["pump"];
    if (pump)
    {
      pump_auto_close = true;
      pumpMillis = millis(); // Reset timer
    }
  }

  if (doc["valve1_delay"] != "null")
  {
    valve1_delay = doc["valve1_delay"];
    need_save_config = true;
  }
  if (doc["valve2_delay"] != "null")
  {
    valve2_delay = doc["valve2_delay"];
    need_save_config = true;
  }
  if (doc["valve3_delay"] != "null")
  {
    valve3_delay = doc["valve3_delay"];
    need_save_config = true;
  }
  if (doc["pump_delay"] != "null")
  {
    pump_delay = doc["pump_delay"];
    need_save_config = true;
  }

  digitalWrite(PUMP_PIN, pump);
  digitalWrite(VALVE1_PIN, valve1);
  digitalWrite(VALVE2_PIN, valve2);
  digitalWrite(VALVE3_PIN, valve3);
  data_readtime = timeClient.getEpochTime();

  upload(0);
  if (need_save_config)
  {
    save_config();
    need_save_config = false;
  }
}

void setup_wifi()
{
  delay(10);
  WiFi.mode(WIFI_STA);
  // We start by connecting to a WiFi network
  WiFi.begin(ssid, password);
  while (WiFi.waitForConnectResult() != WL_CONNECTED)
  {
    delay(5000);
    ESP.restart();
  }
}

// Read sensor data
void read_data()
{
  int chk = DHT.readdht(DHT_PIN);
  switch (chk)
  {
  case DHTLIB_OK:
    relative_humidity = DHT.humidity;
    temperature = DHT.temperature;
    break;
  default:
    relative_humidity = NULL;
    temperature = NULL;
    break;
  }
  wifi_signal = WiFi.RSSI();
  data_readtime = timeClient.getEpochTime();
}

void upload(bool reset)
{
  String payload = String("{\"data\":\"");
  payload += String(data_readtime);
  payload += "," + String(device_id);
  payload += "|" + String(temperature);
  payload += "," + String(relative_humidity);
  payload += "," + String(valve1);
  payload += "," + String(valve2);
  payload += "," + String(valve3);
  payload += "," + String(pump);
  payload += "," + String(valve1_delay);
  payload += "," + String(valve2_delay);
  payload += "," + String(valve3_delay);
  payload += "," + String(pump_delay);
  payload += "," + String(wifi_signal);
  payload += "\"}";

  char msg[200];
  payload.toCharArray(msg, 200);

  socketIO.emit("devicedata", msg);

  if (reset)
    lastMillis = millis(); // Reset the upload data timer
}

bool load_config()
{
  File configFile = SPIFFS.open("/config.json", "r");
  if (!configFile)
  {
    return false;
  }

  size_t size = configFile.size();

  // Allocate a buffer to store contents of the file.
  std::unique_ptr<char[]> buf(new char[size]);

  // We don't use String here because ArduinoJson library requires the input
  // buffer to be mutable. If you don't use ArduinoJson, you may as well
  // use configFile.readString instead.
  configFile.readBytes(buf.get(), size);

  const size_t capacity = JSON_OBJECT_SIZE(4) + 150;
  DynamicJsonDocument doc(capacity);
  auto error = deserializeJson(doc, buf.get());

  if (error)
  {
    return false;
  }

  // Read Config-----------
  valve1_delay = doc["valve1_delay"];
  valve2_delay = doc["valve2_delay"];
  valve3_delay = doc["valve3_delay"];
  pump_delay = doc["pump_delay"];
  // ----------------------

  return true;
}

bool save_config()
{
  const size_t capacity = JSON_OBJECT_SIZE(4);
  DynamicJsonDocument doc(capacity);

  // Save Config------------
  doc["valve1_delay"] = valve1_delay;
  doc["valve2_delay"] = valve2_delay;
  doc["valve3_delay"] = valve3_delay;
  doc["pump_delay"] = pump_delay;
  // -----------------------

  File configFile = SPIFFS.open("/config.json", "w");
  if (!configFile)
  {
    return false;
  }

  serializeJson(doc, configFile);
  return true;
}

// Watchdog
void ISRwatchdog()
{
  watchdogCount++;
  if (watchdogCount > 10) // Not Responding for 10 seconds, it will reset the board.
  {
    ESP.reset();
  }
}

void setup()
{
#ifdef ENABLE_DEBUG
  Serial.begin(115200);
#endif

  pinMode(PUMP_PIN, OUTPUT);
  pinMode(VALVE1_PIN, OUTPUT);
  pinMode(VALVE2_PIN, OUTPUT);
  pinMode(VALVE3_PIN, OUTPUT);
  digitalWrite(PUMP_PIN, LOW);
  digitalWrite(VALVE1_PIN, LOW);
  digitalWrite(VALVE2_PIN, LOW);
  digitalWrite(VALVE3_PIN, LOW); //Off

  SPIFFS.begin(); //FS
  if (!load_config())
    save_config(); // Read config, or save default settings.

  setup_wifi(); // Setup Wi-Fi

  timeClient.begin(); // Start NTC service

  // OTA
  ArduinoOTA.setPort(8266);
  ArduinoOTA.setHostname(device_name);
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    DEBUG_PRINTLN((float)progress / total * 100);
    watchdogCount = 1; // Feed dog while doing update
  });
  ArduinoOTA.begin();

  // WebSocket
  socketIO.beginwebsocket(server_url, server_port);
  socketIO.setAuthorization(admin_name, admin_password);

  char buf[16];
  itoa(device_id, buf, 10);
  socketIO.on(buf, event);

  // Watchdog
  secondTick.attach(1, ISRwatchdog);
}

void loop()
{
  watchdogCount = 1; // Feed dog

  ArduinoOTA.handle(); // OTA
  timeClient.update(); // NTP
  socketIO.loop();     // Websocket

  // Upload data every 10 seconds
  if (millis() - lastMillis > 10000)
  {
    read_data();
    upload(1);
  }

  // Close valve1 after certain delay
  if (valve1_auto_close && millis() - valve1Millis > 1000 * valve1_delay)
  {
    valve1_auto_close = false;
    valve1 = false;
    digitalWrite(VALVE1_PIN, valve1);
    upload(0);
  }
  // Close valve2 after certain delay
  if (valve2_auto_close && millis() - valve2Millis > 1000 * valve2_delay)
  {
    valve2_auto_close = false;
    valve2 = false;
    digitalWrite(VALVE2_PIN, valve2);
    upload(0);
  }
  // Close valve3 after certain delay
  if (valve3_auto_close && millis() - valve3Millis > 1000 * valve3_delay)
  {
    valve3_auto_close = false;
    valve3 = false;
    digitalWrite(VALVE3_PIN, valve3);
    upload(0);
  }

  // Close pump after certain delay
  if (pump_auto_close && millis() - pumpMillis > 1000 * pump_delay)
  {
    pump_auto_close = false;
    pump = false;
    digitalWrite(PUMP_PIN, pump);
    upload(0);
  }
}
