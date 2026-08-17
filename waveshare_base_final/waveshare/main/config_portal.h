#pragma once
#include <WiFi.h>
#include <WebServer.h>
#include "config_manager.h"

// ============================================================
//  config_portal.h  — Waveshare ESP32-S3-Relay-6CH
//  AP: Waveshare-Setup / 12345678
//  URL: http://192.168.4.1
// ============================================================

WebServer* portalServer = nullptr;

const char CONFIG_HTML[] PROGMEM = R"rawhtml(
<!DOCTYPE html>
<html lang="th">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>Waveshare Config</title>
<style>
* { box-sizing: border-box; margin: 0; padding: 0; }
body { font-family: sans-serif; background: #0f172a; color: #e2e8f0; padding: 16px; }
h1 { text-align: center; color: #10b981; margin-bottom: 20px; font-size: 20px; }
h2 { color: #94a3b8; font-size: 13px; margin: 16px 0 8px; border-bottom: 1px solid #334155; padding-bottom: 6px; }
.card { background: #1e293b; border-radius: 12px; padding: 16px; margin-bottom: 12px; }
label { display: block; font-size: 12px; color: #94a3b8; margin: 8px 0 3px; }
input[type=text], input[type=password], input[type=number], select {
  width: 100%; padding: 10px; border-radius: 8px;
  background: #334155; border: 1px solid #475569;
  color: #e2e8f0; font-size: 14px; }
.grid2 { display: grid; grid-template-columns: 1fr 1fr; gap: 10px; }
.grid3 { display: grid; grid-template-columns: 1fr 1fr 1fr; gap: 8px; }
.note { font-size: 11px; color: #64748b; margin-top: 6px; }
button { width: 100%; padding: 14px; border-radius: 10px; border: none;
  background: #10b981; color: white; font-size: 16px; font-weight: bold;
  cursor: pointer; margin-top: 16px; }
#success { background:#065f46; color:#6ee7b7; padding:20px; border-radius:12px;
  text-align:center; font-size:16px; display:none; margin-top:16px; }
</style>
</head>
<body>
<h1>Waveshare Smart Farm</h1>
<form id="frm" onsubmit="save(event)">

  <div class="card">
    <h2>WiFi</h2>
    <label>WiFi SSID</label>
    <input type="text" name="wifi_ssid" placeholder="ชื่อ WiFi">
    <label>WiFi Password</label>
    <input type="password" name="wifi_pass" placeholder="รหัส WiFi">
  </div>

  <div class="card">
    <h2>MQTT Server</h2>
    <label>Server IP / Domain</label>
    <input type="text" name="mqtt_server" value="147.50.255.133">
    <div class="grid2">
      <div><label>Port</label><input type="number" name="mqtt_port" value="1883"></div>
      <div><label>ส่งทุก (วินาที)</label><input type="number" name="send_interval" value="30" min="10" max="300"></div>
    </div>
    <label>Username</label>
    <input type="text" name="mqtt_user" value="meetech">
    <label>Password</label>
    <input type="password" name="mqtt_pass" value="meetech2026">
  </div>

  <div class="card">
    <h2>Sensor ดิน (RS485 Soil)</h2>
    <div class="grid2">
      <div>
        <label>Slave ID</label>
        <input type="number" name="soil_slave_id" value="1" min="1" max="247">
      </div>
      <div>
        <label>Baud Rate</label>
        <select name="soil_baud">
          <option value="1200">1200</option>
          <option value="2400">2400</option>
          <option value="4800" selected>4800</option>
          <option value="9600">9600</option>
          <option value="19200">19200</option>
        </select>
      </div>
    </div>
    <label>Scale Factor (หารค่า)</label>
    <select name="soil_scale">
      <option value="1">÷1 (ค่าตรง)</option>
      <option value="10" selected>÷10 (เช่น 523→52.3)</option>
      <option value="100">÷100</option>
    </select>
    <p class="note">Register Address — ใส่ -1 ถ้าไม่มีค่านี้</p>
    <div class="grid3">
      <div><label>อุณหภูมิดิน</label><input type="number" name="reg_soil_temp"  value="0"  min="-1"></div>
      <div><label>ความชื้นดิน</label><input type="number" name="reg_soil_humid" value="1"  min="-1"></div>
      <div><label>pH</label>          <input type="number" name="reg_soil_ph"    value="3"  min="-1"></div>
      <div><label>EC</label>          <input type="number" name="reg_soil_ec"    value="9"  min="-1"></div>
      <div><label>N</label>           <input type="number" name="reg_soil_n"     value="16" min="-1"></div>
      <div><label>P</label>           <input type="number" name="reg_soil_p"     value="18" min="-1"></div>
      <div><label>K</label>           <input type="number" name="reg_soil_k"     value="19" min="-1"></div>
    </div>
  </div>

  <div class="card">
    <h2>Sensor อากาศ (RS485 Air)</h2>
    <div class="grid2">
      <div>
        <label>Slave ID</label>
        <input type="number" name="air_slave_id" value="2" min="1" max="247">
      </div>
      <div>
        <label>Baud Rate</label>
        <select name="air_baud">
          <option value="1200">1200</option>
          <option value="2400">2400</option>
          <option value="4800" selected>4800</option>
          <option value="9600">9600</option>
          <option value="19200">19200</option>
        </select>
      </div>
    </div>
    <label>Scale Factor (หารค่า)</label>
    <select name="air_scale">
      <option value="1">÷1 (ค่าตรง)</option>
      <option value="10" selected>÷10 (เช่น 256→25.6)</option>
      <option value="100">÷100</option>
    </select>
    <p class="note">Register Address</p>
    <div class="grid2">
      <div><label>ความชื้นอากาศ</label><input type="number" name="reg_air_humid" value="0" min="0"></div>
      <div><label>อุณหภูมิอากาศ</label><input type="number" name="reg_air_temp"  value="1" min="0"></div>
    </div>
  </div>

  <div class="card">
    <h2>ระยะเวลาอ่าน Sensor</h2>
    <label>อ่าน Sensor ทุก (วินาที)</label>
    <input type="number" name="sensor_interval" value="60" min="10" max="3600">
  </div>

  <button type="submit">บันทึกและ Restart</button>
</form>
<div id="success">บันทึกแล้ว! กำลัง Restart...</div>
<script>
function save(e) {
  e.preventDefault();
  fetch('/save', { method:'POST', body: new URLSearchParams(new FormData(document.getElementById('frm'))) })
    .then(() => { document.getElementById('frm').style.display='none'; document.getElementById('success').style.display='block'; });
}
</script>
</body>
</html>
)rawhtml";

void startConfigPortal() {
    portalServer = new WebServer(80);
    Serial.println("[Portal] Starting AP: Waveshare-Setup / 12345678");
    WiFi.mode(WIFI_AP);
    WiFi.softAP("Waveshare-Setup", "12345678");
    Serial.print("[Portal] IP: ");
    Serial.println(WiFi.softAPIP());

    portalServer->on("/", HTTP_GET, []() {
        portalServer->send(200, "text/html; charset=utf-8", FPSTR(CONFIG_HTML));
    });

    portalServer->on("/save", HTTP_POST, []() {
        DeviceConfig cfg;

        strncpy(cfg.wifi_ssid, portalServer->arg("wifi_ssid").c_str(), sizeof(cfg.wifi_ssid) - 1);
        cfg.wifi_ssid[sizeof(cfg.wifi_ssid) - 1] = '\0';

        String newWifiPass = portalServer->arg("wifi_pass");
        if (newWifiPass.length() > 0) {
            strncpy(cfg.wifi_pass, newWifiPass.c_str(), sizeof(cfg.wifi_pass) - 1);
            cfg.wifi_pass[sizeof(cfg.wifi_pass) - 1] = '\0';
        } else {
            DeviceConfig old = loadConfig();
            strncpy(cfg.wifi_pass, old.wifi_pass, sizeof(cfg.wifi_pass) - 1);
            cfg.wifi_pass[sizeof(cfg.wifi_pass) - 1] = '\0';
        }

        strncpy(cfg.mqtt_server, portalServer->arg("mqtt_server").c_str(), sizeof(cfg.mqtt_server) - 1);
        cfg.mqtt_server[sizeof(cfg.mqtt_server) - 1] = '\0';

        cfg.mqtt_port = portalServer->arg("mqtt_port").toInt();

        strncpy(cfg.mqtt_user, portalServer->arg("mqtt_user").c_str(), sizeof(cfg.mqtt_user) - 1);
        cfg.mqtt_user[sizeof(cfg.mqtt_user) - 1] = '\0';

        String newPass = portalServer->arg("mqtt_pass");
        if (newPass.length() > 0) {
            strncpy(cfg.mqtt_pass, newPass.c_str(), sizeof(cfg.mqtt_pass) - 1);
            cfg.mqtt_pass[sizeof(cfg.mqtt_pass) - 1] = '\0';
        } else {
            DeviceConfig old = loadConfig();
            strncpy(cfg.mqtt_pass, old.mqtt_pass, sizeof(cfg.mqtt_pass) - 1);
            cfg.mqtt_pass[sizeof(cfg.mqtt_pass) - 1] = '\0';
        }

        cfg.send_interval   = portalServer->arg("send_interval").toInt();
        cfg.sensor_interval = portalServer->arg("sensor_interval").toInt();

        cfg.soil_slave_id   = portalServer->arg("soil_slave_id").toInt();
        cfg.soil_baud       = portalServer->arg("soil_baud").toInt();
        cfg.soil_scale      = portalServer->arg("soil_scale").toInt();
        cfg.reg_soil_temp   = portalServer->arg("reg_soil_temp").toInt();
        cfg.reg_soil_humid  = portalServer->arg("reg_soil_humid").toInt();
        cfg.reg_soil_ph     = portalServer->arg("reg_soil_ph").toInt();
        cfg.reg_soil_ec     = portalServer->arg("reg_soil_ec").toInt();
        cfg.reg_soil_n      = portalServer->arg("reg_soil_n").toInt();
        cfg.reg_soil_p      = portalServer->arg("reg_soil_p").toInt();
        cfg.reg_soil_k      = portalServer->arg("reg_soil_k").toInt();

        cfg.air_slave_id    = portalServer->arg("air_slave_id").toInt();
        cfg.air_baud        = portalServer->arg("air_baud").toInt();
        cfg.air_scale       = portalServer->arg("air_scale").toInt();
        cfg.reg_air_humid   = portalServer->arg("reg_air_humid").toInt();
        cfg.reg_air_temp    = portalServer->arg("reg_air_temp").toInt();

        saveConfig(cfg);
        portalServer->send(200, "text/plain", "OK");
        delay(2000);
        ESP.restart();
    });

    portalServer->begin();
    Serial.println("[Portal] Ready at http://192.168.4.1");
}

void handlePortal() {
    portalServer->handleClient();
}
