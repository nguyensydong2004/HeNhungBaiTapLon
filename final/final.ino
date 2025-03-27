#include <ESP8266WiFi.h>
#include <Firebase_ESP_Client.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <Servo.h>
#include <NTPClient.h>
#include <WiFiUdp.h>

// Config WiFi
#define WIFI_SSID "OPPO A54"
#define WIFI_PASSWORD "00000000"

// Firebase configuration
#define FIREBASE_HOST "henhung-3c03a-default-rtdb.firebaseio.com"
#define FIREBASE_AUTH "3rg9hgQ0sU1ynXF3CLw7H8M6S24NGB82cCMafNYk"

// Khởi tạo thiết bị
LiquidCrystal_I2C lcd(0x27, 16, 2);
Servo servo;
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", 7 * 3600);

// Firebase objects
FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;

// Chân kết nối
const int servoPin = D6;
const int trigPin = D5, echoPin = D7;
const int buttonPin = D3;

unsigned long lastFeedTimeAuto = 0;
const unsigned long feedCooldown = 60000;
int feedDuration = 2000;
int lowFoodThreshold = 20;
unsigned long lastFeedTime = 0;
bool lastConnectionStatus = false;
unsigned long lastManualFeedLog = 0; // Thêm biến để theo dõi lần ghi log cuối cùng

String getISODateTime() {
  time_t rawtime = timeClient.getEpochTime();
  struct tm *ti;
  ti = localtime(&rawtime);
  
  char buf[25];
  sprintf(buf, "%04d-%02d-%02dT%02d:%02d:%02d.000Z",
          ti->tm_year + 1900, ti->tm_mon + 1, ti->tm_mday,
          ti->tm_hour, ti->tm_min, ti->tm_sec);
  return String(buf);
}

void setup() {
  Serial.begin(115200);
  
  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("PetFeeder");
  lcd.setCursor(0, 1);
  lcd.print("Booting...");

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  Serial.print("Connected with IP: ");
  Serial.println(WiFi.localIP());

  timeClient.begin();
  timeClient.update();

  config.host = FIREBASE_HOST;
  config.signer.tokens.legacy_token = FIREBASE_AUTH;
  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);

  servo.attach(servoPin);
  servo.write(0);
  pinMode(trigPin, OUTPUT);
  pinMode(echoPin, INPUT);
  pinMode(buttonPin, INPUT_PULLUP);

  if (Firebase.RTDB.getInt(&fbdo, "/settings/feed_duration")) {
    feedDuration = fbdo.intData();
  }
  
  if (Firebase.RTDB.getInt(&fbdo, "/settings/low_food_threshold")) {
    lowFoodThreshold = fbdo.intData();
  }
}

void loop() {
  static unsigned long lastNTPUpdate = 0;
  if (millis() - lastNTPUpdate > 60000) {
    timeClient.update();
    lastNTPUpdate = millis();
  }

  bool currentStatus = (WiFi.status() == WL_CONNECTED);
  if (currentStatus != lastConnectionStatus) {
    Firebase.RTDB.setBool(&fbdo, "/sensors/connection_status", currentStatus);
    lastConnectionStatus = currentStatus;
  }

  readFirebaseControls();

  if (digitalRead(buttonPin) == LOW) {
    delay(3000);
    if (digitalRead(buttonPin) == LOW) {
      feedManual();
    }
  }

  checkAutoSchedule();
  updateSensorsAndDisplay();
  delay(1000);
}

void readFirebaseControls() {
  if (Firebase.RTDB.getBool(&fbdo, "/controls/manual_feed")) {
    if (fbdo.boolData()) {
      feedManual();
      Firebase.RTDB.setBool(&fbdo, "/controls/manual_feed", false);
    }
  }

  if (Firebase.RTDB.getBool(&fbdo, "/controls/restart")) {
    if (fbdo.boolData()) {
      Firebase.RTDB.setBool(&fbdo, "/controls/restart", false);
      ESP.restart();
    }
  }

  if (Firebase.RTDB.getInt(&fbdo, "/settings/feed_duration")) {
    feedDuration = fbdo.intData();
  }
  
  if (Firebase.RTDB.getInt(&fbdo, "/settings/low_food_threshold")) {
    lowFoodThreshold = fbdo.intData();
  }
}

void updateSensorsAndDisplay() {
  int foodLevel = readFoodLevel();
  Firebase.RTDB.setInt(&fbdo, "/sensors/food_level", foodLevel);
  
  if (foodLevel < lowFoodThreshold) {
    Firebase.RTDB.setString(&fbdo, "/display/lcd_line1", "LOW FOOD WARNING!");
  }
  
  if (millis() - lastFeedTime > 60000) {
    String currentTime = timeClient.getFormattedTime();
    Firebase.RTDB.setString(&fbdo, "/sensors/last_fed", currentTime);
    lastFeedTime = millis();
  }
  
  lcd.setCursor(0, 0);
  lcd.print("Food:" + String(foodLevel) + "% ");
  lcd.setCursor(0, 1);
  lcd.print(timeClient.getFormattedTime());
}

int readFoodLevel() {
  digitalWrite(trigPin, LOW);
  delayMicroseconds(2);
  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);
  
  long duration = pulseIn(echoPin, HIGH);
  float distance = duration * 0.034 / 2;
  int level = map(distance, 5, 20, 100, 0);
  return constrain(level, 0, 100);
}

void feedManual() {
  lcd.clear();
  lcd.print("Manual Feeding...");
  
  servo.write(90);
  delay(feedDuration);
  servo.write(0);
  
  // Chỉ ghi log nếu đã qua 60s kể từ lần ghi log trước
  if (millis() - lastManualFeedLog > 60000) {
    logFeeding("manual", 1);
    lastManualFeedLog = millis();
  }
  
  lcd.setCursor(0, 1);
  lcd.print("Done!           ");
  delay(2000);
}

void checkAutoSchedule() {
  static int lastCheckedMinute = -1;
  int currentMinute = timeClient.getMinutes();
  
  if (currentMinute == lastCheckedMinute) {
    return;
  }
  lastCheckedMinute = currentMinute;

  if (Firebase.RTDB.getJSON(&fbdo, "/schedule")) {
    FirebaseJson *json = fbdo.jsonObjectPtr();
    size_t count = json->iteratorBegin();

    int currentHour = timeClient.getHours();
    int currentDay = timeClient.getDay();

    for (size_t i = 0; i < count; i++) {
      String key, value;
      int type = 0;
      json->iteratorGet(i, type, key, value);

      FirebaseJson schedule;
      schedule.setJsonData(value);

      FirebaseJsonData result;
      int hour, minute, portion;
      bool enabled;

      schedule.get(result, "hour"); hour = result.intValue;
      schedule.get(result, "minute"); minute = result.intValue;
      schedule.get(result, "enabled"); enabled = result.boolValue;
      schedule.get(result, "portion"); portion = result.intValue;

      bool dayMatch = true;
      if (schedule.get(result, "days")) {
        FirebaseJsonArray daysArray;
        result.get<FirebaseJsonArray>(daysArray);
        dayMatch = false;

        for (size_t j = 0; j < daysArray.size(); j++) {
          FirebaseJsonData dayData;
          daysArray.get(dayData, j);
          if (dayData.intValue == currentDay) {
            dayMatch = true;
            break;
          }
        }
      }

      if (enabled && dayMatch && currentHour == hour && currentMinute == minute) {
        feedAuto(portion);
        logFeeding("schedule", portion);
        break;
      }
    }
    json->iteratorEnd();
  }
}

void feedAuto(int portion) {
  lcd.clear();
  lcd.print("Auto Feeding...");
  
  int totalDuration = feedDuration * portion;
  servo.write(90);
  delay(totalDuration);
  servo.write(0);
  
  lcd.setCursor(0, 1);
  lcd.print("Done!           ");
  delay(2000);
}

void logFeeding(String type, int portion) {
  // Kiểm tra xem bản ghi đã tồn tại chưa trước khi ghi
  String path = "/history/" + String(timeClient.getEpochTime());
  
  if (Firebase.RTDB.get(&fbdo, path)) {
    if (fbdo.dataType() == "null") { // Chỉ ghi nếu chưa tồn tại
      FirebaseJson json;
      json.set("timestamp", getISODateTime());
      json.set("type", type);
      json.set("portion", portion);
      json.set("status", "completed");
      
      if (type == "schedule") {
        json.set("schedule_id", String(millis()));
      }
      
      Firebase.RTDB.setJSON(&fbdo, path.c_str(), &json);
    }
  }
  
  // Luôn cập nhật thời gian cho ăn cuối cùng
  String currentTime = timeClient.getFormattedTime();
  Firebase.RTDB.setString(&fbdo, "/sensors/last_fed", currentTime);
}