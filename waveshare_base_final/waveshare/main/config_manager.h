#pragma once
#include <Preferences.h>

// ============================================================
//  config_manager.h  — Waveshare ESP32-S3-Relay-6CH
// ============================================================

struct DeviceConfig {
    char wifi_ssid[64];    // WiFi SSID
    char wifi_pass[64];    // WiFi Password
    char mqtt_server[64];
    int  mqtt_port;
    char mqtt_user[32];
    char mqtt_pass[32];
    int  send_interval;     // ส่ง telemetry ทุกกี่วินาที (default 30)
    int  sensor_interval;   // อ่าน RS485 sensor ทุกกี่วินาที (default 60)

    // ── RS485 Soil Sensor ──────────────────────────────────
    int  soil_slave_id;     // Modbus Slave ID (default 1)
    int  soil_baud;         // Baud Rate (default 4800)
    int  soil_scale;        // Scale factor ÷ (default 10)
    int  reg_soil_temp;     // register ดินอุณหภูมิ (default 0)
    int  reg_soil_humid;    // register ความชื้นดิน (default 1)
    int  reg_soil_ph;       // register pH (default 3, -1=ไม่มี)
    int  reg_soil_ec;       // register EC (default 9, -1=ไม่มี)
    int  reg_soil_n;        // register N (default 16, -1=ไม่มี)
    int  reg_soil_p;        // register P (default 18, -1=ไม่มี)
    int  reg_soil_k;        // register K (default 19, -1=ไม่มี)

    // ── RS485 Air Sensor ───────────────────────────────────
    int  air_slave_id;      // Modbus Slave ID (default 2)
    int  air_baud;          // Baud Rate (default 4800)
    int  air_scale;         // Scale factor ÷ (default 10)
    int  reg_air_humid;     // register ความชื้นอากาศ (default 0)
    int  reg_air_temp;      // register อุณหภูมิอากาศ (default 1)
};

DeviceConfig defaultConfig() {
    DeviceConfig cfg;
    strncpy(cfg.wifi_ssid, "", sizeof(cfg.wifi_ssid));
    strncpy(cfg.wifi_pass, "", sizeof(cfg.wifi_pass));
    strncpy(cfg.mqtt_server, "147.50.255.133", sizeof(cfg.mqtt_server));
    cfg.mqtt_port       = 1883;
    strncpy(cfg.mqtt_user, "meetech",     sizeof(cfg.mqtt_user));
    strncpy(cfg.mqtt_pass, "meetech2026", sizeof(cfg.mqtt_pass));
    cfg.send_interval   = 30;
    cfg.sensor_interval = 60;

    cfg.soil_slave_id   = 1;
    cfg.soil_baud       = 4800;
    cfg.soil_scale      = 10;
    cfg.reg_soil_temp   = 0;
    cfg.reg_soil_humid  = 1;
    cfg.reg_soil_ph     = 3;
    cfg.reg_soil_ec     = 9;
    cfg.reg_soil_n      = 16;
    cfg.reg_soil_p      = 18;
    cfg.reg_soil_k      = 19;

    cfg.air_slave_id    = 2;
    cfg.air_baud        = 4800;
    cfg.air_scale       = 10;
    cfg.reg_air_humid   = 0;
    cfg.reg_air_temp    = 1;
    return cfg;
}

void saveConfig(DeviceConfig& cfg) {
    Preferences prefs;
    prefs.begin("wcfg", false);
    prefs.putString("wifi_ssid",     cfg.wifi_ssid);
    prefs.putString("wifi_pass",     cfg.wifi_pass);
    prefs.putString("mqtt_server",    cfg.mqtt_server);
    prefs.putInt   ("mqtt_port",      cfg.mqtt_port);
    prefs.putString("mqtt_user",      cfg.mqtt_user);
    prefs.putString("mqtt_pass",      cfg.mqtt_pass);
    prefs.putInt   ("send_interval",  cfg.send_interval);
    prefs.putInt   ("sensor_interval",cfg.sensor_interval);

    prefs.putInt("soil_slave_id",  cfg.soil_slave_id);
    prefs.putInt("soil_baud",      cfg.soil_baud);
    prefs.putInt("soil_scale",     cfg.soil_scale);
    prefs.putInt("reg_s_temp",     cfg.reg_soil_temp);
    prefs.putInt("reg_s_humid",    cfg.reg_soil_humid);
    prefs.putInt("reg_s_ph",       cfg.reg_soil_ph);
    prefs.putInt("reg_s_ec",       cfg.reg_soil_ec);
    prefs.putInt("reg_s_n",        cfg.reg_soil_n);
    prefs.putInt("reg_s_p",        cfg.reg_soil_p);
    prefs.putInt("reg_s_k",        cfg.reg_soil_k);

    prefs.putInt("air_slave_id",   cfg.air_slave_id);
    prefs.putInt("air_baud",       cfg.air_baud);
    prefs.putInt("air_scale",      cfg.air_scale);
    prefs.putInt("reg_a_humid",    cfg.reg_air_humid);
    prefs.putInt("reg_a_temp",     cfg.reg_air_temp);

    prefs.putBool("configured", true);
    prefs.end();
    Serial.println("[Config] Saved");
}

DeviceConfig loadConfig() {
    Preferences prefs;
    prefs.begin("wcfg", true);
    bool configured = prefs.getBool("configured", false);
    if (!configured) {
        prefs.end();
        Serial.println("[Config] Using defaults");
        return defaultConfig();
    }
    DeviceConfig cfg;
    strncpy(cfg.wifi_ssid, prefs.getString("wifi_ssid", "").c_str(), sizeof(cfg.wifi_ssid));
    strncpy(cfg.wifi_pass, prefs.getString("wifi_pass", "").c_str(), sizeof(cfg.wifi_pass));
    strncpy(cfg.mqtt_server, prefs.getString("mqtt_server", "147.50.255.133").c_str(), sizeof(cfg.mqtt_server));
    cfg.mqtt_port       = prefs.getInt("mqtt_port",       1883);
    strncpy(cfg.mqtt_user, prefs.getString("mqtt_user", "meetech").c_str(),     sizeof(cfg.mqtt_user));
    strncpy(cfg.mqtt_pass, prefs.getString("mqtt_pass", "meetech2026").c_str(), sizeof(cfg.mqtt_pass));
    cfg.send_interval   = prefs.getInt("send_interval",   30);
    cfg.sensor_interval = prefs.getInt("sensor_interval", 60);

    cfg.soil_slave_id   = prefs.getInt("soil_slave_id",  1);
    cfg.soil_baud       = prefs.getInt("soil_baud",      4800);
    cfg.soil_scale      = prefs.getInt("soil_scale",     10);
    cfg.reg_soil_temp   = prefs.getInt("reg_s_temp",     0);
    cfg.reg_soil_humid  = prefs.getInt("reg_s_humid",    1);
    cfg.reg_soil_ph     = prefs.getInt("reg_s_ph",       3);
    cfg.reg_soil_ec     = prefs.getInt("reg_s_ec",       9);
    cfg.reg_soil_n      = prefs.getInt("reg_s_n",        16);
    cfg.reg_soil_p      = prefs.getInt("reg_s_p",        18);
    cfg.reg_soil_k      = prefs.getInt("reg_s_k",        19);

    cfg.air_slave_id    = prefs.getInt("air_slave_id",   2);
    cfg.air_baud        = prefs.getInt("air_baud",       4800);
    cfg.air_scale       = prefs.getInt("air_scale",      10);
    cfg.reg_air_humid   = prefs.getInt("reg_a_humid",    0);
    cfg.reg_air_temp    = prefs.getInt("reg_a_temp",     1);

    prefs.end();
    Serial.println("[Config] Loaded");
    return cfg;
}

bool isConfigured() {
    Preferences prefs;
    prefs.begin("wcfg", true);
    bool cfg = prefs.getBool("configured", false);
    prefs.end();
    return cfg;
}

void clearConfig() {
    Preferences prefs;
    prefs.begin("wcfg", false);
    prefs.clear();
    prefs.end();
    Serial.println("[Config] Cleared");
}
