// ============================================================
//  Waveshare ESP32-S3-Relay-6CH  —  Smart Farm (PLC Master Mode)
//  Least Delay Control + 1 Min Telemetry + Multi-Sensor Sniffer
// ============================================================

#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <Adafruit_NeoPixel.h>
#include <esp_wifi.h>
#include "config_manager.h"
#include "config_portal.h"
#include "ota_manager.h"

#include <esp_task_wdt.h>
#define WDT_TIMEOUT_SEC 60

#define RGB_PIN   38
#define RGB_COUNT 1
Adafruit_NeoPixel* rgbLed = nullptr;

#define BTN_CLEAR_WIFI 0

static const int RELAY_PINS[6] = { 1, 2, 41, 42, 45, 46 };
uint16_t esp_holding_regs[6] = {0, 0, 0, 0, 0, 0}; 

#define RS485_TX_PIN  17
#define RS485_RX_PIN  18
#define RS485_BAUD    4800
HardwareSerial* RS485Serial = nullptr;

DeviceConfig  config;
WiFiClient*   wifiClient = nullptr;
PubSubClient* mqttPtr = nullptr;

String deviceId;
String topicTelemetry, topicStatus, topicCommand;
bool   relayState[7] = {false};

// ==========================================
//  ตัวแปรเก็บค่าเซ็นเซอร์
// ==========================================
uint8_t rxBuf[8]; 
int current_sensor = -1; 

int latest_soil_temperature = 0;
int latest_soil_humidity = 0;
int latest_ph = 0;
int latest_ec = 0;
int latest_n = 0;
int latest_p = 0;
int latest_k = 0;

int latest_air_humidity = 0;
int latest_air_temperature = 0;

// ============================================================
//  Helper: ดึง MAC Address จาก Hardware eFuse Direct (ไม่มีทางซ้ำ)
// ============================================================
String getHardwareMac() {
    uint64_t chipid = ESP.getEfuseMac();
    char macStr[13];
    snprintf(macStr, sizeof(macStr), "%04X%08X", 
             (uint16_t)(chipid >> 32), 
             (uint32_t)chipid);
    return String(macStr);
}

// ============================================================
//  RGB Helpers & Modbus CRC
// ============================================================
void rgbShow(uint8_t r, uint8_t g, uint8_t b, uint8_t brightness = 60) {
    rgbLed->setBrightness(brightness);
    rgbLed->setPixelColor(0, rgbLed->Color(r, g, b));
    rgbLed->show();
}
void rgbOff()       { rgbShow(0, 0, 0, 10); }
void rgbGreen()     { rgbShow(0, 255, 0, 70); }
void rgbRed()       { rgbShow(255, 0, 0, 70); }
void rgbBlue()      { rgbShow(0, 0, 255, 70); } 
void rgbBlinkBlue() { rgbShow(0, 0, 255, 70); delay(80); rgbOff(); delay(80); }

static uint16_t modbusCRC(const uint8_t* data, uint16_t len) {
    uint16_t crc = 0xFFFF;
    for (uint16_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++) crc = (crc & 1) ? (crc >> 1) ^ 0xA001 : crc >> 1;
    }
    return crc;
}

void relayInit() {
    for (int i = 0; i < 6; i++) {
        pinMode(RELAY_PINS[i], OUTPUT);
        digitalWrite(RELAY_PINS[i], LOW);
    }
}

// ============================================================
//  WiFi Connect 
// ============================================================
bool isBootPressed() { return digitalRead(BTN_CLEAR_WIFI) == LOW; }
void hardClearWifiAndRestart() {
    WiFi.disconnect(true, true); delay(200); esp_wifi_restore(); delay(300); ESP.restart();
}
void clearWiFiIfBootHeldAtBoot() {
    if (!isBootPressed()) return;
    uint32_t start = millis();
    while (isBootPressed()) { if (millis() - start >= 5000) hardClearWifiAndRestart(); rgbBlinkBlue(); }
}
void clearWiFiIfBootHeldDuringWindow() {
    uint32_t windowStart = millis(), holdStart = 0; bool startedHold = false;
    while (millis() - windowStart < 8000) {
        if (isBootPressed()) {
            if (!startedHold) { startedHold = true; holdStart = millis(); }
            if (millis() - holdStart >= 5000) hardClearWifiAndRestart();
            rgbBlinkBlue();
        } else { startedHold = false; delay(10); }
    }
}
void ensureWiFiConnected() {
    if (WiFi.status() == WL_CONNECTED) return;
    esp_task_wdt_delete(NULL);
    WiFi.disconnect(true, true); delay(500); WiFi.mode(WIFI_STA); WiFi.begin(config.wifi_ssid, config.wifi_pass);
    unsigned long startAttempt = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - startAttempt < 15000) { delay(500); rgbBlinkBlue(); }
    if (WiFi.status() != WL_CONNECTED || strlen(config.wifi_ssid) == 0) {
        rgbBlue(); startConfigPortal(); while (true) { handlePortal(); delay(10); }
    }
    WiFi.mode(WIFI_STA); rgbGreen(); esp_task_wdt_add(NULL);
}

// ============================================================
//  onMessage — รับคำสั่งจากเว็บ
// ============================================================
void onMessage(char* topic, byte* payload, unsigned int length) {
    String msg = "";
    for (unsigned int i = 0; i < length; i++) msg += (char)payload[i];
    if (otaHandleMQTT(topic, msg.c_str())) return;
    if (String(topic) != topicCommand) return;
    StaticJsonDocument<128> doc;
    if (deserializeJson(doc, msg)) return;

    if (doc.containsKey("relay") && doc.containsKey("state")) {
        int  relay = doc["relay"];
        bool state = doc["state"];
        if (relay >= 1 && relay <= 6) {
            relayState[relay] = state; otaSaveRelay(relayState);   
            esp_holding_regs[relay - 1] = state ? 1 : 0; 
            Serial.printf("MQTT -> Relay%d: %s\n", relay, state ? "ON" : "OFF");
        }
    }
}

void connectMQTT() {
    int mqttRetry = 0;
    while (!mqttPtr->connected()) {
        esp_task_wdt_reset();
        if (++mqttRetry > 10) ESP.restart();
        String lwt = "{\"online\":false,\"mac\":\"" + deviceId + "\"}";
        
        // ใช้ deviceId ที่ดึงจาก eFuse เป็น Client ID ทันที
        if (mqttPtr->connect(deviceId.c_str(), config.mqtt_user, config.mqtt_pass, topicStatus.c_str(), 1, true, lwt.c_str(), false)) {
            mqttPtr->subscribe(topicCommand.c_str(), 1);
            if (g_topicOtaCmd.length() > 0) mqttPtr->subscribe(g_topicOtaCmd.c_str(), 1);
            String online = "{\"online\":true,\"mac\":\"" + deviceId + "\",\"fw\":\"" + otaGetVersion() + "\"}";
            mqttPtr->publish(topicStatus.c_str(), online.c_str(), true);
            rgbGreen();
        } else {
            rgbRed(); unsigned long retryStart = millis();
            while (millis() - retryStart < 5000) { esp_task_wdt_reset(); delay(200); }
        }
    }
}

// ============================================================
//  sendTelemetry — ส่งข้อมูลขึ้นเว็บ (ทุก 1 นาที)
// ============================================================
void sendTelemetry() {
    if (g_otaInProgress) return;   
    StaticJsonDocument<512> doc;
    doc["mac"] = deviceId;
    doc["fw"]  = otaGetVersion(); 
    doc["model"] = "Waveshare";  

    doc["soil_temp"]  = latest_soil_temperature / 10.0;
    doc["soil_humid"] = latest_soil_humidity / 10.0;
    doc["ph"] = latest_ph / 10.0;
    doc["ec"] = latest_ec;
    doc["n"]  = latest_n;
    doc["p"]  = latest_p;
    doc["k"]  = latest_k;

    doc["air_humid"] = latest_air_humidity / 10.0;
    doc["air_temp"]  = latest_air_temperature / 10.0;

    String payload; serializeJson(doc, payload);
    if (mqttPtr->publish(topicTelemetry.c_str(), payload.c_str())) {
        Serial.println("Sent Telemetry: " + payload);
    }
}

// ============================================================
//  setup()
// ============================================================
void setup() {
    Serial.begin(115200);
    
    // --- เปลี่ยนมาดึง Device ID จาก eFuse ทันทีที่เปิดเครื่อง ---
    deviceId = getHardwareMac();
    Serial.println("\n[SYSTEM] My MQTT Client ID: " + deviceId);
    // ---------------------------------------------------
    
    wifiClient = new WiFiClient(); delay(500);
    rgbLed = new Adafruit_NeoPixel(RGB_COUNT, RGB_PIN, NEO_GRB + NEO_KHZ800);
    RS485Serial = new HardwareSerial(2);
    rgbLed->begin(); rgbOff(); relayInit(); 
    pinMode(BTN_CLEAR_WIFI, INPUT_PULLUP);
    
    otaLoadRelay(relayState, RELAY_PINS);
    for (int i=1; i<=6; i++) esp_holding_regs[i-1] = relayState[i] ? 1 : 0;
    
    otaValidatePartition();
    RS485Serial->begin(RS485_BAUD, SERIAL_8N1, RS485_RX_PIN, RS485_TX_PIN);

    clearWiFiIfBootHeldAtBoot();
    clearWiFiIfBootHeldDuringWindow();
    bool needConfig = !isConfigured();
    unsigned long waitStart = millis();
    while (millis() - waitStart < 5000) { if (Serial.available()) { while (Serial.available()) Serial.read(); needConfig = true; break; } }
    if (needConfig) { startConfigPortal(); while (true) { handlePortal(); delay(10); } }

    config = loadConfig();
    esp_task_wdt_config_t wdt_config = { .timeout_ms = WDT_TIMEOUT_SEC * 1000, .idle_core_mask = (1 << portNUM_PROCESSORS) - 1, .trigger_panic = true };
    esp_task_wdt_reconfigure(&wdt_config); esp_task_wdt_add(NULL);
    esp_task_wdt_reset(); ensureWiFiConnected();

    // --- ตั้งค่า Topic ต่างๆ ให้เรียบร้อยก่อนต่อ MQTT ---
    topicTelemetry = "farm/" + deviceId + "/telemetry"; 
    topicStatus = "farm/" + deviceId + "/status"; 
    topicCommand = "farm/" + deviceId + "/cmd/relay";

    mqttPtr = new PubSubClient(*wifiClient); mqttPtr->setServer(config.mqtt_server, config.mqtt_port); mqttPtr->setCallback(onMessage); mqttPtr->setBufferSize(512);
    connectMQTT(); otaSetup(); sendTelemetry();
}

// ============================================================
//  loop()
// ============================================================
void loop() {
    esp_task_wdt_reset();

    if (WiFi.status() != WL_CONNECTED) {
        WiFi.disconnect(); WiFi.reconnect(); 
        unsigned long startWait = millis();
        while(WiFi.status() != WL_CONNECTED && millis() - startWait < 5000) { esp_task_wdt_reset(); delay(500); }
    }
    if (WiFi.status() == WL_CONNECTED) {
        if (!mqttPtr->connected()) connectMQTT(); else mqttPtr->loop(); otaLoop();   
    }

    // ── ระบบอ่านข้อมูล RS485 (Rolling Buffer Sniffer) ────────────────
    while (RS485Serial->available()) {
        uint8_t incomingByte = RS485Serial->read();
        
        for(int i = 0; i < 7; i++) {
            rxBuf[i] = rxBuf[i+1];
        }
        rxBuf[7] = incomingByte;

        // =======================================================
        // 1. ดักจับคำสั่งที่ PLC ถามเซ็นเซอร์ (Request)
        // =======================================================
        if (rxBuf[2] == 0x01 && rxBuf[3] == 0x04) {
            if (rxBuf[5] == 0x00 && rxBuf[7] == 0x02) current_sensor = 10; // Temp/Hum
            else if (rxBuf[5] == 0x03 && rxBuf[7] == 0x01) current_sensor = 11; // pH
            else if (rxBuf[5] == 0x09 && rxBuf[7] == 0x01) current_sensor = 12; // EC
            else if (rxBuf[5] == 0x10 && rxBuf[7] == 0x01) current_sensor = 13; // N
            else if (rxBuf[5] == 0x12 && rxBuf[7] == 0x01) current_sensor = 14; // P
            else if (rxBuf[5] == 0x13 && rxBuf[7] == 0x01) current_sensor = 15; // K
        }
        else if (rxBuf[2] == 0x02 && rxBuf[3] == 0x04 && rxBuf[5] == 0x00 && rxBuf[7] == 0x02) {
            current_sensor = 20; // Air Temp/Hum
        }

        // =======================================================
        // 2. ดักจับคำตอบจากเซ็นเซอร์ (Response)
        // =======================================================
        // กรณีตอบกลับ 1 Word (pH, EC, N, P, K) -> Byte Count = 02
        else if ((rxBuf[3] == 0x01 || rxBuf[3] == 0x02) && rxBuf[4] == 0x04 && rxBuf[5] == 0x02) {
            int val = (rxBuf[6] << 8) | rxBuf[7]; 
            
            if (current_sensor == 11) { latest_ph = val; current_sensor = -1; }
            else if (current_sensor == 12) { latest_ec = val; current_sensor = -1; }
            else if (current_sensor == 13) { latest_n = val; current_sensor = -1; }
            else if (current_sensor == 14) { latest_p = val; current_sensor = -1; }
            else if (current_sensor == 15) { latest_k = val; current_sensor = -1; }
        }
        
        // กรณีตอบกลับ 2 Words (Temp/Hum) -> Byte Count = 04
        else if ((rxBuf[1] == 0x01 || rxBuf[1] == 0x02) && rxBuf[2] == 0x04 && rxBuf[3] == 0x04) {
            int val1 = (rxBuf[4] << 8) | rxBuf[5];
            int val2 = (rxBuf[6] << 8) | rxBuf[7];
            
            if (current_sensor == 10) { 
                latest_soil_humidity = val1; 
                latest_soil_temperature = val2; 
                current_sensor = -1; 
            }
            else if (current_sensor == 20) {
                latest_air_humidity = val1;
                latest_air_temperature = val2;
                current_sensor = -1;
            }
        }
        
        // =======================================================
        // 3. ตอบกลับเมื่อ PLC ถามหา ESP32 (Slave ID 3, FC 03)
        // =======================================================
        else if (rxBuf[0] == 0x03 && rxBuf[1] == 0x03) {
            uint16_t rxCrc = (rxBuf[7] << 8) | rxBuf[6];
            if (modbusCRC(rxBuf, 6) == rxCrc) {
                
                uint16_t num_regs = (rxBuf[4] << 8) | rxBuf[5]; 
                uint8_t txBuf[32];
                txBuf[0] = 0x03; 
                txBuf[1] = 0x03; 
                txBuf[2] = num_regs * 2; 
                
                for (int i = 0; i < num_regs; i++) {
                    uint16_t val = (i < 6) ? esp_holding_regs[i] : 0; 
                    txBuf[3 + (i*2)] = val >> 8;   
                    txBuf[4 + (i*2)] = val & 0xFF; 
                }
                
                uint16_t txCrc = modbusCRC(txBuf, 3 + (num_regs * 2));
                txBuf[3 + (num_regs * 2)] = txCrc & 0xFF;
                txBuf[4 + (num_regs * 2)] = txCrc >> 8;
                
                RS485Serial->write(txBuf, 5 + (num_regs * 2));
                RS485Serial->flush();
                memset(rxBuf, 0, sizeof(rxBuf)); 
            }
        }
    }

    // ── ส่ง Telemetry ขึ้นเว็บทุกๆ 1 นาที ────────────────────
    unsigned long now = millis();
    static unsigned long lastSend = 0;
    
    if (now - lastSend >= 60000) { 
        lastSend = now;
        if (WiFi.status() == WL_CONNECTED) sendTelemetry();
    }
}