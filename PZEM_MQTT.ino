
#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <ESP8266HTTPUpdateServer.h>
#include <PubSubClient.h>
#include <SoftwareSerial.h>
#include <PZEM004T.h>

//PZEM-004T设置
PZEM004T pzem(14, 12);                                                       //pzem(RX, TX),分别连接PZEM的TX和RX
IPAddress ip(192, 168, 1, 1);

//WIFI连接设置
const char* ssid = "ASUS";                                                    //无线网SSID
const char* password = "**********";                                           //无线网密码

//MQTT服务器设置
#define mqtt_server "192.168.123.100"                                         //MQTT服务器地址
#define mqtt_port 1883                                                        //MQTT服务器通讯端口
#define mqtt_auth true                                                        //MQTT服务器是否需要验证
const String mqtt_clientname = "energy meter";                                //MQTT客户端名称
const char* mqtt_user = "homeassistant";                                      //MQTT服务器用户名
const char* mqtt_pass = "******";                                           //MQTT服务器密码

//emoncms设置
#define enable_emoncms false                                                   //是否上传数据到enable_emoncms
const char* emoncms_host = "emoncms.org";                                     //emoncms地址
const String emoncms_apikey = "*************************";             //emoncms apikey

//OTA设置
const char* host = "esp8266-webupdate";
const char* update_path = "/update";                                          //OTA页面地址
const char* update_username = "admin";                                        //OTA用户名
const char* update_password = "admin";                                     //OTA密码

float sens[4];

ESP8266WebServer httpServer(80);
ESP8266HTTPUpdateServer httpUpdater;

//订阅回调函数
void callback(char* topic, byte* payload, unsigned int length) {
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.println("] ");
  String strTop = topic;
  if (strTop == "set/energy/restart") {
    //重启
    Serial.println("Reboot initiated");
    ESP.restart();
  }
}

WiFiClient wifiClient;
PubSubClient client(mqtt_server, mqtt_port, callback, wifiClient);

//#define BUFFER_SIZE 100

//wifi断线重连
void wl_reconnect() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.print("Connecting to ");
    Serial.print(ssid);
    Serial.println("...");
    WiFi.begin(ssid, password);
  }
  else {
    delay(5000);
  }
}

//mqtt服务断线重连
void mqtt_reconnect() {
  if (WiFi.status() == WL_CONNECTED) {
    // Loop until we're reconnected to the MQTT server
    while (!client.connected()) {
      Serial.print("Attempting MQTT connection...");
      if (mqtt_auth ? client.connect(mqtt_clientname.c_str(), mqtt_user, mqtt_pass) : client.connect(mqtt_clientname.c_str())) {
        Serial.println("MQTT Connected.");
        //订阅主题
        client.subscribe("set/energy/restart");
        Serial.println("Topic subscribed.");
        //发布存活主题
        client.publish("home/energy/alive", "true");
      }
      else {
        //连接失败，输出错误信息
        Serial.print("Failed.connection state: ");
        Serial.println(client.state ());
        delay(5000);
        //        abort();
      }
    }
  }
}

//上传数据到emoncms
void update_emoncms(const String emoncms_data) {
  WiFiClient emoncmsclient;
  if (!emoncmsclient.connect(emoncms_host, 80)) {
    Serial.println( "Connection Failed");
    return;
  }
  String url = "/api/post?node=home&apikey=" + emoncms_apikey + "&json={" + emoncms_data + "}";
  Serial.print("Requesting URL: ");
  Serial.println(url);
  emoncmsclient.print(String("GET ") + url + " HTTP/1.1\r\n" +
                      "Host: " + emoncms_host + "\r\n" +
                      "Connection: close\r\n\r\n");
  unsigned long timeout = millis();
  while (emoncmsclient.available() == 0) {
    if (millis() - timeout > 5000) {
      Serial.println(">>> emoncmsClient Timeout !");
      emoncmsclient.stop();
      return;
    }
  }
}

void setup() {
  Serial.begin(115200);
  pzem.setAddress(ip);

  //start wifi subsystem
  WiFi.begin(ssid, password);

  //attempt to connect to the WIFI network and then connect to the MQTT server
  wl_reconnect();

  Serial.print("IP: ");
  Serial.println(WiFi.localIP());

  mqtt_reconnect();

  //MQTT
  client.publish("home/energy/alive", "true");
  client.subscribe("set/energy/restart");
  Serial.println("subscribed topic...");

  //OTA
  MDNS.begin(host);
  httpUpdater.setup(&httpServer, update_path, update_username, update_password);
  httpServer.begin();
  MDNS.addService("http", "tcp", 80);
  Serial.printf("HTTPUpdateServer ready! Open http://%s.local%s in your browser and login with username '%s' and password '%s'\n", host, update_path, update_username, update_password);

}

void loop() {

  //WIFI断线重连
  if (WiFi.status() == 3) {
    wl_reconnect();
  }

  //MQTT断线重连
  if (!client.connected()) {
    mqtt_reconnect();
  }

  //maintain MQTT connection
  delay(300);
  httpServer.handleClient();

  String emoncms_data = "";

  sens[0] = pzem.voltage(ip);
  if (sens[0] >= 0.0) {
    Serial.print(sens[0]);
    Serial.println(" V; ");
    client.publish("home/energy/v", String(sens[0]).c_str());
    emoncms_data += "voltage:" + String(sens[0]) + ",";
  }

  sens[1] = pzem.current(ip);
  if (sens[1] >= 0.0) {
    Serial.print(sens[1]);
    Serial.println(" A; ");
    client.publish("home/energy/a", String(sens[1]).c_str());
    emoncms_data += "current:" + String(sens[1]) + ",";
  }

  sens[2] = pzem.power(ip);
  if (sens[2] >= 0.0) {
    Serial.print(sens[2]);
    Serial.println(" W; ");
    client.publish("home/energy/p", String(sens[2]).c_str());
    emoncms_data += "power:" + String(sens[2]) + ",";
  }

  sens[3] = pzem.energy(ip);
  if (sens[3] >= 0.0) {
    //转换为kWh
    sens[3] = sens[3] * 0.001;
    Serial.print(sens[3]);
    Serial.println(" kWh; ");
    client.publish("home/energy/e", String(sens[3]).c_str());
    emoncms_data += "energy:" + String(sens[3]) + ",";
  }

  if ((emoncms_data != "") && enable_emoncms ) {
    //去除拼接字符串末尾逗号
    emoncms_data.remove(emoncms_data.length() - 1 );
    //发送数据到emoncms
    update_emoncms(emoncms_data);
  }
  //更新存活状态
  client.publish("home/energy/alive", "true");
  client.loop();
  //喂狗
  wdt_reset();
}
