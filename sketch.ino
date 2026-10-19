#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <ESP32Servo.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ctype.h>
#include <string.h>
#include <math.h>
#include "secrets.h"
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

Adafruit_SSD1306 display(
  SCREEN_WIDTH,
  SCREEN_HEIGHT,
  &Wire,
  -1
);

// =========================
// WIFI + THINGSPEAK
// =========================

const char* wifi_ssid = WIFI_SSID;
const char* wifi_password = WIFI_PASSWORD;

//API key for ThingSpeak connection 
const char* writeAPIKey  = THINGSPEAK_WRITE_API_KEY;


const unsigned long THINGSPEAK_INTERVAL = 20000;
unsigned long lastThingSpeakUpdate = 0;

//Telegram alert
const char* botToken  = TELEGRAM_BOT_TOKEN;
const char* chatId  = TELEGRAM_CHAT_ID;

const unsigned long WIFI_RECONNECT_INTERVAL = 10000;
unsigned long lastWiFiReconnectAttempt = 0;
bool wifiEverConnected = false;
bool wifiDisconnectPending = false;

// Theo doi timeout cam bien: 5 lan doc lien tiep (~2-4 giay tuy chu ky loop).
const uint8_t SENSOR_TIMEOUT_LIMIT = 5;
uint8_t sensorTimeoutCount[4] = {0, 0, 0, 0}; // slot 1-3, cong
bool sensorFault[4] = {false, false, false, false};

bool parkingFullStateInitialized = false;
bool lastParkingFullState = false;
// =========================
// HC-SR04 các slot đậu xe
// =========================

#define TRIG_1 5
#define ECHO_1 18

#define TRIG_2 19
#define ECHO_2 23

#define TRIG_3 16
#define ECHO_3 17

// =========================
// HC-SR04 CỔNG
// =========================

#define TRIG_GATE 32
#define ECHO_GATE 33

// =========================
// LED SLOT 1
// =========================

#define RED_1 26
#define GREEN_1 25

// =========================
// LED SLOT 2
// =========================

#define RED_2 14
#define GREEN_2 27

// =========================
// LED SLOT 3
// =========================

#define RED_3 13
#define GREEN_3 12

// =========================
// SERVO
// =========================

#define SERVO_PIN 4

Servo gateServo;

#define GATE_CLOSED 90
#define GATE_OPEN 0

// =========================
// KHOẢNG CÁCH PHÁT HIỆN XE
// =========================

#define OCCUPIED_DISTANCE 30.0f
#define GATE_DETECT_DISTANCE 20.0

// =========================
// THỜI GIAN ĐÓNG CỔNG
// =========================

const unsigned long GATE_CLOSE_DELAY = 3000;

unsigned long lastCarDetected = 0;

bool gateOpened = false;

//định nghĩa trạng thái chỗ đỗ
enum SlotState : uint8_t { SLOT_EMPTY = 0, SLOT_OCCUPIED = 1, SLOT_SENSOR_ERROR = 2 };

//lọc theo trạng thái nếu không hợp lệ thì loại bỏ
float getFilteredDistance(int trigPin, int echoPin);
SlotState classifySlot(float distance);
int slotStateToThingSpeak(SlotState state);


// ==================================================
// ĐỌC KHOẢNG CÁCH HC-SR04
// ==================================================

float getDistance(int trigPin, int echoPin) {

  digitalWrite(trigPin, LOW);
  delayMicroseconds(2);

  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);

  digitalWrite(trigPin, LOW);

  unsigned long duration = pulseIn(echoPin, HIGH, 30000UL);

  if (duration == 0) {
    return -1.0f; // Timeout: phép đo không hợp lệ
  }

  return duration * 0.0343f / 2.0f;
}

//lọc nhiễu từ cảm biến
//Hàm mới này thực hiện tối đa ba lần đo khoảng cách, lấy các kết quả hợp lệ rồi chọn trung vị (median)
float getFilteredDistance(int trigPin, int echoPin) {
  float values[3];
  int count = 0;
  for (int i = 0; i < 3; ++i) {
    float value = getDistance(trigPin, echoPin);
    if (isfinite(value) && value >= 0) values[count++] = value;
    if (i < 2) delay(25);
  }
  if (count == 0) return -1.0f;
  if (count == 1) return values[0];
  if (count == 2) return (values[0] + values[1]) / 2.0f;
  if (values[0] > values[1]) { float t = values[0]; values[0] = values[1]; values[1] = t; }
  if (values[1] > values[2]) { float t = values[1]; values[1] = values[2]; values[2] = t; }
  if (values[0] > values[1]) { float t = values[0]; values[0] = values[1]; values[1] = t; }
  return values[1];
}
//xác định trạng thái chỗ đỗ
SlotState classifySlot(float distance) {
  if (!isfinite(distance) || distance < 0) return SLOT_SENSOR_ERROR;
  return distance <= OCCUPIED_DISTANCE ? SLOT_OCCUPIED : SLOT_EMPTY;
}

int slotStateToThingSpeak(SlotState state) { return static_cast<int>(state); }

// ==================================================
// ĐIỀU KHIỂN LED
// ==================================================

void updateLED(int redPin,int greenPin,bool occupied) {

  if (occupied) {

    digitalWrite(redPin, HIGH);
    digitalWrite(greenPin, LOW);

  } else {

    digitalWrite(redPin, LOW);
    digitalWrite(greenPin, HIGH);
  }
}

//điều khiển LED theo trạng thái
void updateSlotLED(int redPin, int greenPin, SlotState state) {
  if (state == SLOT_OCCUPIED) {
    digitalWrite(redPin, HIGH); digitalWrite(greenPin, LOW);
  } else if (state == SLOT_EMPTY) {
    digitalWrite(redPin, LOW); digitalWrite(greenPin, HIGH);
  } else {
    digitalWrite(redPin, LOW); digitalWrite(greenPin, LOW);
  }
}

// ==================================================
// CẬP NHẬT OLED
// ==================================================

void updateOLED( // hiển thị lỗi cảm biến trên màn hình
  SlotState slot1,
  SlotState slot2,
  SlotState slot3,
  int available
) {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(25, 0);
  display.println("SMART PARKING");
  display.setCursor(0, 15);
  display.print("SLOT 1: ");
  display.println(slot1 == SLOT_SENSOR_ERROR ? "SENSOR ERROR" : (slot1 == SLOT_OCCUPIED ? "OCCUPIED" : "EMPTY"));
  display.setCursor(0, 28);
  display.print("SLOT 2: ");
  display.println(slot2 == SLOT_SENSOR_ERROR ? "SENSOR ERROR" : (slot2 == SLOT_OCCUPIED ? "OCCUPIED" : "EMPTY"));
  display.setCursor(0, 41);
  display.print("SLOT 3: ");
  display.println(slot3 == SLOT_SENSOR_ERROR ? "SENSOR ERROR" : (slot3 == SLOT_OCCUPIED ? "OCCUPIED" : "EMPTY"));
  display.setCursor(0, 54);
  display.print("AVAILABLE: ");
  display.print(available);
  display.println("/3");
  display.display();
}

// ==================================================
// ĐIỀU KHIỂN CỔNG SERVO
// ==================================================

void updateGate(float gateDistance,int availableCount) {

  if (!isfinite(gateDistance) || gateDistance < 0) {
    if (gateOpened) {
      gateServo.write(GATE_CLOSED);
      gateOpened = false;
    }
    Serial.println("GATE FAULT: Cảm biến lối vào không hợp lệ, cổng vẫn đóng");
    return;
  }

  bool carDetected = gateDistance <= GATE_DETECT_DISTANCE;

  // Xe tới và còn chỗ
  if (carDetected && availableCount > 0) {

    gateServo.write(GATE_OPEN);

    gateOpened = true;

    lastCarDetected = millis();

    Serial.println("CAR AT GATE");
    Serial.println("GATE OPEN");
  }

  // Nếu cổng đang mở
  if (gateOpened) {

    // Xe vẫn còn trước cổng
    if (carDetected) {

      lastCarDetected = millis();

    }

    // Xe đã đi qua
    else if (millis() - lastCarDetected >= GATE_CLOSE_DELAY) {

      gateServo.write(GATE_CLOSED);

      gateOpened = false;

      Serial.println("GATE CLOSE");
    }
  }

  // Hết chỗ
  if (carDetected && availableCount == 0) {

    gateServo.write(GATE_CLOSED);

    gateOpened = false;

    Serial.println("PARKING FULL");
    Serial.println("GATE LOCKED");
  }
}

// ==================================================
// TELEGRAM + WIFI + CANH BAO CAM BIEN
// ==================================================
bool telegramConfigured() {
  return strcmp(botToken, "YOUR_TELEGRAM_BOT_TOKEN") != 0 &&
         strcmp(chatId, "YOUR_TELEGRAM_CHAT_ID") != 0 &&
         strlen(botToken) > 10 &&
         strlen(chatId) > 0;
}

String urlEncode(const String& value) {
  const char hex[] = "0123456789ABCDEF";
  String encoded;
  for (size_t i = 0; i < value.length(); i++) {
    unsigned char c = (unsigned char)value[i];
    if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
      encoded += (char)c;
    } else {
      encoded += '%';
      encoded += hex[c >> 4];
      encoded += hex[c & 0x0F];
    }
  }
  return encoded;
}

bool sendTelegramMessage(const String& message) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Telegram: WiFi chưa kết nối, bỏ qua thông báo lúc này");
    return false;
  }
  if (!telegramConfigured()) {
    Serial.println("Telegram chưa cấu hình BOT_TOKEN / CHAT_ID");
    return false;
  }

  WiFiClientSecure client;
  client.setInsecure(); 
  HTTPClient http;
  http.setTimeout(5000);

  String url = "https://api.telegram.org/bot";
  url += TELEGRAM_BOT_TOKEN;
  url += "/sendMessage?chat_id=";
  url += urlEncode(String(TELEGRAM_CHAT_ID));
  url += "&text=";
  url += urlEncode(message);

  bool sent = false;
  if (http.begin(client, url)) {
    int httpCode = http.GET();
    String response = http.getString();
    if (httpCode == HTTP_CODE_OK && response.indexOf("\"ok\":true") >= 0) {
      Serial.println("Telegram: gửi thông báo thành công");
      sent = true;
    } else {
      Serial.print("Telegram HTTP error: ");
      Serial.println(httpCode);
      Serial.println(response);
    }
    http.end();
  }
  return sent;
}

void maintainWiFi() {
  if (WiFi.status() == WL_CONNECTED) {
    if (wifiDisconnectPending) {
      wifiDisconnectPending = false;
      sendTelegramMessage("Smart Parking: Wi-Fi đã được khôi phục.");
    }
    wifiEverConnected = true;
    return;
  }

  if (wifiEverConnected) {
    wifiDisconnectPending = true;
  }

  if (millis() - lastWiFiReconnectAttempt >= WIFI_RECONNECT_INTERVAL) {
    lastWiFiReconnectAttempt = millis();
    Serial.println("Wi-Fi mất kết nối, đang thử kết nối lại...");
    WiFi.reconnect();
  }
}

void updateSensorHealth(uint8_t sensorIndex, float distance, const char* sensorName) {
  if (distance >= 998.0) {
    if (sensorTimeoutCount[sensorIndex] < SENSOR_TIMEOUT_LIMIT) {
      sensorTimeoutCount[sensorIndex]++;
    }
    if (sensorTimeoutCount[sensorIndex] >= SENSOR_TIMEOUT_LIMIT && !sensorFault[sensorIndex]) {
      sensorFault[sensorIndex] = true;
      String message = "CẢNH BÁO Smart Parking: Cảm biến ";
      message += sensorName;
      message += " khôgn có phản hồi trong nhiều lần đọc liên tiếp. Vui lòng kiểm tra lại hệ thống.";
      sendTelegramMessage(message);
      Serial.println(message);
    }
  } else {
    sensorTimeoutCount[sensorIndex] = 0;
    if (sensorFault[sensorIndex]) {
      sensorFault[sensorIndex] = false;
      String message = "Smart Parking: Cảm biến ";
      message += sensorName;
      message += " đã hoạt động trở lại.";
      sendTelegramMessage(message);
      Serial.println(message);
    }
  }
}

void updateParkingFullAlert(int availableCount) {
  bool isFull = availableCount <= 0;
  if (!parkingFullStateInitialized) {
    parkingFullStateInitialized = true;
    lastParkingFullState = isFull;
    if (isFull) {
      sendTelegramMessage("CẢNH BÁO Smart Parking: Bãi đỗ đã đầy.");
    }
    return;
  }

  if (isFull != lastParkingFullState) {
    lastParkingFullState = isFull;
    if (isFull) {
      sendTelegramMessage("CẢNH BÁO Smart Parking: Bãi đỗ đã đầy.");
    } else {
      String message = "Smart Parking: Bãi đỗ đã có chỗ trống. Số chỗ trống: ";
      message += String(availableCount);
      message += "/3.";
      sendTelegramMessage(message);
    }
  }
}

// ==================================================
// GUI DU LIEU LEN THINGSPEAK
// ==================================================
void sendToThingSpeak(
  float distance1,
  float distance2,
  float distance3,
  SlotState slot1, 
  SlotState slot2,
  SlotState slot3,
  int availableCount,
  int occupiedCount,
  float gateDistance
) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("ThingSpeak: WiFi chua ket noi");
    return;
  }

  WiFiClientSecure client;
  client.setInsecure(); 
  HTTPClient http;

  int occupancyPercent = (occupiedCount * 100) / 3;
  int gateStatus = gateOpened ? 1 : 0;

  String url = "https://api.thingspeak.com/update?api_key=";
  url += writeAPIKey ;
  url += "&field1=" + String(slotStateToThingSpeak(slot1)); 
  url += "&field2=" + String(slotStateToThingSpeak(slot2)); 
  url += "&field3=" + String(slotStateToThingSpeak(slot3));
  url += "&field4=" + String(availableCount);
  url += "&field5=" + String(occupiedCount);
  url += "&field6=" + String(occupancyPercent);
  url += "&field7=" + String(gateStatus);
  url += "&field8=" + String(gateDistance, 1);

  if (http.begin(client, url)) {
    int httpCode = http.GET();
    String response = http.getString();
    if (httpCode == HTTP_CODE_OK) {
      long entryId = response.toInt();
      if (entryId > 0) {
        Serial.print("ThingSpeak update OK, entry ID: ");
        Serial.println(entryId);
      } else {
        Serial.println("ThingSpeak không cập nhật: Vui lòng kiểm tra Write API Key/chữ ký gửi");
      }
    } else {
      Serial.print("ThingSpeak HTTP error: ");
      Serial.println(httpCode);
    }
    http.end();
  } else {
    Serial.println("Không khởi tạo được kết nối ThingSpeak");
  }
}

// ==================================================
// SETUP
// ==================================================

void setup() {

  Serial.begin(115200);

  // -------------------------
  // WIFI
  // -------------------------
  // Ket noi Wi-Fi toi da 15 giay; that bai thi cac phan con lai van chay.
  WiFi.mode(WIFI_STA);
  WiFi.begin(wifi_ssid, wifi_password);

  Serial.print("Connecting to WiFi");
  unsigned long wifiStart = millis();

  while (WiFi.status() != WL_CONNECTED && millis() - wifiStart < 15000) {
    delay(250);
    Serial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi connected");
    Serial.print("IP: ");
    Serial.println(WiFi.localIP());

    wifiEverConnected = true;

  } else {
    Serial.println("\nWiFi connection timeout; sensor system will continue");
  }

  // -------------------------
  // HC-SR04 SLOT
  // -------------------------

  pinMode(TRIG_1, OUTPUT);
  pinMode(ECHO_1, INPUT);

  pinMode(TRIG_2, OUTPUT);
  pinMode(ECHO_2, INPUT);

  pinMode(TRIG_3, OUTPUT);
  pinMode(ECHO_3, INPUT);

  // -------------------------
  // HC-SR04 GATE
  // -------------------------

  pinMode(TRIG_GATE, OUTPUT);
  pinMode(ECHO_GATE, INPUT);

  // -------------------------
  // LED
  // -------------------------

  pinMode(RED_1, OUTPUT);
  pinMode(GREEN_1, OUTPUT);

  pinMode(RED_2, OUTPUT);
  pinMode(GREEN_2, OUTPUT);

  pinMode(RED_3, OUTPUT);
  pinMode(GREEN_3, OUTPUT);

  // -------------------------
  // SERVO
  // -------------------------

  gateServo.setPeriodHertz(50);

  gateServo.attach(
    SERVO_PIN,
    500,
    2400
  );

  gateServo.write(GATE_CLOSED);

  // -------------------------
  // OLED
  // -------------------------

  Wire.begin(21, 22);

  if (!display.begin(
    SSD1306_SWITCHCAPVCC,
    0x3C
  )) {

    Serial.println("OLED not found!");

    while (true);
  }

  // -------------------------
  // LED BAN ĐẦU
  // -------------------------

  digitalWrite(GREEN_1, HIGH);
  digitalWrite(GREEN_2, HIGH);
  digitalWrite(GREEN_3, HIGH);

  // -------------------------
  // OLED BAN ĐẦU
  // -------------------------

  display.clearDisplay();

  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);

  display.setCursor(25, 0);
  display.println("PARKINGSENSE NEXUS");

  display.setCursor(0, 20);
  display.println("System starting...");

  display.display();

  delay(1000);
}

// ==================================================
// LOOP
// ==================================================

void loop() {

  maintainWiFi();
  // -------------------------
  // ĐỌC 3 SLOT
  // -------------------------

  float distance1 =
    getDistance(TRIG_1, ECHO_1);

  delay(50);

  float distance2 =
    getDistance(TRIG_2, ECHO_2);

  delay(50);

  float distance3 =
    getDistance(TRIG_3, ECHO_3);

  // -------------------------
  // XÁC ĐỊNH SLOT
  // -------------------------

  SlotState slot1 = classifySlot(distance1);
  SlotState slot2 = classifySlot(distance2);
  SlotState slot3 = classifySlot(distance3);
  bool occupied1 = slot1 == SLOT_OCCUPIED;
  bool occupied2 = slot2 == SLOT_OCCUPIED;
  bool occupied3 = slot3 == SLOT_OCCUPIED;
  
  // -------------------------
  // LED
  // -------------------------
  //Red=occupied, green=empty, both off=sensor error
  updateSlotLED(RED_1, GREEN_1, slot1);
  updateSlotLED(RED_2, GREEN_2, slot2);
  updateSlotLED(RED_3, GREEN_3, slot3);
  
 
  // -------------------------
  // ĐẾM SLOT
  // -------------------------

  int occupiedCount = (slot1 == SLOT_OCCUPIED) + (slot2 == SLOT_OCCUPIED) + (slot3 == SLOT_OCCUPIED);
  int availableCount = (slot1 == SLOT_EMPTY) + (slot2 == SLOT_EMPTY) + (slot3 == SLOT_EMPTY);

  // -------------------------
  // OLED
  // -------------------------

  updateOLED(slot1, slot2, slot3, availableCount);

  // -------------------------
  // ĐỌC CẢM BIẾN CỔNG
  // -------------------------

  float gateDistance = getFilteredDistance(TRIG_GATE, ECHO_GATE);

  // Canh bao timeout cam bien neu khong co phan hoi 5 lan doc lien tiep.
  updateSensorHealth(0, distance1, "Slot 1");
  updateSensorHealth(1, distance2, "Slot 2");
  updateSensorHealth(2, distance3, "Slot 3");
  updateSensorHealth(3, gateDistance, "cong vao");
  updateParkingFullAlert(availableCount);

  // -------------------------
  // ĐIỀU KHIỂN CỔNG
  // -------------------------

  updateGate(
    gateDistance,
    availableCount
  );

  // -------------------------
  // SERIAL MONITOR
  // -------------------------

  Serial.println(
    "----------------------------"
  );

  Serial.print("Slot 1: ");
  Serial.print(distance1);
  Serial.print(" cm -> ");
  Serial.println(
    occupied1 ? "OCCUPIED" : "EMPTY"
  );

  Serial.print("Slot 2: ");
  Serial.print(distance2);
  Serial.print(" cm -> ");
  Serial.println(
    occupied2 ? "OCCUPIED" : "EMPTY"
  );

  Serial.print("Slot 3: ");
  Serial.print(distance3);
  Serial.print(" cm -> ");
  Serial.println(
    occupied3 ? "OCCUPIED" : "EMPTY"
  );

  Serial.print("Available: ");
  Serial.print(availableCount);
  Serial.println("/3");

  Serial.print("Gate distance: ");
  Serial.print(gateDistance);
  Serial.println(" cm");


  if (millis() - lastThingSpeakUpdate >= THINGSPEAK_INTERVAL) {
    lastThingSpeakUpdate = millis();

    sendToThingSpeak(
      distance1,
      distance2,
      distance3,
      slot1, // [FIX-1] Send three-state slot status.
      slot2,
      slot3,
      availableCount,
      occupiedCount,
      gateDistance
    );
  }
  delay(500);
}