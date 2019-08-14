// Pins
#define DHT_PIN 2            // D4
#define PUMP_PIN 5           // D1
#define VALVE_PIN 4          // D2
#define SOIL_MOISTURE_PIN 17 // A0

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

// Pump&Valve
unsigned long valveMillis = 0; // Valve Auto Close Timer
bool valve_auto_close = false; // Valve Auto Close Switch
unsigned long pumpMillis = 0;  // Pump Auto Close Timer
bool pump_auto_close = false;  // Pump Auto Close Switch
bool valve = false;
bool pump = false;
unsigned long valve_delay = 60; // Valve Auto Close Delay (seconds)
unsigned long pump_delay = 60;  // Pump Auto Close Delay (seconds)

// Auto Watering
bool auto_watering = false;            // Auto Watering Switch
unsigned long watering_interval = 720; // Auto Watering Interval (minutes)
bool moisture_trigger = false;         // Moisture Trigger State (enable/disable)
unsigned long soil_moisture = 0;
unsigned long soil_moisture_threshold = 1023; // Threshold of soil moisture trigger (0-1023)
unsigned long last_watering_time = 0;         // Last watering time

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

  if (doc["valve"] != "null")
  {
    valve = doc["valve"];
    if (valve)
    {
      valve_auto_close = true;
      valveMillis = millis(); // Reset timer
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

  if (doc["auto_watering"] != "null")
  {
    auto_watering = doc["auto_watering"];
    moisture_trigger = doc["auto_watering"];
    need_save_config = true;
  }
  if (doc["moisture_trigger"] != "null")
  {
    moisture_trigger = doc["moisture_trigger"];
  }

  if (doc["valve_delay"] != "null")
  {
    valve_delay = doc["valve_delay"];
    need_save_config = true;
  }
  if (doc["pump_delay"] != "null")
  {
    pump_delay = doc["pump_delay"];
    need_save_config = true;
  }
  if (doc["soil_moisture_threshold"] != "null")
  {
    soil_moisture_threshold = doc["soil_moisture_threshold"];
    need_save_config = true;
  }
  if (doc["watering_interval"] != "null")
  {
    watering_interval = doc["watering_interval"];
    need_save_config = true;
  }

  digitalWrite(PUMP_PIN, pump);
  digitalWrite(VALVE_PIN, valve);
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
  soil_moisture = analogRead(SOIL_MOISTURE_PIN);
  data_readtime = timeClient.getEpochTime();
}

void upload(bool reset)
{
  String payload = String("{\"data\":\"");
  payload += String(data_readtime);
  payload += "," + String(device_id);
  payload += "|" + String(temperature);
  payload += "," + String(relative_humidity);
  payload += "," + String(soil_moisture);
  payload += "," + String(valve);
  payload += "," + String(pump);
  payload += "," + String(auto_watering);
  payload += "," + String(moisture_trigger);
  payload += "," + String(watering_interval);
  payload += "," + String(valve_delay);
  payload += "," + String(pump_delay);
  payload += "," + String(soil_moisture_threshold);
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

  const size_t capacity = JSON_OBJECT_SIZE(6) + 150;
  DynamicJsonDocument doc(capacity);
  auto error = deserializeJson(doc, buf.get());

  if (error)
  {
    return false;
  }

  // Read Config-----------
  valve_delay = doc["valve_delay"];
  pump_delay = doc["pump_delay"];
  soil_moisture_threshold = doc["soil_moisture_threshold"];
  watering_interval = doc["watering_interval"];
  auto_watering = doc["auto_watering"];
  last_watering_time = doc["last_watering_time"];
  // ----------------------

  return true;
}

bool save_config()
{
  const size_t capacity = JSON_OBJECT_SIZE(6);
  DynamicJsonDocument doc(capacity);

  // Save Config------------
  doc["valve_delay"] = valve_delay;
  doc["pump_delay"] = pump_delay;
  doc["soil_moisture_threshold"] = soil_moisture_threshold;
  doc["watering_interval"] = watering_interval;
  doc["auto_watering"] = auto_watering;
  doc["last_watering_time"] = last_watering_time;
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
  pinMode(VALVE_PIN, OUTPUT);
  digitalWrite(PUMP_PIN, LOW);
  digitalWrite(VALVE_PIN, LOW); //Off

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

  // Enable moisture trigger
  if (auto_watering && !moisture_trigger && timeClient.getEpochTime() - last_watering_time > 60 * watering_interval)
  {
    moisture_trigger = true;
    upload(0);
  }

  // Open valve when soil moisture passes threshold
  if (auto_watering && moisture_trigger && soil_moisture > soil_moisture_threshold)
  {
    moisture_trigger = false; // Reset trigger state

    last_watering_time = timeClient.getEpochTime(); // Reset last watering time
    if (save_config())
    {
      DEBUG_PRINTLN("Save config failed!")
    };

    valve = true;
    digitalWrite(VALVE_PIN, valve);
    valve_auto_close = true;
    valveMillis = millis(); // Reset valve timer
    upload(0);
  }

  // Close valve after certain delay
  if (valve_auto_close && millis() - valveMillis > 1000 * valve_delay)
  {
    valve_auto_close = false;
    valve = false;
    digitalWrite(VALVE_PIN, valve);
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
