//Config
#include "config.test.h"
#ifdef ENABLE_DEBUG
#define DEBUG_PRINTLN(...) Serial.println(__VA_ARGS__);
#else
#define DEBUG_PRINTLN(...)
#endif

//WIFI & OTA
#include <ESP8266WiFi.h>
#include <ArduinoOTA.h>

//NTP
#include <NTPClient.h>
#include <WiFiUdp.h>
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "ntp.aliyun.com", 0, 60000);
//FIXME: 莫名奇妙会无法与NTP服务器同步时间。(猜测是和路由器有关)

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
  Serial.println("Data received");
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

void setup()
{
#ifdef ENABLE_DEBUG
  Serial.begin(115200);
#endif

  setup_wifi(); //Setup WIFI

  timeClient.begin(); //Start NTC service

  //Setup OTA
  ArduinoOTA.setPort(8266);
  ArduinoOTA.setHostname(device_name);
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    DEBUG_PRINTLN((float)progress / total * 100);
  });
  ArduinoOTA.begin();

  //Setup WebSocket
  webSocket.beginwebsocket(server_url, server_port);
  webSocket.setAuthorization(admin_name, admin_password);

  char buf[16];
  itoa(device_id, buf, 10);
  webSocket.on(buf, event);
}

unsigned long messageTimestamp = 0;
void loop()
{
  ArduinoOTA.handle(); //OTA
  timeClient.update(); //Update time over NTP
  webSocket.loop();    //Websocket loop

  uint64_t now = millis();

  if (now - messageTimestamp > 2000)
  {
    webSocket.emit("devicedata", "0w0");
    messageTimestamp = now;
  }
}
