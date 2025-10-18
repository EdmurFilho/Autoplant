#include <DHT.h> 
#include <WiFi.h>              
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include "image.h"  // Certifique-se de que este arquivo existe (seu array de imagem)

// --- CONSTANTES DE CALIBRAÇÃO E HARDWARE ---
// Calibração do Sensor de Umidade do Solo (Lembre-se: O mapeamento é inverso)
const int SOLO_SECO_BRUTO = 2600;  // Valor lido quando o sensor está seco (0% umidade)
const int SOLO_UMIDO_BRUTO = 1350; // Valor lido quando o sensor está submerso (100% umidade)

// Pinos TFT
#define TFT_DC 26
#define TFT_CS 32
#define TFT_RST 25

Adafruit_ILI9341 tft = Adafruit_ILI9341(TFT_CS, TFT_DC, TFT_RST);

// Credenciais Wi-Fi
const char* WIFI_SSID  = "Paradiso";
const char* WIFI_PASSWORD = "8167350Rm";

// Firebase HTTPS
const char* FIREBASE_HOST = "tca-autoplant-default-rtdb.firebaseio.com/";
String databaseURL = "https://" + String(FIREBASE_HOST);

#define DHTPIN 5
#define DHTTYPE DHT22
DHT dht(DHTPIN, DHTTYPE);

// Sensores / Atuadores
#define sense_solo 36
#define bomba 27
#define potenci_r 34
#define potenci_v 35 
#define PIN_boia 14
#define button 13

// --- VARIÁVEIS GLOBAIS ---
bool Agua = 0;
String AguaS;
float Solo = 0.00;    // Umidade mapeada (0.00 a 100.00)
float pastSolo = 0.00; // Restaurada: Último Solo enviado/registrado.
int SoloBruto = 0;    // Leitura bruta (0 a 4095)
float Temp = 0;
int Regas = 3;
int Vol = 1;
bool Regar = 0;
float Hum; 

int regR = 0, volR = 0;
int reg = 0, vol = 0;
String SregFB = "", SvolFB = "";
int regFB = 0, volFB = 0;
int pastReg = 0, pastVol = 0, pastRegFB = 0, pastVolFB = 0;
int RegS = 0, VolS = 0;

unsigned long tempoAnteriorRega = 0;
unsigned long tempoAtual;
unsigned long intervalo;
int tempoRega;
long trigger, trigger_passado;
int rep;

// Controle de Potenciômetro (Debounce)
unsigned long tempoUltimaMudanca = 0;
const long INTERVALO_DEBOUNCE = 500; // 500 ms
bool mudancaPendente = false;

// Controle de Atualização da Tela (100 ms)
unsigned long tempoUltimaAtualizacaoTela = 0;
const long INTERVALO_TELA = 100;

// Cliente seguro e cliente HTTP
WiFiClientSecure client;
HTTPClient https;

// --- DECLARAÇÃO DE FUNÇÕES ---
void update();
void warning();
void regar();
void valoresderega();
void ReVsend();
void TFTbackground(const uint16_t *bmpData, uint16_t vWidth, uint16_t vHeight);
void TFTdata();
void ler_e_mapear_solo(); 

void setup() {
  pinMode(bomba, OUTPUT);         
  pinMode(button, INPUT_PULLUP);  
  pinMode(4,OUTPUT);
  pinMode(sense_solo, INPUT);
  pinMode(potenci_r, INPUT);
  pinMode(potenci_v, INPUT);
  digitalWrite(bomba, LOW);

  Serial.begin(115200);
  Serial.println("\n--- INICIANDO SETUP ---");

  dht.begin();
  
  // Leitura inicial para setup das variáveis
  regR = analogRead(potenci_r);
  volR = analogRead(potenci_v);
  reg = map(regR, 0, 4095, 1, 10);
  vol = map(volR, 0, 4095, 1, 10);
  
  // Inicializa o trigger_passado com a primeira leitura MAPEADA
  trigger_passado = reg + vol; 
  RegS = reg;
  VolS = vol; 

  tft.begin();
  tft.setRotation(0);
  TFTbackground(imageBMP, image_WIDTH, image_HEIGHT);

  // Conexão Wi-Fi
  Serial.print("Endereço MAC: ");
  Serial.println(WiFi.macAddress());
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Conectando ao Wi-Fi");
  
  while (WiFi.status() != WL_CONNECTED) {
    delay(500); 
    Serial.print(".");
  }
  Serial.println("\nWiFi conectado com sucesso!");
  Serial.print("Endereço IP: ");
  Serial.println(WiFi.localIP());
  client.setInsecure(); 

  Serial.println("Chamando valoresderega() pela primeira vez no setup.");
  valoresderega(); 
  tempoAnteriorRega = millis();
  Serial.println("--- SETUP CONCLUÍDO ---");
}

// --- LOOP ---
void loop() {
  tempoAtual = millis();
  Temp = dht.readTemperature();
  
  ler_e_mapear_solo();
  
  regR = analogRead(potenci_r);
  volR = analogRead(potenci_v);
  reg = map(regR, 0, 4095, 1, 10);
  vol = map(volR, 0, 4095, 1, 10);

  // Lógica de rega temporizada (non-blocking)
  if (tempoAtual - tempoAnteriorRega >= intervalo) {
    tempoAnteriorRega = tempoAtual; 
    Serial.println("Intervalo de rega atingido. Iniciando ciclo.");
    regar();
  }
  
  // Lógica de debounce para potenciômetros
   trigger = reg + vol;

  // 1. Se o valor mapeado mudou (Mesmo que seja por ruído no último ponto)
  if(trigger != trigger_passado){ 
    trigger_passado = trigger;
    tempoUltimaMudanca = tempoAtual;
    mudancaPendente = true; // SINALIZA UMA MUDANÇA
  } 
  
  // 2. Dispara o envio SE HOUVE MUDANÇA E SE PASSOU o tempo de estabilização (500ms)
  if (mudancaPendente && (tempoAtual - tempoUltimaMudanca >= INTERVALO_DEBOUNCE)) {
      
    // CHECAGEM CRUCIAL: Só envia se o valor MAPEADO (1-10) for diferente do último valor ENVIADO
    if (reg != RegS || vol != VolS) {
        Serial.printf("Potenciômetros estabilizados após %d ms. Enviando para o Firebase.\n", INTERVALO_DEBOUNCE);
        ReVsend(); 
        // ATUALIZA RegS/VolS para o novo valor enviado
        RegS = reg;
        VolS = vol;
        
    }
    mudancaPendente = false; // Reseta a flag para EVITAR REPETIÇÃO
  }

  // Rega manual
  if(digitalRead(button) == LOW){ 
    Serial.println("Botão de rega manual pressionado!");
    regar();
    tempoAnteriorRega = tempoAtual; 
    delay(200); 
  }

  // ATUALIZAÇÃO DA TELA (non-blocking: 100 ms)
  if (tempoAtual - tempoUltimaAtualizacaoTela >= INTERVALO_TELA) {
      tempoUltimaAtualizacaoTela = tempoAtual;
      TFTdata(); 
  }
}

void ler_e_mapear_solo() {
    SoloBruto = analogRead(sense_solo);
    // Mapeamento: (SECO -> 0%) e (UMIDO -> 100%)
    float solo_map = map(SoloBruto, SOLO_SECO_BRUTO, SOLO_UMIDO_BRUTO, 0, 100);
    
    // Garante que o valor fique entre 0.00 e 100.00
    Solo = constrain(solo_map, 0.00, 100.00); 
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
    // Posição Y inicial (ajustada para melhor visualização)
    int y_start = 75; 
    int y_increment = 32; // Espaçamento
    int x_data = 135; // Posição X para todos os dados
    
    tft.setTextColor(ILI9341_BLACK); 
    
    // --- VOLUME ---
    int y_vol = y_start;
    tft.fillRect(x_data, y_vol + 10, 100, 15, ILI9341_WHITE); 
    tft.setTextSize(1); 
    tft.setCursor(x_data, y_vol);
    tft.println("volume de rega:");
    tft.setTextSize(2); 
    tft.setCursor(x_data, y_vol + 10);
    tft.print(VolS * 100); 
    tft.setTextSize(1); 
    tft.println("ml");

    // --- REGAS ---
    int y_reg = y_vol + y_increment;
    tft.fillRect(x_data, y_reg + 10, 100, 15, ILI9341_WHITE); 
    tft.setTextSize(1); 
    tft.setCursor(x_data, y_reg);
    tft.println("regas:");
    tft.setTextSize(2); 
    tft.setCursor(x_data, y_reg + 10);
    tft.println(RegS);

    // --- INTERVALO ---
    int y_int = y_reg + y_increment;
    tft.fillRect(x_data, y_int + 10, 100, 15, ILI9341_WHITE);
    tft.setTextSize(1); 
    tft.setCursor(x_data, y_int);
    tft.println("intervalo:");
    tft.setTextSize(2); 
    tft.setCursor(x_data, y_int + 10);
    tft.println(String(intervalo/3600000.0)); 

    // --- TEMPERATURA ---
    int y_temp = y_int + y_increment;
    tft.fillRect(x_data, y_temp + 10, 100, 15, ILI9341_WHITE);
    tft.setTextSize(1); 
    tft.setCursor(x_data, y_temp);
    tft.print("Temp:");
    tft.setTextSize(2); 
    tft.setCursor(x_data, y_temp + 10);
    tft.println(String(Temp, 1) + "C"); 

    // --- UMIDADE DO SOLO ---
    int y_solo = y_temp + y_increment;
    tft.fillRect(x_data, y_solo + 10, 100, 15, ILI9341_WHITE); 
    tft.setTextSize(1); 
    tft.setCursor(x_data, y_solo);
    tft.print("Umi.solo:");
    tft.setTextSize(2); 
    tft.setCursor(x_data, y_solo + 10);
    tft.printf("%.2f%%\n", Solo); 
}

void update(){
  Serial.println("\n--- EXECUTANDO update() (Envio para Firebase RESTAURADO) ---");
  Temp = dht.readTemperature();
  ler_e_mapear_solo(); 
  Agua = !digitalRead(PIN_boia);
  AguaS = Agua ? "true" : "false";
  
  // RESTAURADO: Atualiza o último valor registrado antes do envio
  pastSolo = Solo; 
  
  Serial.printf("Valores lidos/enviados: Temp=%.2fC, Solo=%.2f%%, Agua=%d\n", Temp, Solo, Agua);

  // Upload temperature
  String urlPutTemp = databaseURL + "/Ambiente1/Temp.json";
  Serial.println("Tentando enviar Temperatura...");
  if (https.begin(client, urlPutTemp)) {
    https.addHeader("Content-Type", "application/json");            
    int httpCode = https.PUT(String(Temp, 1));   
    if (httpCode > 0) { Serial.printf("PUT [Temp] OK: %d\n", httpCode); } 
    else { Serial.printf("PUT [Temp] FALHA: %s\n", https.errorToString(httpCode).c_str()); }
    https.end();
  }

  // Upload soil humidity
  String urlPutSolo = databaseURL + "/Ambiente1/Solo.json";
  Serial.println("Tentando enviar Umidade do Solo...");
  if (https.begin(client, urlPutSolo)) {
    https.addHeader("Content-Type", "application/json");            
    int httpCode = https.PUT(String(Solo, 2));  
    if (httpCode > 0) { Serial.printf("PUT [Solo] OK: %d\n", httpCode); } 
    else { Serial.printf("PUT [Solo] FALHA: %s\n", https.errorToString(httpCode).c_str()); }
    https.end();
  }

  // Upload water sensor
  String urlPutAgua = databaseURL + "/Ambiente1/Agua.json";
  Serial.println("Tentando enviar Sensor de Água...");
  if (https.begin(client, urlPutAgua)) {
    https.addHeader("Content-Type", "application/json");            
    int httpCode = https.PUT(String(AguaS));    
    if (httpCode > 0) { Serial.printf("PUT [Agua] OK: %d\n", httpCode); } 
    else { Serial.printf("PUT [Agua] FALHA: %s\n", https.errorToString(httpCode).c_str()); }
    https.end();
  }
  Serial.println("--- Fim da função update() ---");
}

void warning(){
  Serial.println("!!! ATENÇÃO: Reabastecer a água!!!!");
  Agua = !digitalRead(PIN_boia);
  AguaS = Agua ? "true" : "false";

  // Upload water sensor
  String urlPutAgua = databaseURL + "/Ambiente1/Agua.json";
  Serial.println("Tentando enviar Sensor de Água...");
  if (https.begin(client, urlPutAgua)) {
    https.addHeader("Content-Type", "application/json");            
    int httpCode = https.PUT(String(AguaS));    
    if (httpCode > 0) { Serial.printf("PUT [Agua] OK: %d\n", httpCode); } 
    else { Serial.printf("PUT [Agua] FALHA: %s\n", https.errorToString(httpCode).c_str()); }
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
 Serial.println("--- Fim da função warning() ---");
}

void regar(){
  Serial.println("\n--- EXECUTANDO regar() ---");
  digitalWrite(4,1);
  delay(400);
  digitalWrite(4,0);
  Agua = !digitalRead(PIN_boia);
  if(Agua){ 
    Serial.printf("Nível de água OK. Ligando a bomba por %d ms.\n", tempoRega);
    digitalWrite(bomba,1); 
    delay(tempoRega); 
    digitalWrite(bomba,0);
    Serial.println("Rega concluída.");
    valoresderega(); 
    update();
  } else {
    Serial.println("Nível de água baixo. Não é possível regar.");
    warning();
    while (!Agua){ 
      Serial.println("Aguardando reabastecimento de água...");
      Agua = !digitalRead(PIN_boia);
      delay(500);
    }
    Serial.println("Água reabastecida!");
  }
}

void valoresderega(){
  Serial.println("\n--- EXECUTANDO valoresderega() ---");
  regR = analogRead(potenci_r);
  volR = analogRead(potenci_v);
  Temp = dht.readTemperature();
  ler_e_mapear_solo(); 
  reg = map(regR, 0, 4095, 1, 10);
  vol = map(volR, 0, 4095, 1, 10);

  // GET Regas from Firebase
  String urlGetRegas = databaseURL + "/Ambiente1/Regas.json";
  Serial.println("Tentando buscar 'Regas' do Firebase...");  
  if (https.begin(client, urlGetRegas)) {
    int httpCode = https.GET();
    if (httpCode > 0) {
      Serial.printf("GET [Regas] de %s - Código de resposta: %d\n", urlGetRegas.c_str(), httpCode); 
      if (httpCode == HTTP_CODE_OK) {
        SregFB = https.getString();
        Serial.println("Valor de 'Regas' recebido do Firebase: " + SregFB);
      }
    } else {
      Serial.printf("GET [Regas] falhou, erro: %s\n", https.errorToString(httpCode).c_str());
    }
    https.end();
  } else {
    Serial.println("Falha ao iniciar conexão HTTPS para buscar Regas.");
  }
    // GET Volume from Firebase
  String urlGetVolume = databaseURL + "/Ambiente1/Vol.json";
  Serial.println("Tentando buscar 'Volume' do Firebase...");    
  if (https.begin(client, urlGetVolume)) {
    int httpCode = https.GET();
    if (httpCode > 0) {
      Serial.printf("GET [Vol] de %s - Código de resposta: %d\n", urlGetVolume.c_str(), httpCode); 
      if (httpCode == HTTP_CODE_OK) {
        SvolFB = https.getString();
        Serial.println("Valor de 'Volume' recebido do Firebase: " + SvolFB);
      }
    } else {
      Serial.printf("GET [Vol] falhou, erro: %s\n", https.errorToString(httpCode).c_str());
    }
    https.end();
  } else {
    Serial.println("Falha ao iniciar conexão HTTPS para buscar Volume.");
  }
  
  regFB = SregFB.toInt();
  volFB = SvolFB.toInt();

  if (vol != pastVol) {
    VolS = vol;
    pastVol = vol;
  } else if (volFB != pastVolFB) {
    VolS = volFB;
    pastVolFB = volFB;
  }
  
  if (reg != pastReg) {
    RegS = reg;
    pastReg = reg;
  } else if (regFB != pastRegFB) {
    RegS = regFB;
    pastRegFB = regFB;
  }

  // Final calculations
  tempoRega = VolS * 6250; 
  intervalo = (86400000 / RegS) - tempoRega; 
  Serial.printf("Cálculos finais - tempoRega: %d ms, intervalo: %lu ms\n", tempoRega, intervalo);
  Serial.println("--- Fim da função valoresderega() ---");
  TFTdata();
}

void ReVsend(){
  Serial.println("\n--- EXECUTANDO ReVsend() ---");
  if(RegS != reg){
    RegS = reg;
    // PUT Regas
    String urlPutRegas = databaseURL + "/Ambiente1/Regas.json";
    Serial.printf("Tentando PUT [Regas] com valor %d...\n", RegS);
    if (https.begin(client, urlPutRegas)) {
      https.addHeader("Content-Type", "application/json");
      int httpCode = https.PUT(String(RegS));
      if (httpCode > 0) { 
        Serial.printf("PUT [Regas] OK: %d\n", httpCode); 
      }else{ 
        Serial.printf("PUT [Regas] FALHA: %s\n", https.errorToString(httpCode).c_str()); 
      }
        https.end();
      }
    }
    if(VolS != vol){
    VolS = vol;
    // PUT Volume
    String urlPutVolume = databaseURL + "/Ambiente1/Vol.json";
    Serial.printf("Tentando PUT [Vol] com valor %d...\n", VolS);
    if (https.begin(client, urlPutVolume)) {
        https.addHeader("Content-Type", "application/json");
        int httpCode = https.PUT(String(VolS));
        if (httpCode > 0) { 
          Serial.printf("PUT [Vol] OK: %d\n", httpCode); 
        }else{ 
          Serial.printf("PUT [Vol] FALHA: %s\n", https.errorToString(httpCode).c_str()); 
        }
          https.end();
        }
    }
    mudancaPendente = false;
    Serial.println("--- Fim da função ReVsend() ---");
  }