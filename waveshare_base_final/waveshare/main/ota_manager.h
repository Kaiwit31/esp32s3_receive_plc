#pragma once
// ============================================================
//  ota_manager.h  v2.0
//  ใช้คู่กับ main.ino + config_manager.h + config_portal.h
//
//  จุดที่เพิ่มใน main.ino:
//  [OTA#1] #include "ota_manager.h"             หลัง include อื่นๆ
//  [OTA#2] otaValidatePartition()               บรรทัดแรกของ setup()
//  [OTA#3] otaLoadRelay(relayState, RELAY_PINS) หลัง pinMode ใน setup()
//  [OTA#4] otaSetup()                           หลัง connectMQTT() ใน setup()
//  [OTA#5] if (otaHandleMQTT(topic, msg.c_str())) return;  บรรทัดแรก onMessage()
//  [OTA#6] otaSaveRelay(relayState)             ทุกครั้งที่ relay เปลี่ยนใน onMessage()
//  [OTA#7] otaLoop()                            ใน loop()
//  [OTA#8] doc["fw"] = otaGetVersion()          ใน sendTelemetry()
//
//  การเปลี่ยนแปลงจาก v1.0:
//  - otaHandleMQTT รับ url และ version จาก MQTT payload โดยตรง
//  - เพิ่ม otaDownloadFromUrl() ดาวน์โหลดจาก URL ที่กำหนด
//  - otaCheckAndUpdate() ยังคงไว้สำหรับ auto-check แบบเดิม
//  - StaticJsonDocument ขยายเป็น 256 รองรับ url ยาว
// ============================================================

#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include <WiFiClientSecure.h>
#include <WiFiClient.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <esp_task_wdt.h>
#include <esp_ota_ops.h>

// ── ตัวแปรจาก main.ino ────────────────────────────────────────
extern PubSubClient* mqttPtr;
extern String        deviceId;
extern DeviceConfig  config;

#ifndef OTA_WDT_TIMEOUT_SEC
#define OTA_WDT_TIMEOUT_SEC 60
#endif

// ── OTA auto-check interval (6 ชั่วโมง) ─────────────────────
#ifndef OTA_CHECK_INTERVAL_MS
  #define OTA_CHECK_INTERVAL_MS (6UL * 60UL * 60UL * 1000UL)
#endif

// ── MQTT topics ───────────────────────────────────────────────
String g_topicOtaCmd;    // farm/{id}/cmd/ota
String g_topicOtaStatus; // farm/{id}/ota/status

// ── state ─────────────────────────────────────────────────────
bool             g_otaInProgress = false;
static bool          s_otaNotified  = false;
static unsigned long s_lastOtaCheck = 0;
static uint8_t       s_lastPct      = 255;

// ── เก็บ URL และ version จาก MQTT payload ──────────────────
static String s_otaUrl     = "";   // URL จาก MQTT {"url":"https://..."}
static String s_otaVersion = "";   // version จาก MQTT {"version":"1.1.0"}

// ── forward declarations ──────────────────────────────────────
static void otaPublish(const char* state, int progress = -1, const char* detail = "");
static String otaGetServerUrl();
static bool otaCheckAndUpdate();
static bool otaDownloadFromUrl(const String& url, const String& newVersion);

// ============================================================
//  otaGetVersion / otaSaveVersion
// ============================================================
String otaGetVersion() {
    Preferences p;
    p.begin("ota", true);
    String v = p.getString("fw_ver", "1.0.0");
    p.end();
    return v;
}

static void otaSaveVersion(const char* ver) {
    Preferences p;
    p.begin("ota", false);
    p.putString("fw_ver", ver);
    p.end();
}

// ============================================================
//  otaPublish — ส่ง JSON status ไป farm/{id}/ota/status
// ============================================================
static void otaPublish(const char* state, int progress, const char* detail) {
    if (!mqttPtr || !mqttPtr->connected()) return;
    StaticJsonDocument<200> doc;
    doc["state"] = state;
    doc["mac"]   = deviceId;
    doc["fw"]    = otaGetVersion();
    if (progress >= 0)      doc["progress"] = progress;
    if (strlen(detail) > 0) doc["detail"]   = detail;
    char buf[256];
    serializeJson(doc, buf);
    mqttPtr->publish(g_topicOtaStatus.c_str(), buf, false);
    Serial.printf("[OTA] status: %s\n", buf);
}

// ============================================================
//  otaGetServerUrl — fallback สำหรับ auto-check
// ============================================================
static String otaGetServerUrl() {
    Preferences p;
    p.begin("ota", true);
    String url = p.getString("ota_url", "");
    p.end();
    if (url.length() == 0)
        url = "http://" + String(config.mqtt_server) + ":5000";
    return url;
}

void otaSetServerUrl(const String& url) {
    Preferences p;
    p.begin("ota", false);
    p.putString("ota_url", url);
    p.end();
    Serial.println("[OTA] Server URL saved: " + url);
}

// ============================================================
//  Relay save/load — คงสถานะ relay หลัง OTA restart
// ============================================================
void otaSaveRelay(bool states[7]) {
    Preferences p;
    p.begin("relay", false);
    for (int i = 1; i <= 6; i++)
        p.putBool(("r" + String(i)).c_str(), states[i]);
    p.end();
}

void otaLoadRelay(bool states[7], const int pins[6]) {
    Preferences p;
    p.begin("relay", true);
    for (int i = 1; i <= 6; i++) {
        states[i] = p.getBool(("r" + String(i)).c_str(), false);
        digitalWrite(pins[i - 1], states[i] ? HIGH : LOW);
        Serial.printf("[OTA] Relay%d restored -> %s\n", i, states[i] ? "ON" : "OFF");
    }
    p.end();
}

// ============================================================
//  otaValidatePartition — mark firmware valid หลัง boot สำเร็จ
//  ป้องกัน rollback โดยอัตโนมัติ
// ============================================================
void otaValidatePartition() {
    const esp_partition_t* running = esp_ota_get_running_partition();
    esp_ota_img_states_t   ota_state;
    if (esp_ota_get_state_partition(running, &ota_state) == ESP_OK) {
        if (ota_state == ESP_OTA_IMG_PENDING_VERIFY) {
            esp_ota_mark_app_valid_cancel_rollback();
            Serial.println("[OTA] Firmware verified — rollback cancelled");
        }
    }
}

// ============================================================
//  otaDownloadFromUrl — download firmware จาก URL ที่กำหนด
//  ใช้เมื่อ admin ส่ง URL มาผ่าน MQTT โดยตรง
//  รองรับทั้ง http:// และ https://
// ============================================================
static bool otaDownloadFromUrl(const String& url, const String& newVersion) {
    if (g_otaInProgress) return false;
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[OTA] WiFi not connected — skip");
        otaPublish("error", -1, "wifi_not_connected");
        return false;
    }

    Serial.printf("[OTA] Download from URL: %s\n", url.c_str());
    Serial.printf("[OTA] Target version: v%s\n", newVersion.c_str());

    g_otaInProgress = true;
    s_lastPct       = 255;
    otaPublish("downloading", 0, newVersion.c_str());

    // ปิด WDT ระหว่าง download
    esp_task_wdt_delete(NULL);
    Serial.println("[OTA] WDT disabled for download");

    // progress callback — ส่งทุก 10%
    httpUpdate.onProgress([](int cur, int total) {
        if (total <= 0) return;
        uint8_t pct = (uint8_t)((cur * 100) / total);
        if (s_lastPct == 255 || pct / 10 != s_lastPct / 10) {
            s_lastPct = pct;
            otaPublish("downloading", pct);
            Serial.printf("[OTA] %d%%\n", pct);
        }
    });

    httpUpdate.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);

    t_httpUpdate_return ret;

    // เลือก http หรือ https ตาม URL
    if (url.startsWith("https://")) {
        // HTTPS — ใช้ WiFiClientSecure แบบ insecure
        // (ไม่ verify cert เพราะ ESP32 ไม่มี CA bundle)
        WiFiClientSecure secClient;
        secClient.setInsecure();  // ไม่ verify SSL cert
        ret = httpUpdate.update(secClient, url);
    } else {
        // HTTP
        WiFiClient plainClient;
        ret = httpUpdate.update(plainClient, url);
    }

    switch (ret) {
        case HTTP_UPDATE_OK:
            // บันทึก version ใหม่ก่อน restart
            if (newVersion.length() > 0) {
                otaSaveVersion(newVersion.c_str());
            }
            otaPublish("success", 100, newVersion.c_str());
            Serial.printf("[OTA] SUCCESS v%s — restarting in 2s\n", newVersion.c_str());
            delay(5000);
            ESP.restart();
            return true;  // ไม่ถึงบรรทัดนี้

        case HTTP_UPDATE_FAILED:
            Serial.printf("[OTA] FAILED (%d): %s\n",
                httpUpdate.getLastError(),
                httpUpdate.getLastErrorString().c_str());
            otaPublish("failed", -1, httpUpdate.getLastErrorString().c_str());
            break;

        case HTTP_UPDATE_NO_UPDATES:
            Serial.println("[OTA] No updates");
            otaPublish("up_to_date", -1, otaGetVersion().c_str());
            break;
    }

    // download ไม่สำเร็จ → เปิด WDT กลับ
    g_otaInProgress = false;
    s_lastPct       = 255;
    esp_task_wdt_config_t ota_wdt_config = {
    .timeout_ms = OTA_WDT_TIMEOUT_SEC * 1000,
    .idle_core_mask = (1 << portNUM_PROCESSORS) - 1, // เฝ้าระวัง core ที่ว่างงานทั้งหมด
    .trigger_panic = true
    };
    esp_task_wdt_reconfigure(&ota_wdt_config);
    esp_task_wdt_add(NULL);
    Serial.println("[OTA] WDT re-enabled");
    return false;
}

// ============================================================
//  otaCheckAndUpdate — auto-check จาก server (fallback)
//  ใช้เมื่อไม่มี URL จาก MQTT — เช็ค /version แล้ว download /firmware
// ============================================================
static bool otaCheckAndUpdate() {
    if (g_otaInProgress) return false;
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[OTA] WiFi not connected — skip");
        return false;
    }

    String serverUrl  = otaGetServerUrl();
    String currentVer = otaGetVersion();
    Serial.printf("[OTA] Auto-check: v%s  Server: %s\n",
                  currentVer.c_str(), serverUrl.c_str());
    otaPublish("checking", -1, currentVer.c_str());

    // GET /version
    WiFiClient httpWifi;
    HTTPClient http;
    http.begin(httpWifi, serverUrl + "/version");
    http.setTimeout(10000);
    int code = http.GET();

    if (code != 200) {
        Serial.printf("[OTA] /version HTTP %d\n", code);
        otaPublish("error", -1, ("HTTP " + String(code)).c_str());
        http.end();
        return false;
    }

    String body = http.getString();
    http.end();

    StaticJsonDocument<128> vdoc;
    if (deserializeJson(vdoc, body)) {
        Serial.println("[OTA] Invalid JSON from /version");
        otaPublish("error", -1, "invalid_json");
        return false;
    }

    String newVer = vdoc["version"] | "";
    bool   force  = vdoc["force"]   | false;
    Serial.printf("[OTA] Server: v%s  force=%d\n", newVer.c_str(), force);

    if (!force && newVer == currentVer) {
        Serial.println("[OTA] Already up to date");
        otaPublish("up_to_date", -1, currentVer.c_str());
        return false;
    }

    // download จาก /firmware
    return otaDownloadFromUrl(serverUrl + "/firmware", newVer);
}

// ============================================================
//  otaSetup — เรียกใน setup() หลัง connectMQTT()
// ============================================================
void otaSetup() {
    g_topicOtaCmd    = "farm/" + deviceId + "/cmd/ota";
    g_topicOtaStatus = "farm/" + deviceId + "/ota/status";

    if (mqttPtr && mqttPtr->connected()) {
        mqttPtr->subscribe(g_topicOtaCmd.c_str(), 1);
        Serial.println("[OTA] Subscribed: " + g_topicOtaCmd);
    }

    otaPublish("ready", -1, otaGetVersion().c_str());
    Serial.printf("[OTA] Ready  fw=v%s\n", otaGetVersion().c_str());
}

// ============================================================
//  otaHandleMQTT — เรียกบรรทัดแรกของ onMessage()
//
//  payload JSON ที่รองรับ:
//  {"cmd":"update","url":"https://...","version":"1.1.0"} ← admin trigger
//  {"cmd":"check"}   ← เช็ค server เอง
//  {"cmd":"version"} ← รายงาน version ปัจจุบัน
//
//  return true  = topic ตรง จัดการแล้ว
//  return false = ไม่ใช่ OTA topic
// ============================================================
bool otaHandleMQTT(const char* topic, const char* payload) {
    if (String(topic) != g_topicOtaCmd) return false;

    Serial.printf("[OTA] MQTT cmd: %s\n", payload);

    // ขยาย buffer เพราะ URL อาจยาว
    StaticJsonDocument<256> doc;
    if (deserializeJson(doc, payload)) {
        Serial.println("[OTA] JSON parse error");
        return true;  // topic ตรงแต่ parse ไม่ได้
    }

    String cmd = doc["cmd"] | "";

    if (cmd == "version") {
        // รายงาน version ปัจจุบัน
        otaPublish("ready", -1, otaGetVersion().c_str());

    } else if (cmd == "update") {
        // รับ URL และ version จาก payload โดยตรง
        String url = doc["url"] | "";
        String ver = doc["version"] | "";

        if (url.length() > 0) {
            // มี URL จาก admin → เก็บไว้ให้ otaLoop() จัดการ
            s_otaUrl     = url;
            s_otaVersion = ver;
            s_otaNotified = true;
            Serial.printf("[OTA] Update queued: v%s from %s\n",
                          ver.c_str(), url.c_str());
        } else {
            // ไม่มี URL → ใช้ auto-check แบบเดิม
            s_otaUrl      = "";
            s_otaVersion  = "";
            s_otaNotified = true;
            Serial.println("[OTA] Update queued: auto-check mode");
        }

    } else if (cmd == "check") {
        // เช็ค server เอง ไม่มี URL
        s_otaUrl      = "";
        s_otaVersion  = "";
        s_otaNotified = true;
        Serial.println("[OTA] Check queued");
    }

    return true;
}

// ============================================================
//  otaLoop — เรียกใน loop() ทุก iteration
// ============================================================
void otaLoop() {
    if (g_otaInProgress) return;

    unsigned long now = millis();

    if (s_otaNotified) {
        s_otaNotified  = false;
        s_lastOtaCheck = now;

        if (s_otaUrl.length() > 0) {
            // มี URL จาก MQTT admin → download โดยตรง ไม่ต้องเช็ค /version
            Serial.println("[OTA] Triggered by MQTT (direct URL)");
            String url = s_otaUrl;
            String ver = s_otaVersion;
            s_otaUrl     = "";
            s_otaVersion = "";
            otaDownloadFromUrl(url, ver);
        } else {
            // ไม่มี URL → auto-check server
            Serial.println("[OTA] Triggered by MQTT (auto-check)");
            otaCheckAndUpdate();
        }
        return;
    }

    // Auto-check ทุก 6 ชั่วโมง
    if (now - s_lastOtaCheck >= OTA_CHECK_INTERVAL_MS) {
        s_lastOtaCheck = now;
        Serial.println("[OTA] Auto-check (interval)");
        otaCheckAndUpdate();
    }
}