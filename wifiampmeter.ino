// Wifi Ammeter

#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include <ESP8266mDNS.h>
#include <ArduinoOTA.h>
#include <Wire.h>
#include <INA219.h>
#include <U8g2lib.h>
#include <ESP8266WebServer.h>     //Local WebServer used to serve the configuration portal
#include <WiFiManager.h> //https://github.com/tzapu/WiFiManager WiFi Configuration Magic

INA219 ina219;
#define SHUNT_MAX_V .075  /* Rated max for our shunt is 75mv for 100 A current: 
                             we will mesure only up to 20A so max is about 75mV*20/50 lets put some more*/
#define BUS_MAX_V   16.0  /* with 12v lead acid battery this should be enough*/
#define MAX_CURRENT 20    /* In our case this is enaugh even shunt is capable to 50 A*/
#define SHUNT_R  0.000697 /*Shunt resistor in ohm */
U8G2_SSD1306_128X32_UNIVISION_F_HW_I2C u8g2(U8G2_R0);

boolean connected = false;
// current and voltage readings
float shuntvoltage = 0;
float busvoltage = 0;
float current_A = 0;
float batvoltage = 0;
float power = 0;
float Ah = 0;
unsigned long lastread = 0; // used to calculate Ah
unsigned long tick;         // current read time - last read

// different intervals for each Task
int intervalReadData = 50;
int intervalDisplay = 1000;

// last taks call
unsigned long previousMillisReadData = 0;
unsigned long previousMillisDisplay = 0;

WiFiUDP udp;        // A UDP instance to let us send and receive packets over UDP
void WiFiEvent(WiFiEvent_t event);


void setup() {
  Serial.begin(115200);
  u8g2.begin();
  WiFiManager wm;
  bool res;
  res = wm.autoConnect(); // auto generated AP name from chipid
  if (!res) {
    Serial.println("Failed to connect");
    ESP.restart();
  }
  else {
    //if you get here you have connected to the WiFi
    Serial.println("connected...yeey :)");
  }
  ArduinoOTA.setHostname("ampmeter");
  
  u8g2.clearBuffer();         // clear the internal menory
  u8g2.setFont(u8g2_font_helvB12_tf);  // choose a suitable font
  u8g2.setCursor (0, 32);
  u8g2.print("Connecting v2");
  u8g2.setCursor (0, 16);

  u8g2.sendBuffer();

  ArduinoOTA.onStart([]() {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH) {
      type = "sketch";
    } else { // U_FS
      type = "filesystem";
    }

    // NOTE: if updating FS this would be the place to unmount FS using FS.end()
    Serial.println("Start updating " + type);
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\nEnd");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) {
      Serial.println("Auth Failed");
    } else if (error == OTA_BEGIN_ERROR) {
      Serial.println("Begin Failed");
    } else if (error == OTA_CONNECT_ERROR) {
      Serial.println("Connect Failed");
    } else if (error == OTA_RECEIVE_ERROR) {
      Serial.println("Receive Failed");
    } else if (error == OTA_END_ERROR) {
      Serial.println("End Failed");
    }
  });
  ArduinoOTA.begin();
  Serial.println("");
  Serial.println("WiFi connected");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
  ina219.begin();
  // setting up our configuration
  // default values are RANGE_32V, GAIN_8_320MV, ADC_12BIT, ADC_12BIT, CONT_SH_BUS
  ina219.configure(INA219::RANGE_16V, INA219::GAIN_1_40MV, INA219::ADC_128SAMP, INA219::ADC_128SAMP, INA219::CONT_SH_BUS);
  // calibrate with our values
  ina219.calibrate(SHUNT_R, SHUNT_MAX_V, BUS_MAX_V, MAX_CURRENT);
  
  delay(1000);
}

void loop() {
  ArduinoOTA.handle();
  // get current time stamp
  // only need one for both if-statements
  unsigned long currentMillis = millis();

  if ((unsigned long)(currentMillis - previousMillisReadData) >= intervalReadData) {
    previousMillisReadData = millis();
    readCurrent();
    }


  if ((unsigned long)(currentMillis - previousMillisDisplay) >= intervalDisplay) {
    previousMillisDisplay = millis();
    // displays data

    u8g2.clearBuffer();         // clear the internal menory
    u8g2.setFont(u8g2_font_helvB12_tf);  // choose a suitable font
    u8g2.setCursor (0, 32);
    u8g2.print(current_A);
    u8g2.print("A ");
    u8g2.print(Ah);
    u8g2.print("Ah");
    u8g2.setCursor (0, 16);
    u8g2.print(batvoltage);
    u8g2.print("V ");
    u8g2.print(power);
    u8g2.print("W ");
    u8g2.sendBuffer();
    sendSK();
  }
 
}

void readCurrent() {
  uint32_t count = 0;
  unsigned long newtime;

  
  // reads busVoltage
  busvoltage = ina219.busVoltage();
  // waits for conversion ready
  while (!ina219.ready() && count < 500) {
    count++;
    delay(1);
    busvoltage = ina219.busVoltage();
  }

  //  Serial.print("Count:   "); Serial.println(count);

  // read the other values
  shuntvoltage = ina219.shuntVoltage() * 1000;
  current_A = ina219.shuntCurrent() * -8.076923077;
  batvoltage = busvoltage + (shuntvoltage / 1000);
  power = ina219.busPower() * 8.076923077;
  newtime = millis();
  tick = newtime - lastread;
  Ah += (current_A * tick) / 3600000.0;
  lastread = newtime;

  // prepare for next read -- this is security just in case the ina219 is reset by transient curent
  ina219.recalibrate();
  ina219.reconfig();
}

void sendSK() {
  
  if ((WiFi.status() == WL_CONNECTED)) { //Check the current connection status
    //Serial.println("wificonnected");
    char buf3 [512];
    const char * udpAddress = "10.10.10.1";
    //Serial.println(current_A);
    snprintf(buf3, sizeof buf3, "{\"updates\":[{ \"$source\": \"OPwifi.Battery\",\"values\":[{\"path\": \"electrical.batteries.house.current\",\"value\":%8.3f },{\"path\": \"electrical.batteries.house.voltage\",\"value\":%8.2f },{\"path\": \"electrical.batteries.house.AmpH\",\"value\":%8.2f }]}]}\n", current_A, batvoltage, Ah);
    //Serial.println("madeitthis far");
    udp.beginPacket(udpAddress, 55561);
    udp.printf(buf3);
    //Serial.println(buf3);
    udp.endPacket();
    //    Serial.println("Connected to server successful!");

  } else {
    Serial.println("Error not connected");
  }

}
