#include <U8g2lib.h>
#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <OneWire.h>
#include <DallasTemperature.h>

const char* mqtt_server = "192.168.15.15";  // Your MQTT Broker IP address
#include "arduino_secrets.h"   // "" means to look for the file in current dir
// wifi info here, or get from arduino_secrets.h
const char* ssid     = SECRET_SSID; // Your ssid
const char* password = SECRET_PASS; // Your Password
int ip2; int ip3;

WiFiClient espClient;
PubSubClient client(espClient);
long lastMsg = 0;
char msg[50];
int value = 0;

// LCD screen
U8G2_SSD1306_128X64_NONAME_F_SW_I2C u8g2(U8G2_R0, /* clock=*/ 14, /* data=*/ 12, /* reset=*/ U8X8_PIN_NONE);

#define ONE_WIRE_BUS 2  // DS One wire Data wire is plugged into port 2, GPIO2, pin G4 on ESP8266 board
//#define SENSOR  0      // flowmeter signal wire on GPIO0, pin D3/G3 on ESP8266 board
#define SENSOR  5      // board#1 gpio0 bad, use GPIO5, pin D1 on ESP8266 board
// LED Pin
const int ledPin = 4;

long now = 0;
long tmpMillis = 0;
long pMillisFlow = 0;
long deltaMillisFlow = 0;
int intervalFlow = 1000;
long pMillisTemp = 0;
int intervalTemp = 3500;
long pMillisMQTT = 0;
int intervalMQTT = 10000;
long deltaMillisEPwr = 0;
long pMillisEPwr = 0;

float calibrationFactor = 5;
float calOffset = 3;
float minFlowLPM = 2.0;  // minimum flow to calculate COP (filter parasitic main circ flow)
volatile byte pulseCount;
byte pulse1Sec = 0;
float flowLPM;  // water flow rate L/min
float QkW;    // heat flow rate kW
float EQWh;   // heat Wh calc, Wh
float sEQkWh = 63.6;  // since 1/6 to 1/8 am, 2.97COP hydronic
float sEkWh = 21.4;  
float PkW;    // electrical power kW, from best of grid/OG source
float EWh;    // daily total Wh electrical
float gridW;  // latest ASHP grid power (watts) from gridvue
float ogW;    // latest ASHP off-grid power (watts) from emporia cloud
int ePwrSource; // 0=none, 1=grid, 2=off-grid
float ogKWh;       // latest cumulative off-grid kWh today from emporia
float prevOgKWh;   // previous off-grid kWh reading (for delta calc)
bool ogKWhValid;   // have we received at least one kWh reading?
float ogDeltaWh;   // pending off-grid energy delta (Wh), consumed once
long pMillisOgEnergy = 0;  // timestamp of last energy_today message
float COP;    // instantaneous coefficient of performance
float cCOP;   // integrated cycle COP
float sCOP;   // integrated COP (since last reset)
float EWT;    // water coil entering water temp dF
float LWT;    // water coil leaving water temp dF
float LIQ;    // refrigerant liquid line temp dF
float VAP;    // refrigerant vapor line temp dF
float deltaT;  // LWT-EWT 
float RET_Bd;    // water return from bedroom loop temp dF
float RET_HW;   // indirect DHW return
float RET_Bsmt;  //  basement loop
float RET_N;    // North loop
float RET_S;    // South loop
float LAT;   // Air Handler leaving air temp
float EAT;   // Air Handler entering air temp
float tmp;   // temp variable
float tmp2;   // temp variable


// Setup a oneWire instance to communicate with any OneWire devices
OneWire oneWire(ONE_WIRE_BUS);

// Pass our oneWire reference to Dallas Temperature.
DallasTemperature sensors(&oneWire);

// Addresses of DS18B20s
uint8_t ds1[8] = { 0x28, 0x75, 0xE1, 0x81, 0xE3, 0x58, 0x3C, 0x13 };  //LWT  1m cable
uint8_t ds2[8] = { 0x28, 0x4F, 0x71, 0x81, 0xE3, 0x64, 0x3C, 0xE6 };  //EWT  1m cable
//uint8_t ds3[8] = { 0x28, 0x48, 0xA1, 0x81, 0xE3, 0x87, 0x3C, 0x1D };  //Vapor 1m cable - failed
uint8_t ds3[8] = { 0x28, 0x9B, 0x5B, 0x96, 0xF0, 0x01, 0x3C, 0x0E };  // new ref vapor 031624
uint8_t ds4[8] = { 0x28, 0xA9, 0xE8, 0x81, 0xE3, 0x12, 0x3C, 0x25 };  //Liquid 1m cable
uint8_t ds5[8] = { 0x28, 0x1C, 0x79, 0x81, 0xE3, 0x48, 0x3C, 0x23 };  //Bdrm Return 
uint8_t ds6[8] = { 0x28, 0x66, 0x7C, 0x96, 0xF0, 0x01, 0x3C, 0x9F };  //DHW Return
uint8_t ds7[8] = { 0x28, 0xD6, 0x42, 0x96, 0xF0, 0x01, 0x3C, 0xBE };  //Bsmt Return
uint8_t ds8[8] = { 0x28, 0x96, 0x92, 0x96, 0xF0, 0x01, 0x3C, 0x95 };  //North Return (Bath/LivRm/SpBed)
uint8_t ds9[8] = { 0x28, 0xB1, 0xD1, 0x96, 0xF0, 0x01, 0x3C, 0x01 };  //South Return (FamRm/Kit/Laundry)
uint8_t ds10[8] = { 0x28, 0x61, 0x2D, 0x96, 0xF0, 0x01, 0x3C, 0x42 }; // LAT (AH leaving air temp)
uint8_t ds11[8] = { 0x28, 0xA3, 0x25, 0x96, 0xF0, 0x01, 0x3C, 0x8D }; // EAT (AH entering air temp)
uint8_t ds12[8] = { 0x28, 0x4F, 0x60, 0x96, 0xF0, 0x01, 0x3C, 0xE2 };  // spare ref vapor 031624

void IRAM_ATTR pulseCounter()
{
  pulseCount++;
}

void setup(void)
{
  pinMode(SENSOR, INPUT);
  attachInterrupt(digitalPinToInterrupt(SENSOR), pulseCounter, FALLING);
  pinMode(ledPin, OUTPUT);

  pulseCount = 0;
  flowLPM = 0.0;
  pMillisFlow = 0;
  pMillisTemp = 0;

  Serial.begin(115200);
  delay(10);
  u8g2.begin();   // start screen
  delay(10);

// Connect to WiFi network
  int xScrLoc = 0;
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_7x14B_tr);
  u8g2.drawStr(0,10,"Connect ..15.43");
  u8g2.drawStr(10,28,ssid);
  u8g2.drawStr(0,62,"v080421");
  u8g2.sendBuffer();
  WiFi.hostname("ESP8266_HeatMeter");
  Serial.println();
  Serial.println();
  WiFi.mode(WIFI_STA);
  Serial.print("MAC: ");
  Serial.println(WiFi.macAddress());

  // Static IP configuration
  IPAddress staticIP(192, 168, 15, 43);
  IPAddress gateway(192, 168, 15, 1);
  IPAddress subnet(255, 255, 255, 0);
  IPAddress dns(192, 168, 15, 1);
  WiFi.config(staticIP, gateway, subnet, dns);

  Serial.print("Connecting to ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(50);
    Serial.print(".");
    u8g2.drawStr(xScrLoc,46,".");
    u8g2.sendBuffer();
    xScrLoc +=3;
    delay(50);
    Serial.print(".");
    u8g2.drawStr(xScrLoc,43,".");
    u8g2.sendBuffer();
    xScrLoc +=3;
    if (xScrLoc>3000){     // ~50sec
      Serial.println("Connection Failed! Rebooting...");
      delay(5000);
      ESP.restart();
      }
  }
  Serial.println("");
  Serial.println("WiFi connected");
  u8g2.drawStr(0,46,"     connected!!");
  u8g2.sendBuffer();
  
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
  ArduinoOTA.setHostname("ESP8266_HeatMeter");
  ArduinoOTA.setPassword(OTA_PASS);   // vault: Device/cop_heatmeter_ota
  ArduinoOTA.begin();
  Serial.println("Ready");
  Serial.print("IP address: ");
  u8g2.drawStr(0,46,"     connected!!");
  u8g2.sendBuffer();
    
  IPAddress ip; 
  ip = WiFi.localIP();
  Serial.println(ip);
  ip2 = ip[2];  ip3 = ip[3];
  client.setServer(mqtt_server, 1883);
  client.setCallback(mqttCallback);
  
  u8g2.setCursor(55,64);  u8g2.print("..");  
  u8g2.setCursor(65,64);  u8g2.print(ip2);
  u8g2.setCursor(80,64);  u8g2.print(".");  
  u8g2.setCursor(90,64);  u8g2.print(ip3);
  u8g2.sendBuffer();
  delay(3000);

  u8g2.clearBuffer();

  sensors.begin();

  u8g2.setCursor(0,10);   u8g2.print("F");  // labels on the screen
  u8g2.setCursor(45,10);  u8g2.print("kW");
  u8g2.setCursor(0,26);   u8g2.print("EWT"); 
  u8g2.setCursor(45,26);  u8g2.print("LWT");
  u8g2.setCursor(90,26);  u8g2.print("RtBd");
  u8g2.setCursor(0,50);   u8g2.print("VAP");
  u8g2.setCursor(45,50);  u8g2.print("LIQ");
  u8g2.setCursor(90,50);  u8g2.print("COP");
}                                               // end setup()

void loop(void)
{
    ArduinoOTA.handle();

    if (!client.connected()) {   // mqtt
      reconnect(); 
    }
    client.loop();

  now = millis();
  if (pulseCount>0){         //   flow meter is active, some flow
    if (now - pMillisFlow > intervalFlow){
      pulse1Sec = pulseCount;
      pulseCount = 0;
      tmpMillis = millis();
      deltaMillisFlow = tmpMillis - pMillisFlow;   //  now minus previous millis
      pMillisFlow = tmpMillis;
      flowLPM = (( 1000.0 / deltaMillisFlow * pulse1Sec)+calOffset) / calibrationFactor;
      
//      Serial.print("Flow rate: ");   // Print flow rate in litres / minute
//      Serial.print(float(flowLPM));  
//      Serial.print("L/min");
//      Serial.print("\t");       // Print tab space
      u8g2.setCursor(17,10);
      u8g2.print(flowLPM,1);
      if (deltaT>0) { QkW = .0385 * flowLPM * deltaT; }
      else { QkW = 0; }
      EQWh += QkW*deltaMillisFlow/3600;
      if (PkW>0 && flowLPM>minFlowLPM){
        COP = QkW/PkW;
        if (COP>10) {COP=10; }
      }
      u8g2.setCursor(64,10);
      u8g2.print(QkW,2);
      u8g2.sendBuffer(); 
    }
  }
  else if (VAP>RET_HW+5){  // if Vapor temp > Ret_DHW, then it's running in airflow heating, if no water flow
    
    }
  else {     //  no flowmeter pulses, no flow, no airflow
    flowLPM = 0;
    if (EQWh > 100) {    //  if heatpump has been on and now is not, ie: end of a HP run cycle
      sEQkWh += EQWh/1000;    // update running total energy kWh
      sEkWh += EWh/1000;
      EQWh = 0;   // done with these now, set to zero
      EWh = 0;
      cCOP = 0;     
      COP = 0;      
      if ((sEQkWh>0)&&(sEkWh>0)){ sCOP = sEQkWh/sEkWh; }   // seasonal COP

      char sCOP_Str[8];                 //    put these out to MQTT only at end of a HP run cycle
      dtostrf(sCOP, 1, 2, sCOP_Str);          //  convert to char array
      client.publish("HPEM/sCOP", sCOP_Str);   // pub to MQTT

      char sEkWh_Str[8];                 
      dtostrf(sEkWh, 1, 1, sEkWh_Str);          //  convert to char array
      client.publish("HPEM/elec_sEkWh", sEkWh_Str);   // pub to MQTT

      char sEQkWh_Str[8];                 
      dtostrf(sEQkWh, 1, 1, sEQkWh_Str);          //  convert to char array
      client.publish("HPEM/heat_sEQkWh", sEQkWh_Str);   // pub to MQTT

      char LPM_Str[8];                   // output one last time, for the zero
      dtostrf(flowLPM, 1, 2, LPM_Str);          //  convert to char array
      client.publish("HPEM/flowLPM", LPM_Str);   // pub to MQTT

      char COP_Str[8];                 
      dtostrf(COP, 1, 2, COP_Str);          //  convert to char array
      client.publish("HPEM/COP", COP_Str);   // pub to MQTT

      char cCOP_Str[8];                 
      dtostrf(cCOP, 1, 2, cCOP_Str);          //  convert to char array
      client.publish("HPEM/cCOP", cCOP_Str);   // pub to MQTT
    }
    u8g2.setCursor(17,10);
    u8g2.print(flowLPM,1);
  }

  if (now - pMillisTemp > intervalTemp){   //  flowmeter temperatures

  sensors.requestTemperatures();  // all sensors do a conversion please, ready to read

//  Serial.print("EWT: ");       
  tmp = printTemperature(ds1);   
  if ((tmp>0)) {EWT=tmp;}
  u8g2.setCursor(0,38);
  u8g2.print(EWT);

//  Serial.print("LWT: ");
  tmp = printTemperature(ds2);
  if ((tmp>0)) {LWT=tmp+1;}   //  looks like 1dF low, compared to EWT,Vap,Liq sensors
  u8g2.setCursor(45,38);
  u8g2.print(LWT);
  u8g2.sendBuffer();

  deltaT = LWT-EWT;
  if (deltaT>0) { QkW = .0385 * flowLPM * deltaT; }
  else { QkW = 0; }
//  Serial.print("QkW: ");
//  Serial.println(QkW);
  if (PkW>0 && flowLPM>minFlowLPM){
    COP = QkW/PkW;
    if (COP>10) {COP=10; }
  }
  
  u8g2.setCursor(64,10);
  u8g2.print(QkW,2);
  u8g2.setCursor(101,10);
  u8g2.print(PkW,2);
  u8g2.setCursor(90,62);
  u8g2.print(COP,2);
  u8g2.sendBuffer();

  pMillisTemp = millis();
  }                                                 // end flow temperatures

//  output to MQTT,  temperatures constantly but slower unless running, COP variables only when running
  if (now - pMillisMQTT > (intervalMQTT*(1+17*((VAP<RET_HW+5)&&(flowLPM==0))))){  //10s if flow, 3min if no flow
    u8g2.clearBuffer();
    u8g2.setCursor(0,10);   u8g2.print("F");  // labels on the screen
    u8g2.setCursor(45,10);  u8g2.print("kW");
    u8g2.setCursor(0,26);   u8g2.print("EWT"); 
    u8g2.setCursor(45,26);  u8g2.print("LWT");
    u8g2.setCursor(90,26);  u8g2.print("RtBd");
    u8g2.setCursor(0,50);   u8g2.print("VAP");
    u8g2.setCursor(45,50);  u8g2.print("LIQ");
    u8g2.setCursor(90,50);  u8g2.print("COP");
    
  if (flowLPM>0){  // don't need all these zeros published when it's not running
      char COP_Str[8];                 
      dtostrf(COP, 1, 2, COP_Str);          //  convert to char array
      client.publish("HPEM/COP", COP_Str);   // pub to MQTT
      
      if (EWh>0){ cCOP = EQWh/EWh;}    // calc cCOP here (less often), vs with flow or temp loop
      char cCOP_Str[8];                 
      dtostrf(cCOP, 1, 2, cCOP_Str);          //  convert to char array
      client.publish("HPEM/cCOP", cCOP_Str);   // pub to MQTT
    
      char QkW_Str[8];                 
      dtostrf(QkW, 1, 2, QkW_Str);          //  convert to char array
      client.publish("HPEM/heatQkW", QkW_Str);   // pub to MQTT

      char EQWh_Str[8];                 
      dtostrf(EQWh, 1, 1, EQWh_Str);          //  convert to char array
      client.publish("HPEM/heatEWh", EQWh_Str);   // pub to MQTT
  
      char LPM_Str[8];                 
      dtostrf(flowLPM, 1, 2, LPM_Str);          //  convert to char array
      client.publish("HPEM/flowLPM", LPM_Str);   // pub to MQTT
    }
   
    char PkW_Str[8];                 // put elec power to MQTT all the time
    dtostrf(PkW, 1, 2, PkW_Str);          //  convert to char array
    client.publish("HPEM/elecPkW", PkW_Str);   // pub to MQTT

    char EWh_Str[8];
    dtostrf(EWh, 1, 1, EWh_Str);          //  convert to char array
    client.publish("HPEM/elecEWh", EWh_Str);   // pub to MQTT

    // publish power source: 0=none, 1=grid, 2=off-grid
    char src_Str[4];
    itoa(ePwrSource, src_Str, 10);
    client.publish("HPEM/ePwrSrc", src_Str);   // debug: which ASHP power source
    

//  Serial.print("Vapor: ");       //  other temps, read less often, do with MQTT
  tmp = printTemperature(ds3);
  tmp2 = printTemperature(ds12);  // 2nd sensor
  if (tmp>-40) {VAP=tmp;}
    else if (tmp2>-40) {VAP=tmp2;}
      else  
      {
      VAP=11.1;     // error value
      }
  u8g2.setCursor(0,62);
  u8g2.print(VAP);
  
//  Serial.print("Liquid: ");
  tmp = printTemperature(ds4);
  if (tmp>-40) {LIQ=tmp;}
  u8g2.setCursor(45,62);
  u8g2.print(LIQ);
  u8g2.sendBuffer();
  
//  Serial.print("RET_Bd: ");
  tmp = printTemperature(ds5);
  if ((tmp>0)) {RET_Bd=tmp;}
  u8g2.setCursor(90,38);
  u8g2.print(RET_Bd);
  u8g2.sendBuffer();

//  Serial.print("RET_HW: ");
  tmp = printTemperature(ds6);
  if ((tmp>0)) {RET_HW=tmp;}
  
//  Serial.print("RET_Bsmt: ");
  tmp = printTemperature(ds7);
  if ((tmp>0)) {RET_Bsmt=tmp;}
  
//  Serial.print("RET_N: ");
  tmp = printTemperature(ds8);
  if ((tmp>0)) {RET_N=tmp;}
  
//  Serial.print("RET_S: ");
  tmp = printTemperature(ds9);
  if ((tmp>0)) {RET_S=tmp;}

//  Serial.print("LAT: ");
  tmp = printTemperature(ds10);
  if ((tmp>0)) {LAT=tmp-.3;}   // ~.6 dF delta error between these two sensors, both in 95 dF water bath
  
//  Serial.print("EAT: ");
  tmp = printTemperature(ds11);
  if ((tmp>0)) {EAT=tmp+.3;}   // ~.6 dF delta error between these two sensors, both in 95 dF water bath
  
// Serial.println();          // end read other temperatures


    char EWT_Str[8];                 
    dtostrf(EWT, 1, 2, EWT_Str);          //  convert to char array
    client.publish("HPEM/EWT", EWT_Str);   // pub to MQTT

    char LWT_Str[8];                 
    dtostrf(LWT, 1, 2, LWT_Str);          //  convert to char array
    client.publish("HPEM/LWT", LWT_Str);   // pub to MQTT

    char VAP_Str[8];                 
    dtostrf(VAP, 1, 2, VAP_Str);          //  convert to char array
    client.publish("HPEM/VAP", VAP_Str);   // pub to MQTT

    char LIQ_Str[8];                 
    dtostrf(LIQ, 1, 2, LIQ_Str);          //  convert to char array
    client.publish("HPEM/LIQ", LIQ_Str);   // pub to MQTT

    char deltaT_Str[8];
    dtostrf(deltaT, 1, 2, deltaT_Str);          //  convert to char array
    client.publish("HPEM/deltaT", deltaT_Str);   // pub to MQTT
   
    char RET_Bd_Str[8];                 
    dtostrf(RET_Bd, 1, 2, RET_Bd_Str);          //  convert to char array
    client.publish("HPEM/RET_Bd", RET_Bd_Str);   // pub to MQTT

    char RET_HW_Str[8];                 
    dtostrf(RET_HW, 1, 2, RET_HW_Str);          //  convert to char array
    client.publish("HPEM/RET_HW", RET_HW_Str);   // pub to MQTT

    char RET_Bsmt_Str[8];                 
    dtostrf(RET_Bsmt, 1, 2, RET_Bsmt_Str);          //  convert to char array
    client.publish("HPEM/RET_Bsmt", RET_Bsmt_Str);   // pub to MQTT

    char RET_N_Str[8];                 
    dtostrf(RET_N, 1, 2, RET_N_Str);          //  convert to char array
    client.publish("HPEM/RET_N", RET_N_Str);   // pub to MQTT

    char RET_S_Str[8];                 
    dtostrf(RET_S, 1, 2, RET_S_Str);          //  convert to char array
    client.publish("HPEM/RET_S", RET_S_Str);   // pub to MQTT

    char LAT_Str[8];                 
    dtostrf(LAT, 1, 2, LAT_Str);          //  convert to char array
    client.publish("HPEM/LAT", LAT_Str);   // pub to MQTT

    char EAT_Str[8];                 
    dtostrf(EAT, 1, 2, EAT_Str);          //  convert to char array
    client.publish("HPEM/EAT", EAT_Str);   // pub to MQTT

    pMillisMQTT = millis();
  }     //    end MQTT output  

}    //  end of main    loop()

float printTemperature(DeviceAddress deviceAddress)
{
  float tempC = sensors.getTempC(deviceAddress);
  float tempF = DallasTemperature::toFahrenheit(tempC);
//  Serial.print(tempF);
//  Serial.println("F");
  return tempF;
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  // Check: http://playground.arduino.cc/Code/PrintFloats
  // Also: http://www.varesano.net/blog/fabio/sending-float-variables-over-serial-without-loss-precision-arduino-and-processing
  float f = 0.0;
  int x = 0;
  int index = -1;

//  Serial.print("Message arrived [");
//  Serial.print(topic);
//  Serial.print("] ");
//    for (int i = 0; i < length; i++) {
//    Serial.print((char)payload[i]);
//  }
//  Serial.println();
 
  if (String(topic) == "gridvue/power/sensor/ashpgrid/state") {
    gridW = parse_payload2float(payload, length);
  }
  else if (String(topic) == "emporia/ashp_og/power") {
    ogW = parse_payload2float(payload, length);
  }
  else if (String(topic) == "emporia/ashp_og/energy_today") {
    prevOgKWh = ogKWh;
    ogKWh = parse_payload2float(payload, length);
    if (!ogKWhValid) { prevOgKWh = ogKWh; ogKWhValid = true; }  // first reading, no delta yet
    ogDeltaWh = (ogKWh - prevOgKWh) * 1000.0;  // compute delta here, consume once below
    if (ogDeltaWh > 500.0) { ogDeltaWh = 0; }  // reject HA total_increasing rebound after daily reset
  }

  // Use the larger source (only one can be active at a time)
  float bestW = (gridW >= ogW) ? gridW : ogW;
  ePwrSource = (gridW >= ogW) ? 1 : 2;
  if (bestW < 5.0) { bestW = 0; ePwrSource = 0; }

  tmpMillis = millis();
  deltaMillisEPwr = tmpMillis - pMillisEPwr;
  pMillisEPwr = tmpMillis;
  PkW = bestW / 1000.0;

  if (ePwrSource == 2 && ogKWhValid) {
    // Off-grid: consume pending energy delta (computed in energy_today handler)
    if (ogDeltaWh > 0) {
      long nowOg = millis();
      long dtOg = nowOg - pMillisOgEnergy;
      pMillisOgEnergy = nowOg;
      // Cross-check derivedPkW against 1-min averaged power (ogW). Emporia cloud
      // occasionally reports an oversized cumulative jump in energy_today that
      // self-corrects next poll (seen 2026-04-18 14:08: +205 Wh in 60 s while
      // ogW held at ~1960 W, yielding a bogus 12.3 kW "spike"). If derivedPkW
      // disagrees with ogW by >2x, reconstruct the delta from ogW × dtOg.
      bool glitch = false;
      float derivedPkW = 0;
      if (dtOg > 1000) {
        derivedPkW = ogDeltaWh / (dtOg / 3600000.0) / 1000.0;
        if (ogW > 100.0 && derivedPkW > 2.0 * (ogW / 1000.0)) {
          ogDeltaWh = ogW * (dtOg / 3600000.0);  // substitute plausible delta
          glitch = true;
        } else if (derivedPkW > 0.01 && derivedPkW < 20.0) {
          PkW = derivedPkW;  // sanity cap 20kW
        }
      }
      EWh += ogDeltaWh;
      char dbg[96];
      snprintf(dbg, sizeof(dbg), "OG dWh=%.2f EWh=%.1f dtOg=%ld dPkW=%.2f ogW=%.0f%s",
               ogDeltaWh, EWh, dtOg, derivedPkW, ogW, glitch ? " GLITCH" : "");
      client.publish("HPEM/debug", dbg);
      ogDeltaWh = 0;  // consume once
    }
  }
  else if (ePwrSource == 1) {
    // On-grid: integrate power (gridvue 5s updates are fine)
    float addWh = PkW * deltaMillisEPwr / 3600;
    EWh += addWh;
    // debug: publish on-grid integration
    char dbg[64];
    snprintf(dbg, sizeof(dbg), "GR add=%.4f PkW=%.4f dt=%ld EWh=%.1f", addWh, PkW, deltaMillisEPwr, EWh);
    client.publish("HPEM/debug", dbg);
  }
}

float parse_payload2float(byte* payload, unsigned int length){
  // null-terminate and use strtof — safe for any length decimal
  char buf[32];
  unsigned int n = (length < sizeof(buf)-1) ? length : sizeof(buf)-1;
  memcpy(buf, payload, n);
  buf[n] = '\0';
  return strtof(buf, NULL);
}  

void reconnect() {
  // Loop until we're reconnected
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    // Attempt to connect
    // Create a random client ID
    String clientId = "ESP8266Client-";
    clientId += String(random(0xffff), HEX);
    // Attempt to connect
    if (client.connect(clientId.c_str(), MQTT_USER, MQTT_PASS)) {
      Serial.println("connected");
      client.subscribe("gridvue/power/sensor/ashpgrid/state");
      client.subscribe("emporia/ashp_og/power");
      client.subscribe("emporia/ashp_og/energy_today");
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}
