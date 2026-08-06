#define RXD2 18
#define TXD2 17

uint8_t buf[8]; 
int current_sensor = -1; 

// ตัวแปรสำหรับเก็บค่าล่าสุดของเซ็นเซอร์
int latest_humidity = 0;
int latest_temperature = 0;

// ตัวแปรสำหรับการหน่วงเวลา 10 วินาที (10000 มิลลิวินาที)
unsigned long previousMillis = 0;
const long interval = 10000; 

void setup() {
  Serial.begin(115200); 
  Serial2.begin(4800, SERIAL_8N1, RXD2, TXD2);
  
  Serial.println("=========================================");
  Serial.println("Dual Sensor Sniffer Ready!");
  Serial.println("Display format: Value / 10.0 (2 Decimal Places)");
  Serial.println("Update Interval: 10 Seconds");
  Serial.println("=========================================");
}

void loop() {
  // -----------------------------------------------------------
  // ส่วนที่ 1: จัดการการแสดงผลทุกๆ 10 วินาที
  // -----------------------------------------------------------
  unsigned long currentMillis = millis();
  if (currentMillis - previousMillis >= interval) {
    previousMillis = currentMillis; 
    
    // หารด้วย 10.0 และใส่ ", 2" เพื่อบังคับแสดงทศนิยม 2 ตำแหน่ง
    Serial.print("Humidity: ");
    Serial.print(latest_humidity / 10.0, 2); 
    Serial.print(" %RH  |  Temperature: ");
    Serial.print(latest_temperature / 10.0, 2);
    Serial.println(" C");
  }

  // -----------------------------------------------------------
  // ส่วนที่ 2: ดักฟังและอัปเดตค่าตลอดเวลา (ทำงานอยู่เบื้องหลัง)
  // -----------------------------------------------------------
  if (Serial2.available()) {
    uint8_t incomingByte = Serial2.read();
    
    for(int i = 0; i < 7; i++) {
      buf[i] = buf[i+1];
    }
    buf[7] = incomingByte;

    // ดักฟังคำสั่งจาก PLC (Request)
    if (buf[2] == 0x01 && buf[3] == 0x04 && buf[4] == 0x00 && buf[5] == 0x00 && buf[6] == 0x00 && buf[7] == 0x01) {
      current_sensor = 0; 
    }
    else if (buf[2] == 0x01 && buf[3] == 0x04 && buf[4] == 0x00 && buf[5] == 0x01 && buf[6] == 0x00 && buf[7] == 0x01) {
      current_sensor = 1; 
    }

    // ดักฟังคำตอบจาก Sensor (Response)
    else if (buf[3] == 0x01 && buf[4] == 0x04 && buf[5] == 0x02) {
      int sensorValue = (buf[6] << 8) | buf[7]; 
      
      if (current_sensor == 0) {
        latest_humidity = sensorValue; // อัปเดตค่าความชื้น
        current_sensor = -1; 
      } 
      else if (current_sensor == 1) {
        latest_temperature = sensorValue; // อัปเดตค่าอุณหภูมิ
        current_sensor = -1; 
      }
    }
  }
}