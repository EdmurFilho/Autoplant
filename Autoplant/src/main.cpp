#include <DHT.h> 
#include <WiFi.h>              
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include "image.h"  

const int Soil_Dry = 2600;  
const int Soil_Humid = 1350;

// Pinos TFT
#define TFT_DC 26
#define TFT_CS 32
#define TFT_RST 25

Adafruit_ILI9341 tft = Adafruit_ILI9341(TFT_CS, TFT_DC, TFT_RST);

const char* WIFI_SSID  = "Network name (SSID)";
const char* WIFI_PASSWORD = "Password";

const char* FIREBASE_HOST = "firebase database URL";
String databaseURL = "https://" + String(FIREBASE_HOST);

#define DHTPIN 5
#define DHTTYPE DHT22
DHT dht(DHTPIN, DHTTYPE);

#define sensor_soil 36
#define bomba 27
#define potenci_r 34
#define potenci_v 35 
#define FloatSensor 14
#define button 13

bool Water = 0;
String WaterS;
float soil = 0.00;    
float pastsoil = 0.00; 
int soilBruto = 0;    
float Temp = 0;
int cycles = 3;
int Vol = 1;
float Hum; 

int cycR = 0, volR = 0;
int cyc = 0, vol = 0;
String ScycFB = "", SvolFB = "";
int cycFB = 0, volFB = 0;
int pastCyc = 0, pastVol = 0, pastCycFB = 0, pastVolFB = 0;
int RegS = 0, VolS = 0;

unsigned long LastWatering = 0;
unsigned long newTime;
unsigned long interval;
int wateringDurantion;
long trigger, last_trigger;
int rep;

unsigned long lastChange= 0;
const long debounceTime = 500; 
bool pendingChange = false;

unsigned long lastDysplayChange = 0;
const long displayInterval = 100;

WiFiClientSecure client;
HTTPClient https;


void update();
void warning();
void regar();
void WateringParameters();
void ReVsend();
void TFTbackground(const uint16_t *bmpData, uint16_t vWidth, uint16_t vHeight);
void TFTdata();
void Soil_value(); 

void setup() {
  pinMode(bomba, OUTPUT);         
  pinMode(button, INPUT_PULLUP);  
  pinMode(4,OUTPUT);
  pinMode(sensor_soil, INPUT);
  pinMode(potenci_r, INPUT);
  pinMode(potenci_v, INPUT);
  digitalWrite(bomba, LOW);

  Serial.begin(115200);
  Serial.println("\n--- SETUP Begin ---");

  dht.begin();
  
  cycR = analogRead(potenci_r);
  volR = analogRead(potenci_v);
  cyc = map(cycR, 0, 4095, 1, 10);
  vol = map(volR, 0, 4095, 1, 10);
  
  last_trigger = cyc + vol; 
  RegS = cyc;
  VolS = vol; 

  tft.begin();
  tft.setRotation(0);
  TFTbackground(imageBMP, image_WIDTH, image_HEIGHT);

  Serial.print("MAC adress: ");
  Serial.println(WiFi.macAddress());
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting to Wi-Fi");
  
  while (WiFi.status() != WL_CONNECTED) {
    delay(500); 
    Serial.print(".");
  }
  Serial.println("\nconnected to WiFi successfully!");
  Serial.print("IP adress: ");
  Serial.println(WiFi.localIP());
  client.setInsecure(); 

  Serial.println("Calling WateringParameters()");
  WateringParameters(); 
  LastWatering = millis();
  Serial.println("--- SETUP Done ---");
}

void loop() {
  newTime = millis();
  Temp = dht.readTemperature();
  
  Soil_value();
  
  cycR = analogRead(potenci_r);
  volR = analogRead(potenci_v);
  cyc = map(cycR, 0, 4095, 1, 10);
  vol = map(volR, 0, 4095, 1, 10);

  if (newTime - LastWatering >= interval) {
    LastWatering = newTime; 
    Serial.println("Watering interval reached, begining watering cycle");
    regar();
  }
  
   trigger = cyc + vol;

  if(trigger != last_trigger){ 
    last_trigger = trigger;
    lastChange= newTime;
    pendingChange = true; 
  } 
  
  if (pendingChange && (newTime - lastChange>= debounceTime)) {
      
    if (cyc != RegS || vol != VolS) {
        Serial.printf("Potentiometes changed Sending values to Firebase.\n");
        ReVsend(); 
        RegS = cyc;
        VolS = vol;
        
    }
    pendingChange = false;
  }

  if(digitalRead(button) == LOW){ 
    Serial.println("Manual watering activated");
    regar();
    LastWatering = newTime; 
    delay(200); 
  }

  if (newTime - lastDysplayChange >= displayInterval) {
      lastDysplayChange = newTime;
      TFTdata(); 
  }
}

void Soil_value(){
    soilBruto = analogRead(sensor_soil);
    float soil_map = map(soilBruto, Soil_Dry, Soil_Humid, 0, 100);
    soil = constrain(soil_map, 0.00, 100.00); 
}

void TFTbackground(const uint16_t *bmpData, uint16_t vWidth, uint16_t vHeight){
  tft.fillScreen(ILI9341_BLACK);
  int xOffset = (tft.width() - vWidth)/2;
  int yOffset = (tft.height() - vHeight)/2;
  tft.drawRGBBitmap(xOffset, yOffset, (uint16_t*)bmpData, vWidth, vHeight);
  tft.setTextColor(ILI9341_BLACK);
  tft.setTextSize(1);
}

void TFTdata(){
    int y_start = 75; 
    int y_increment = 32; 
    int x_data = 135; 
    
    tft.setTextColor(ILI9341_BLACK); 
    
    int y_vol = y_start;
    tft.fillRect(x_data, y_vol + 10, 100, 15, ILI9341_WHITE); 
    tft.setTextSize(1); 
    tft.setCursor(x_data, y_vol);
    tft.println("Watering volume:");
    tft.setTextSize(2); 
    tft.setCursor(x_data, y_vol + 10);
    tft.print(VolS * 100); 
    tft.setTextSize(1); 
    tft.println("ml");

    int y_reg = y_vol + y_increment;
    tft.fillRect(x_data, y_reg + 10, 100, 15, ILI9341_WHITE); 
    tft.setTextSize(1); 
    tft.setCursor(x_data, y_reg);
    tft.println("cycles:");
    tft.setTextSize(2); 
    tft.setCursor(x_data, y_reg + 10);
    tft.println(RegS);

    int y_int = y_reg + y_increment;
    tft.fillRect(x_data, y_int + 10, 100, 15, ILI9341_WHITE);
    tft.setTextSize(1); 
    tft.setCursor(x_data, y_int);
    tft.println("interval:");
    tft.setTextSize(2); 
    tft.setCursor(x_data, y_int + 10);
    tft.println(String(interval/3600000.0)); 

    int y_temp = y_int + y_increment;
    tft.fillRect(x_data, y_temp + 10, 100, 15, ILI9341_WHITE);
    tft.setTextSize(1); 
    tft.setCursor(x_data, y_temp);
    tft.print("Temp:");
    tft.setTextSize(2); 
    tft.setCursor(x_data, y_temp + 10);
    tft.println(String(Temp, 1) + "C"); 

    int y_soil = y_temp + y_increment;
    tft.fillRect(x_data, y_soil + 10, 100, 15, ILI9341_WHITE); 
    tft.setTextSize(1); 
    tft.setCursor(x_data, y_soil);
    tft.print("Hum.soil:");
    tft.setTextSize(2); 
    tft.setCursor(x_data, y_soil + 10);
    tft.printf("%.2f%%\n", soil); 
}

void update(){
  Serial.println("\n--- Begining update() ---");
  Temp = dht.readTemperature();
  Soil_value(); 
  
  // RESTAURADO: Atualiza o último valor registrado antes do envio
  pastsoil = soil; 
  
  Serial.printf("Values: Temp=%.2fC, soil=%.2f%%, Water=%d\n", Temp, soil, WaterS);

  // Upload temperature
  String urlPutTemp = databaseURL + "/enviroment1/Temp.json";
  Serial.println("Trying to send the Temperature...");
  if (https.begin(client, urlPutTemp)) {
    https.addHeader("Content-Type", "application/json");            
    int httpCode = https.PUT(String(Temp, 1));   
    if (httpCode > 0) { Serial.printf("PUT [Temp] OK: %d\n", httpCode); } 
    else { Serial.printf("PUT [Temp] FAIL: %s\n", https.errorToString(httpCode).c_str()); }
    https.end();
  }

  // Upload soil humidity
  String urlPutsoil = databaseURL + "/enviroment1/soil.json";
  Serial.println("Trying to send the soil humidity...");
  if (https.begin(client, urlPutsoil)) {
    https.addHeader("Content-Type", "application/json");            
    int httpCode = https.PUT(String(soil, 2));  
    if (httpCode > 0) { Serial.printf("PUT [soil] OK: %d\n", httpCode); } 
    else { Serial.printf("PUT [Soil] FAIL: %s\n", https.errorToString(httpCode).c_str()); }
    https.end();
  }

  Serial.println("--- update() Done ---");
}

void warning(){
  Serial.println("!!! Warning: replenish water !!!!");
  Water = !digitalRead(FloatSensor);
  WaterS = Water ? "true" : "false";

  // Upload water sensor
  String urlPutWater = databaseURL + "/enviroment1/Water.json";
  Serial.println("Trying to send the water level...");
  if (https.begin(client, urlPutWater)) {
    https.addHeader("Content-Type", "application/json");            
    int httpCode = https.PUT(String(WaterS));    
    if (httpCode > 0) { Serial.printf("PUT [Water] OK: %d\n", httpCode); } 
    else { Serial.printf("PUT [Water] FAIL: %s\n", https.errorToString(httpCode).c_str()); }
    https.end();
  }

  rep = 10;
  while (rep > 0){
  digitalWrite(4,1);
  delay(200);
  digitalWrite(4,0);
  delay(200);
  rep --;
  }
}

void regar(){
  Serial.println("\n--- EXECUTANDO regar() ---");
  digitalWrite(4,1);
  delay(400);
  digitalWrite(4,0);
  Water = !digitalRead(FloatSensor);
  if(Water){ 
    Serial.printf("Water level: OK. Turning pump on %d ms.\n", wateringDurantion);
    digitalWrite(bomba,1); 
    delay(wateringDurantion); 
    digitalWrite(bomba,0);
    Serial.println("Watering done.");
    WateringParameters(); 
    update();
  } else {
    Serial.println("Low water level.");
    warning();
    while (!Water){ 
      Serial.println("waiting for water replenish...");
      Water = !digitalRead(FloatSensor);
      delay(500);
    }
    Serial.println("water replenished!");
  }
}

void WateringParameters(){
  Serial.println("\n--- Begining WateringParameters() ---");
  cycR = analogRead(potenci_r);
  volR = analogRead(potenci_v);
  Temp = dht.readTemperature();
  Soil_value(); 
  cyc = map(cycR, 0, 4095, 1, 10);
  vol = map(volR, 0, 4095, 1, 10);

  // GET cycles from Firebase
  String urlGetcycles = databaseURL + "/enviroment1/cycles.json";
  Serial.println("Trying to get 'cycles' from Firebase...");  
  if (https.begin(client, urlGetcycles)) {
    int httpCode = https.GET();
    if (httpCode > 0) {
      Serial.printf("GET [cycles] from %s - reply code: %d\n", urlGetcycles.c_str(), httpCode); 
      if (httpCode == HTTP_CODE_OK) {
        ScycFB = https.getString();
        Serial.println("'cycles' recivied from Firebase: " + ScycFB);
      }
    } else {
      Serial.printf("GET [cycles] failed, error: %s\n", https.errorToString(httpCode).c_str());
    }
    https.end();
  } else {
    Serial.println("FAIL to get cycles.");
  }
    // GET Volume from Firebase
  String urlGetVolume = databaseURL + "/enviroment1/Vol.json";
  Serial.println("Trying to get 'volume' from Firebase...");    
  if (https.begin(client, urlGetVolume)) {
    int httpCode = https.GET();
    if (httpCode > 0) {
      Serial.printf("GET [Vol] from %s - reply code: %d\n", urlGetVolume.c_str(), httpCode); 
      if (httpCode == HTTP_CODE_OK) {
        SvolFB = https.getString();
        Serial.println("'Volume' recivied from Firebase: " + SvolFB);
      }
    } else {
      Serial.printf("GET [Vol] failed, error: %s\n", https.errorToString(httpCode).c_str());
    }
    https.end();
  } else {
    Serial.println("FAIL to get Volume.");
  }
  
  cycFB = ScycFB.toInt();
  volFB = SvolFB.toInt();

  if (vol != pastVol) {
    VolS = vol;
    pastVol = vol;
  } else if (volFB != pastVolFB) {
    VolS = volFB;
    pastVolFB = volFB;
  }
  
  if (cyc != pastCyc) {
    RegS = cyc;
    pastCyc = cyc;
  } else if (cycFB != pastCycFB) {
    RegS = cycFB;
    pastCycFB = cycFB;
  }

  // Final calculations
  wateringDurantion = VolS * 6250; 
  interval = (86400000 / RegS) - wateringDurantion; 
  Serial.printf("Final values - wateringDurantion: %d ms, interval: %lu ms\n", wateringDurantion, interval);
  Serial.println("--- WateringParameters() done ---");
  TFTdata();
}

void ReVsend(){
  Serial.println("\n--- Begining ReVsend() ---");
  if(RegS != cyc){
    RegS = cyc;
    // PUT cycles
    String urlPutcycles = databaseURL + "/enviroment1/cycles.json";
    Serial.printf("trying PUT [cycles] : %d...\n", RegS);
    if (https.begin(client, urlPutcycles)) {
      https.addHeader("Content-Type", "application/json");
      int httpCode = https.PUT(String(RegS));
      if (httpCode > 0) { 
        Serial.printf("PUT [cycles] OK: %d\n", httpCode); 
      }else{ 
        Serial.printf("PUT [cycles] FAIL: %s\n", https.errorToString(httpCode).c_str()); 
      }
        https.end();
      }
    }
    if(VolS != vol){
    VolS = vol;
    // PUT Volume
    String urlPutVolume = databaseURL + "/enviroment1/Vol.json";
    Serial.printf("trying PUT [Vol] : %d...\n", VolS);
    if (https.begin(client, urlPutVolume)) {
        https.addHeader("Content-Type", "application/json");
        int httpCode = https.PUT(String(VolS));
        if (httpCode > 0) { 
          Serial.printf("PUT [Vol] OK: %d\n", httpCode); 
        }else{ 
          Serial.printf("PUT [Vol] FAIL: %s\n", https.errorToString(httpCode).c_str()); 
        }
          https.end();
        }
    }
    pendingChange = false;
    Serial.println("--- ReVsend() done ---");
  }