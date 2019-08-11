//Pins
#define DHT_PIN D4            // 连接到 DHT 传感器的端口 D4
#define PUMP_PIN D1           // 抽水机 D1
#define VALVE_PIN D2          // 电磁阀 (连接继电器的端口) D2
#define SOIL_MOISTURE_PIN A0  // 土壤湿度传感器 A0

//Config
#include "config.h"
#ifdef ENABLE_DEBUG
#define DEBUG_PRINTLN(...) Serial.println(__VA_ARGS__);
#else
#define DEBUG_PRINTLN(...)
#endif

//WIFI&OTA&FS
#include <ESP8266WiFi.h>
#include <ArduinoOTA.h>
#include <FS.h>

//Ticker&Watchdog
#include <Ticker.h>
Ticker secondTick;
volatile int watchdogCount = 1;

//NTP
#include <NTPClient.h>
#include <WiFiUdp.h>
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "ntp.aliyun.com", 0, 60000);
//FIXME:莫名奇妙会无法与NTP服务器同步时间。(猜测是和路由器有关)

//Json
#include <ArduinoJson.h>

//Socket
#include <SocketIoClient.h>
#ifdef ENABLE_SSL
#define beginwebsocket beginSSL
#else
#define beginwebsocket begin
#endif
SocketIoClient webSocket;

//DHT
#include <dht.h>
#ifdef DHT_VERSION_11
#define readdht read11
#endif
#ifdef DHT_VERSION_22
#define readdht read22
#endif
dht DHT;

//Status
unsigned long lastMillis = 0; //计时器
float temperature;
float relative_humidity;
unsigned long data_readtime;

//Pump&Valve
unsigned long valveMillis = 0; //电磁阀自动关闭计时器
bool valve_delay_trigger = 0;  //电磁阀自动关闭触发器
unsigned long pumpMillis = 0;  //抽水机自动关闭计时器
bool pump_delay_trigger = 0;   //抽水机自动关闭触发器
bool valve = false;
bool pump = false;
unsigned long valve_delay = 60; //电磁阀延时，单位 秒
unsigned long pump_delay = 60;  //抽水机延时，单位 秒

//Auto Watering
bool auto_watering = false;            //自动灌溉控制
unsigned long watering_interval = 720; //灌溉间隔(分钟)
bool moisture_trigger = false;         //湿度触发器
unsigned long soil_moisture = 0;
unsigned long soil_moisture_limit = 1023; //湿度触发阈值
unsigned long last_watering_time = 0;     //上次触发时间

bool need_save_config = false; //设置保存触发器

void event(const char *payload, size_t length)
{
  const size_t capacity = JSON_OBJECT_SIZE(8) + 150;
  DynamicJsonDocument doc(capacity);
  auto error = deserializeJson(doc, payload);
  //Test if parsing succeeds.
  if (error)
  {
    return;
  }

  if (doc["valve"] != "null")
  {
    valve = doc["valve"];
    if (valve)
    {
      valve_delay_trigger = true;
      valveMillis = millis(); //重置电磁阀关闭计时
    }
  }
  if (doc["pump"] != "null")
  {
    pump = doc["pump"];
    if (pump)
    {
      pump_delay_trigger = true;
      pumpMillis = millis(); //重置抽水机关闭计时
    }
  }

  if (doc["auto_watering"] != "null")
  {
    auto_watering = doc["auto_watering"]; //自动灌溉
    moisture_trigger = doc["auto_watering"];
    need_save_config = true;
  }
  if (doc["moisture_trigger"] != "null")
  {
    moisture_trigger = doc["moisture_trigger"]; //湿度触发器
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
  if (doc["soil_moisture_limit"] != "null")
  {
    soil_moisture_limit = doc["soil_moisture_limit"];
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
  //We start by connecting to a WiFi network
  WiFi.begin(ssid, password);
  while (WiFi.waitForConnectResult() != WL_CONNECTED)
  {
    delay(5000);
    ESP.restart();
  }
}

void read_data()
{
  int chk = DHT.readdht(DHT_PIN); //读取传感器数据
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
  soil_moisture = analogRead(SOIL_MOISTURE_PIN); //土壤湿度情况
  data_readtime = timeClient.getEpochTime();     //读取数据的时间
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
  payload += "," + String(soil_moisture_limit);
  payload += "\"}";

  char msg[200];
  payload.toCharArray(msg, 200);

  webSocket.emit("devicedata", msg);

  if (reset)
    lastMillis = millis(); //重置上传计时
}

bool load_config()
{
  File configFile = SPIFFS.open("/config.json", "r");
  if (!configFile)
  {
    return false;
  }

  size_t size = configFile.size();

  //Allocate a buffer to store contents of the file.
  std::unique_ptr<char[]> buf(new char[size]);

  //We don't use String here because ArduinoJson library requires the input
  //buffer to be mutable. If you don't use ArduinoJson, you may as well
  //use configFile.readString instead.
  configFile.readBytes(buf.get(), size);

  const size_t capacity = JSON_OBJECT_SIZE(6) + 150;
  DynamicJsonDocument doc(capacity);
  auto error = deserializeJson(doc, buf.get());

  if (error)
  {
    return false;
  }

  // 读取配置---------------
  valve_delay = doc["valve_delay"];
  pump_delay = doc["pump_delay"];
  soil_moisture_limit = doc["soil_moisture_limit"];
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

  // 保存配置---------------
  doc["valve_delay"] = valve_delay;
  doc["pump_delay"] = pump_delay;
  doc["soil_moisture_limit"] = soil_moisture_limit;
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

// 看门狗
void ISRwatchdog()
{
  watchdogCount++;
  if (watchdogCount > 10) // 超过十秒无响应则重置单片机
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
  digitalWrite(VALVE_PIN, LOW); //初始为关闭状态

  SPIFFS.begin(); //配置FS
  if (!load_config())
    save_config(); //读取配置，否则保存默认配置

  setup_wifi(); //配置WIFI

  timeClient.begin(); //NTC服务启动

  //OTA设置
  ArduinoOTA.setPort(8266);
  ArduinoOTA.setHostname(device_name);
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    DEBUG_PRINTLN((float)progress / total * 100);
    watchdogCount = 1; //Feed dog while doing update
  });
  ArduinoOTA.begin();

  //WebSocket设置
  webSocket.beginwebsocket(server_url, server_port);
  webSocket.setAuthorization(admin_name, admin_password);

  char buf[16];
  itoa(device_id, buf, 10);
  webSocket.on(buf, event);

  //Watchdog
  secondTick.attach(1, ISRwatchdog);
}

void loop()
{
  watchdogCount = 1; // Feed dog

  ArduinoOTA.handle(); // OTA
  timeClient.update(); // 更新NTP时间
  webSocket.loop();    // Websocket loop

  // 每10秒上传一次数据
  if (millis() - lastMillis > 10000)
  {
    read_data();
    upload(1);
  }

  // 自动灌溉间隔
  if (auto_watering && !moisture_trigger && timeClient.getEpochTime() - last_watering_time > 60 * watering_interval)
  {
    moisture_trigger = true;
    upload(0);
  }

  // 根据土壤湿度自动打开电磁阀
  if (auto_watering && moisture_trigger && soil_moisture > soil_moisture_limit)
  {
    moisture_trigger = false; // 重置湿度触发器

    last_watering_time = timeClient.getEpochTime(); // 重置上次灌溉时间
    if (save_config())
    {
      DEBUG_PRINTLN("保存配置失败。")
    };

    valve = true;
    digitalWrite(VALVE_PIN, valve);
    valve_delay_trigger = true;
    valveMillis = millis(); // 重置电磁阀关闭计时
    upload(0);
  }

  // 延时关闭电磁阀
  if (valve_delay_trigger && millis() - valveMillis > 1000 * valve_delay)
  {
    valve_delay_trigger = false;
    valve = false;
    digitalWrite(VALVE_PIN, valve);
    upload(0);
  }

  // 延时关闭抽水机
  if (pump_delay_trigger && millis() - pumpMillis > 1000 * pump_delay)
  {
    pump_delay_trigger = false;
    pump = false;
    digitalWrite(PUMP_PIN, pump);
    upload(0);
  }
}
