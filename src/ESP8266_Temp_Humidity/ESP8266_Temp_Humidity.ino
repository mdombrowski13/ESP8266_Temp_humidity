/******************************************************
   Program:    ESP8266_Temp_Humidity
   C++_file:   ESP8266_Temp_Humidity.ino
   Version:    1.1.0
   Created:    May 1st, 2018
   Created By: Michael Dombrowski
               mike@domcoelectronics.com

   Description:
   This program is designed to monitor the temperature
   and humidity and send the data to a remote logging 
   database. 

   Copyright (c) 2018, DomCo Electronics, Inc.
 *****************************************************/

// include
#include <ArduinoOTA.h>
#include "DHTesp.h"
#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266mDNS.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPUpdateServer.h>
#include <Ticker.h>
#include <time.h>
#include <simpleDSTadjust.h>
#include <WiFiManager.h>

//#define STATIC_IP_NETWORK           // Uncomment this out if you want to set a static IP address once it joins your network.
//#define CLEAR_SSID_ON_EVERY_BOOT    // Uncomment this code if you want the DomCo Power Switch to clear the stored SSID/PASSWORD every time the code is flashed.  This will cause it to boot to AP mode every time.

// define
#define VERSION_MAJOR                           1
#define VERSION_MINOR                           1
#define VERSION_BUILD                           0

//TIME AND TIMERS
#define NTP_UPDATE_INTERVAL_SEC                 3600          // Update time from NTP server every 1 hours (3600 Seconds in an HOUR)
#define TEMP_HUMIDITY_INTERVAL_SEC              5             // Update temp/humidity reading every 2 seconds as the sensor can't return updated values any faster
#define NTP_SERVERS                             "us.pool.ntp.org", "pool.ntp.org", "time.nist.gov"    // Maximum of 3 servers
#define timezone                                -6 // US Central Time Zone

#define BAUDRATE                                115200
#define SERVER_PORT                             80

#define DHT22_SENSOR                            5
#define WIFI_LED                                4
#define RESET_PIN                               12
#define ERROR_LED                               13

// LED ACTIONS
#define LIGHT_OFF                               HIGH
#define LIGHT_ON                                !LIGHT_OFF

#ifdef STATIC_IP_NETWORK
  IPAddress _ip = IPAddress(192, 168, 1, 235);    // set static IP Address here
  IPAddress _gw = IPAddress(192, 168, 1, 1);      // set static IP Gateway here
  IPAddress _sn = IPAddress(255, 255, 255, 0);    // set static IP Subnet Mask here
#endif


const char* mdnsName =     "DCE-TH-";
String uniquieDeviceName;

String site;  //HTML web page string for displaying web pages

struct dstRule StartRule = {"CDT", Second, Sun, Mar, 2, 3600};    // Daylight time = UTC/GMT -5 hours
struct dstRule EndRule = {"CST", First, Sun, Nov, 2, 0};       // Standard time = UTC/GMT -6 hour

//Counters
Ticker NTP_Ticker_Obj;
Ticker Sensor_Ticker_Obj;
int32_t NTP_Ticks;
int32_t sensor_Ticks;
bool readyForNtpUpdate = false; // flag changed in the ticker function to start NTP Update
bool readyforTempHumidityUpdate = false;  //flag changed in the ticker function to read Temp/Humidity sensor

//Sensors
float humidity;
float temperature;

ESP8266WebServer server(SERVER_PORT);
ESP8266HTTPUpdateServer httpUpdater;
simpleDSTadjust dstAdjusted(StartRule, EndRule);// Setup simpleDSTadjust Library rules
DHTesp dht;


String ethernetTime(time_t offset)
{
  char buf[30];
  char *dstAbbrev;
  time_t t = dstAdjusted.time(&dstAbbrev);
  struct tm *timeinfo = localtime (&t);
  
  int hour = (timeinfo->tm_hour+11)%12+1;  // take care of noon and midnight
  sprintf(buf, "%02d/%02d/%04d %02d:%02d:%02d%s %s",timeinfo->tm_mon+1, timeinfo->tm_mday, timeinfo->tm_year+1900, hour, timeinfo->tm_min, timeinfo->tm_sec, timeinfo->tm_hour>=12?"pm":"am", dstAbbrev);
  return buf;
}


String ipToString(IPAddress ip){
  String s="";
  for (int i=0; i<4; i++)
    s += i  ? "." + String(ip[i]) : String(ip[i]);
  return s;
}


String getMacAddress() 
{
  byte mac[6];

  WiFi.macAddress(mac);
  String cMac = "";
  for (int i = 0; i < 6; ++i) 
  {
    if (mac[i]<0x10) 
    {
      cMac += "0";
    }
  
  cMac += String(mac[i],HEX);
  if(i<5)
    cMac += ":"; // delimiter between bytes
  }
  
  cMac.toUpperCase();
  return cMac;
}


void create_WebSite()
{
 site = "<html><head><style>";
 site += "h1   {color:Black;font-size: 25px;}";
 site += "table {font-family: arial, sans-serif; border-collapse: collapse; width: 100%;}";
 site += "td, th {border: 1px solid #d93434; text-align: center; padding: 8px;}";
 site += "tr:nth-child(even) {background-color: #dddddd;}";
 site += ".buttonContainer {text-align: center; display: inline-block; position: absolute; left: 43%;}";
 site += ".buttonjson {background-image: -webkit-linear-gradient(top, #e1e5e8, #5e8fab); background-image: -moz-linear-gradient(top, #e1e5e8, #5e8fab); background-image: -ms-linear-gradient(top, #e1e5e8, #5e8fab); background-image: -o-linear-gradient(top, #e1e5e8, #5e8fab); background-image: linear-gradient(to bottom, #e1e5e8, #5e8fab); -webkit-border-radius: 10; -moz-border-radius: 10; border-radius: 10px; -webkit-box-shadow: 0px 1px 3px #666666; -moz-box-shadow: 0px 1px 3px #666666; box-shadow: 0px 1px 3px #666666; font-family: Arial; color: #000000; font-size: 10px; padding: 10px 20px 10px 20px; text-decoration: none; margin: 20px;}";
 site += ".buttonupdate {background-image: -webkit-linear-gradient(top, #d93434, #b82c2c); background-image: -moz-linear-gradient(top, #d93434, #b82c2c); background-image: -ms-linear-gradient(top, #d93434, #b82c2c); background-image: -o-linear-gradient(top, #d93434, #b82c2c); background-image: linear-gradient(to bottom, #d93434, #b82c2c); -webkit-border-radius: 10; -moz-border-radius: 10; border-radius: 10px; -webkit-box-shadow: 0px 1px 3px #666666; -moz-box-shadow: 0px 1px 3px #666666; box-shadow: 0px 1px 3px #666666; font-family: Arial; color: #000000; font-size: 10px; padding: 10px 20px 10px 20px; text-decoration: none; margin: 20px;}";
 site += "</style></head><body>";
 site += "<h1 align=\"center\">";
 site += "Temp/Humidity Data";
 site += "</h1>";
 site += "<table align=\"center\"><tr><th>Data</th><th>Value</th></tr>";
 site += "<tr><td>Temp:</td><td>";
 site += temperature;
 site += " F</td></tr>";
 site += "<tr><td>Humidity:</td><td>";
 site += humidity;
 site += " %RH</td></tr>";
 site += "<tr><td>Date/Time:</td><td>";
 site += ethernetTime(NTP_Ticks);
 site += "</td></tr>";
 site += "</table><br></br>";
 site += "<table align=\"center\"><tr><th>System</th><th></th></tr>";
 site += "<tr><td>Firmware Version:</td><td>";
 site += VERSION_MAJOR;
 site += ".";
 site += VERSION_MINOR;
 site += ".";
 site += VERSION_BUILD;
 site += "</td></tr>";
 site += "<tr><td>IP Address:</td><td>";
 site += ipToString(WiFi.localIP());
 site += "</td></tr>";
 site += "<tr><td>mDNS:</td><td>";
 site += "http://";
 site += uniquieDeviceName;
 site += ".local";
 site += "</td></tr>";
 site += "<tr><td>MAC:</td><td>";
 site += getMacAddress();
 site += "</td></tr>";
 site += "</table><br></br>";
 site += "<div class=\"buttonContainer\">";
 site += "<a href=\"/json\" class=\"buttonjson\">Get Json Data</a>";
 site += "<a href=\"/update\" class=\"buttonupdate\">Firmware Update</a></div>";

 site += "</body></html>";
}


void create_jsonWebSite()
{
  site = "{";
  site += "\"Date/Time\":\"";
  site += ethernetTime(NTP_Ticks);
  site += "\",\"hwid\":\"";
  site += uniquieDeviceName;
  site += "\"";
  site += ",\"temp\":\"";
  site += temperature;
  site += "F\"";
  site += ",\"humidity\":\"";
  site += humidity;
  site += "%RH\"";
  site += "}";  
}

void handleSite()
{
  Serial.println("Default WebPage loaded");
  create_WebSite();
  server.send(200, "text/html", site);
  delay(100);
}


void jsonSite()
{
  Serial.println("JSON WebPage loaded");
  create_jsonWebSite();
  server.send(200, "application/json", site);
  delay(100);
}


// NTP timer update ticker
void secTicker_NTP()
{
  NTP_Ticks--;
  if(NTP_Ticks<=0)
   {
    readyForNtpUpdate = true;
    NTP_Ticks = NTP_UPDATE_INTERVAL_SEC; // Re-arm
   }

  // printTime(0);  // Uncomment if you want to see time printed every second
}


//Temp Humidiity sensor timer update ticker
void secTicker_TempHumidity()
{
  sensor_Ticks--;
  if(sensor_Ticks<=0)
   {
    readyforTempHumidityUpdate = true;
    sensor_Ticks = TEMP_HUMIDITY_INTERVAL_SEC; // Re-arm
   }
   
  // printTime(0);  // Uncomment if you want to see time printed every second
}


void updateNTP() 
{
  readyForNtpUpdate = false;
  printTime(0);

  configTime(timezone * 3600, 0, NTP_SERVERS);

  delay(500);
  while (!time(nullptr)) 
  {
    Serial.print("#");
    delay(1000);
  }

    Serial.print("\nUpdated time from NTP Server: ");
    printTime(0);
    Serial.print("Next NTP Update: ");
    printTime(NTP_Ticks);
}


void printTime(time_t offset)
{
  char buf[30];
  char *dstAbbrev;
  time_t t = dstAdjusted.time(&dstAbbrev)+offset;
  struct tm *timeinfo = localtime (&t);
  
  int hour = (timeinfo->tm_hour+11)%12+1;  // take care of noon and midnight
  sprintf(buf, "%02d/%02d/%04d %02d:%02d:%02d%s %s\n",timeinfo->tm_mon+1, timeinfo->tm_mday, timeinfo->tm_year+1900, hour, timeinfo->tm_min, timeinfo->tm_sec, timeinfo->tm_hour>=12?"pm":"am", dstAbbrev);
  Serial.print(buf);
}


void tempHumiditySensorUpdate()
{
  humidity = dht.getHumidity();
  temperature = dht.toFahrenheit(dht.getTemperature());
  readyforTempHumidityUpdate = false;  
}


void setup() 
{
  pinMode(WIFI_LED, OUTPUT);
  digitalWrite(WIFI_LED, LIGHT_OFF);
  pinMode(ERROR_LED, OUTPUT);
  digitalWrite(ERROR_LED, LIGHT_ON);
  pinMode(RESET_PIN, INPUT);
  digitalWrite(RESET_PIN, HIGH);

  Serial.begin(BAUDRATE);

  WiFiManager wifiManager;            //Local intialization. Once its business is done, there is no need to keep it around

  if(digitalRead(RESET_PIN) == LOW)
  {
    wifiManager.resetSettings();      //reset saved settings
  }

  #ifdef CLEAR_SSID_ON_EVERY_BOOT
  wifiManager.resetSettings();      //reset saved settings
  #endif

  #ifdef STATIC_IP_NETWORK
  wifiManager.setSTAStaticIPConfig(_ip, _gw, _sn);
  #endif

  uniquieDeviceName = mdnsName;
  byte mac[6];
  WiFi.macAddress(mac);
  char temp[4];
  sprintf(temp, "%02X%02X", mac[4], mac[5]);
  uniquieDeviceName += temp;
  int n = uniquieDeviceName.length();
  char DeviceName[n+1];
  strcpy(DeviceName, uniquieDeviceName.c_str()); 
  
  wifiManager.autoConnect(DeviceName);

  Serial.println();
  if (MDNS.begin(DeviceName)) 
  {
    Serial.println("MDNS responder started");
  }

  Serial.println("");
  Serial.print("Connected to ");
  Serial.println(WiFi.SSID());
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  server.on("/", handleSite);
  server.on("/json", jsonSite);
  httpUpdater.setup(&server);
  server.begin();

  // Hostname defaults to esp8266-[ChipID]
  ArduinoOTA.setHostname(DeviceName);

  // No authentication by default
  //ArduinoOTA.setPassword((const char *)"123");

  ArduinoOTA.onStart([]() {
    Serial.println("Start");
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\nEnd");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR) Serial.println("End Failed");
  });
  
  ArduinoOTA.begin();

  Serial.print("Version: ");
  Serial.print(VERSION_MAJOR);
  Serial.print(".");
  Serial.print(VERSION_MINOR);
  Serial.print(".");
  Serial.println(VERSION_BUILD);
  Serial.println();

  delay(500);
  updateNTP(); // Init the NTP time
  char *dstAbbrev;
  time_t t = dstAdjusted.time(&dstAbbrev);
  struct tm *timeinfo = localtime (&t);
  int x = 1;
   while ((timeinfo->tm_year+1900) < 2017)
  {
    updateNTP(); // Init the NTP time
    t = dstAdjusted.time(&dstAbbrev);
    tm *timeinfo = localtime (&t);
    Serial.print("TimeZone Update Failed trying again! Trial Number: ");
    Serial.println(x);
    x++;
    if(x>10)
    {
      Serial.println("Unable to get NTP TIME! Will try again later.");
      break;
    }
    delay(1000);
  }
  
  printTime(0); // print initial time time now.

  NTP_Ticks = NTP_UPDATE_INTERVAL_SEC; // Init the NTP update countdown ticker
  NTP_Ticker_Obj.attach(1, secTicker_NTP); // Run a 1 second interval Ticker
  Serial.print("Next NTP Update: ");
  printTime(NTP_Ticks);

  sensor_Ticks = TEMP_HUMIDITY_INTERVAL_SEC;
  Sensor_Ticker_Obj.attach(1, secTicker_TempHumidity); // Run a 1 second interval Ticker
  
  dht.setup(DHT22_SENSOR, DHTesp::DHT22); // Connect DHT sensor DHT22_SENSOR pin

  tempHumiditySensorUpdate();

  digitalWrite(ERROR_LED, LIGHT_OFF);
  digitalWrite(WIFI_LED, LIGHT_ON);
} // end of void setup()


void loop() 
{
  server.handleClient();
  ArduinoOTA.handle();       // handler for OTA updates of the code.

  if(readyforTempHumidityUpdate)
  {
    tempHumiditySensorUpdate();
  }
  
  if(readyForNtpUpdate)
   {
    updateNTP();
  }
  
} // end of void loop()





















