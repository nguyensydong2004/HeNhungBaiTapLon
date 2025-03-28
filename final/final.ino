#include <ESP8266WiFi.h>
#include <Firebase_ESP_Client.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <Servo.h>
#include <WiFiUdp.h>
#include <RTClib.h>
#include <EEPROM.h>
#include <NTPClient.h>

String wifiSSID = "";
String wifiPassword = "";

#define FIREBASE_HOST "henhung-3c03a-default-rtdb.firebaseio.com"
#define FIREBASE_AUTH "3rg9hgQ0sU1ynXF3CLw7H8M6S24NGB82cCMafNYk"

LiquidCrystal_I2C lcd(0x27, 16, 2);
Servo servo;
RTC_DS3231 rtc;
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "vn.pool.ntp.org", 7 * 3600);

FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;

const int servoPin = D6;
const int trigPin = D5, echoPin = D7;
const int buttonPin = D3;

struct EEPROMSettings {
  int feedDuration;
  int lowFoodThreshold;
  uint32_t lastWriteTime;
  uint16_t writeCount;
  uint8_t initialized;
  char wifiSSID[32];
  char wifiPassword[64];
};


EEPROMSettings eepromSettings;
unsigned long lastFeedTime = 0;
bool lastConnectionStatus = false;
bool rtcAvailable = false;
bool firebaseReady = false;

void setup() {
  Serial.begin(115200);
  
  lcd.init();
  lcd.backlight();
  showLCDMessage("PetFeeder", "Booting...");

  EEPROM.begin(sizeof(EEPROMSettings));
  loadEEPROMSettings();

  wifiSSID = String(eepromSettings.wifiSSID);
  wifiPassword = String(eepromSettings.wifiPassword);

  connectWiFi();

  initRTC();

  timeClient.begin();
  updateNetworkTime();

  initFirebase();

  initHardware();

  syncSettingsFromFirebase();
}

void loop() {
  static unsigned long lastTimeSync = 0;
  static unsigned long lastFirebaseCheck = 0;
  static unsigned long lastWifiCheck = 0;
  
  if (millis() - lastTimeSync > 3600000) {
    updateNetworkTime();
    lastTimeSync = millis();
  }

  if (millis() - lastFirebaseCheck > 60000) {
    firebaseReady = Firebase.ready();
    lastFirebaseCheck = millis();
  }

  if (millis() - lastWifiCheck > 60000) {
    checkWiFiConfigUpdate();
    lastWifiCheck = millis();
  }

  checkWiFiConnection();

  if (firebaseReady) {
    readFirebaseControls();
  }

  checkManualButton();

  checkAutoSchedule();
  
  updateSensorsAndDisplay();
  
  delay(100);
}


void showLCDMessage(String line1, String line2) {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(line1);
  lcd.setCursor(0, 1);
  lcd.print(line2);

  if (firebaseReady) {
    FirebaseJson displayJson;
    displayJson.set("lcd_line1", line1);
    displayJson.set("lcd_line2", line2);
    displayJson.set("last_updated", getISODateTime());
    Firebase.RTDB.setJSON(&fbdo, "/display", &displayJson);
  }
}

void connectWiFi() {
  showLCDMessage("Connecting to", wifiSSID);
  Serial.print("Connecting to WiFi: ");
  Serial.println(wifiSSID);
  
  WiFi.begin(wifiSSID.c_str(), wifiPassword.c_str());
  
  unsigned long startTime = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startTime < 20000) {
    delay(500);
    Serial.print(".");
  }
  
  if (WiFi.status() != WL_CONNECTED) {
    showLCDMessage("WiFi Failed", "Use Button");
    Serial.println("\nWiFi connection failed");
    return;
  }
  
  Serial.println("\nConnected! IP: " + WiFi.localIP().toString());
  showLCDMessage("Connected", WiFi.localIP().toString());
  delay(1000);
  
  if (firebaseReady) {
    Firebase.RTDB.setBool(&fbdo, "/sensors/connection_status", true);
  }
}

void saveWiFiConfig() {
  strncpy(eepromSettings.wifiSSID, wifiSSID.c_str(), sizeof(eepromSettings.wifiSSID)-1);
  strncpy(eepromSettings.wifiPassword, wifiPassword.c_str(), sizeof(eepromSettings.wifiPassword)-1);
  saveEEPROMSettings();
}

void checkWiFiConfigUpdate() {
  if (!firebaseReady) return;

  if (Firebase.RTDB.getJSON(&fbdo, "/settings/wifi")) {
    FirebaseJson wifiConfig;
    wifiConfig.setJsonData(fbdo.jsonString());
    
    FirebaseJsonData ssidData, passData;
    wifiConfig.get(ssidData, "ssid");
    wifiConfig.get(passData, "password");
    
    String newSSID = ssidData.stringValue;
    String newPass = passData.stringValue;
    
    if (newSSID.length() > 0 && newPass.length() > 0 && 
        (newSSID != wifiSSID || newPass != wifiPassword)) {
      wifiSSID = newSSID;
      wifiPassword = newPass;
      saveWiFiConfig();
      
      Serial.println("Updating WiFi config...");
      WiFi.begin(wifiSSID.c_str(), wifiPassword.c_str());
    }
  }
}

void initRTC() {
  if (rtc.begin()) {
    rtcAvailable = true;
    if (rtc.lostPower()) {
      Serial.println("RTC lost power, syncing from NTP");
      if (updateNetworkTime()) {
        rtc.adjust(DateTime(timeClient.getEpochTime()));
      }
    }
    updateRTCStatus();
  } else {
    Serial.println("Couldn't find RTC");
  }
}

bool updateNetworkTime() {
  if (WiFi.status() != WL_CONNECTED) return false;
  
  if (!timeClient.update()) {
    Serial.println("Failed to update time from NTP");
    return false;
  }
  
  if (rtcAvailable) {
    rtc.adjust(DateTime(timeClient.getEpochTime()));
  }
  return true;
}

void initFirebase() {
  config.host = FIREBASE_HOST;
  config.signer.tokens.legacy_token = FIREBASE_AUTH;
  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);
  firebaseReady = Firebase.ready();
}

void initHardware() {
  if (!servo.attach(servoPin)) {
    Serial.println("Failed to attach servo");
  }
  servo.write(0);
  
  pinMode(trigPin, OUTPUT);
  pinMode(echoPin, INPUT);
  pinMode(buttonPin, INPUT_PULLUP);
}

void loadEEPROMSettings() {
  EEPROM.get(0, eepromSettings);
  
  if (eepromSettings.initialized != 0xAA) {
    eepromSettings.feedDuration = 2000;
    eepromSettings.lowFoodThreshold = 20;
    eepromSettings.lastWriteTime = 0;
    eepromSettings.writeCount = 0;
    eepromSettings.initialized = 0xAA;
    strcpy(eepromSettings.wifiSSID, "OPPO A54");
    strcpy(eepromSettings.wifiPassword, "00000000");
    saveEEPROMSettings();
  }
}

void saveEEPROMSettings() {
  eepromSettings.writeCount++;
  eepromSettings.lastWriteTime = getCurrentUnixTime();
  EEPROM.put(0, eepromSettings);
  
  if (!EEPROM.commit()) {
    Serial.println("EEPROM commit failed");
  } else {
    updateEEPROMStatus();
  }
}

uint32_t getCurrentUnixTime() {
  if (rtcAvailable) {
    return rtc.now().unixtime();
  }
  return timeClient.getEpochTime();
}

String getISODateTime() {
  DateTime now;
  if (rtcAvailable) {
    now = rtc.now();
  } else if (timeClient.isTimeSet()) {
    now = DateTime(timeClient.getEpochTime());
  } else {
    return "";
  }
  
  char buf[35];
  sprintf(buf, "%04d-%02d-%02dT%02d:%02d:%02d.000+07:00",
        now.year(), now.month(), now.day(),
        now.hour(), now.minute(), now.second());
  return String(buf);
  }


void checkWiFiConnection() {
  bool currentStatus = (WiFi.status() == WL_CONNECTED);
  
  if (currentStatus != lastConnectionStatus) {
    if (firebaseReady) {
      Firebase.RTDB.setBool(&fbdo, "/sensors/connection_status", currentStatus);
    }
    
    if (!currentStatus) {
      showLCDMessage("WiFi Disconnected", "Use Button");
      connectWiFi();
    }
    
    lastConnectionStatus = currentStatus;
  }
}

void syncSettingsFromFirebase() {
  if (!firebaseReady) return;

  if (Firebase.RTDB.getInt(&fbdo, "/settings/feed_duration")) {
    int newDuration = fbdo.intData();
    if (eepromSettings.feedDuration != newDuration) {
      eepromSettings.feedDuration = newDuration;
      saveEEPROMSettings();
      Serial.println("Updated feed duration: " + String(newDuration));
    }
  }
  
  if (Firebase.RTDB.getInt(&fbdo, "/settings/low_food_threshold")) {
    int newThreshold = fbdo.intData();
    if (eepromSettings.lowFoodThreshold != newThreshold) {
      eepromSettings.lowFoodThreshold = newThreshold;
      saveEEPROMSettings();
      Serial.println("Updated food threshold: " + String(newThreshold));
    }
  }
}

void updateRTCStatus() {
  if (!rtcAvailable || !firebaseReady) return;
  
  FirebaseJson rtcJson;
  rtcJson.set("timestamp", getISODateTime());
  rtcJson.set("timezone", "UTC+7");
  rtcJson.set("battery_backup", !rtc.lostPower());
  rtcJson.set("last_sync", getISODateTime());
  
  Firebase.RTDB.setJSON(&fbdo, "/rtc", &rtcJson);
}

void updateEEPROMStatus() {
  if (!firebaseReady) return;

  FirebaseJson eepromJson;
  eepromJson.set("initialized", true);
  eepromJson.set("settings_version", 1);
  eepromJson.set("last_write", getISODateTime());
  eepromJson.set("error_count", 0);
  eepromJson.set("wear_leveling", 100 - (eepromSettings.writeCount / 1000));
  
  Firebase.RTDB.setJSON(&fbdo, "/eeprom", &eepromJson);
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
      delay(500);
      ESP.restart();
    }
  }
}

void checkManualButton() {
  static unsigned long lastButtonPress = 0;
  static bool buttonPressed = false;
  
  if (digitalRead(buttonPin) == LOW) {
    if (!buttonPressed && millis() - lastButtonPress > 1000) {
      buttonPressed = true;
      feedManual();
      lastButtonPress = millis();
    }
  } else {
    buttonPressed = false;
  }
}

void updateSensorsAndDisplay() {
  static unsigned long lastUpdate = 0;
  if (millis() - lastUpdate < 2000) return;
  lastUpdate = millis();

  int foodLevel = readFoodLevel();
  String timeStr = "No Time";
  
  if (rtcAvailable) {
    DateTime now = rtc.now();
    char buffer[9];
    sprintf(buffer, "%02d:%02d:%02d", now.hour(), now.minute(), now.second());
    timeStr = String(buffer);
  } else if (timeClient.isTimeSet()) {
    timeStr = timeClient.getFormattedTime();
  }

  String lcdLine1 = "Food:" + String(foodLevel) + "%";
  String lcdLine2 = timeStr;
  
  if (WiFi.status() != WL_CONNECTED) {
    lcdLine2 += " Offline";
  }

  lcd.setCursor(0, 0);
  lcd.print(lcdLine1);
  lcd.setCursor(0, 1);
  lcd.print(lcdLine2);

  if (firebaseReady) {
    if (foodLevel >= 0) {
      Firebase.RTDB.setInt(&fbdo, "/sensors/food_level", foodLevel);
    }

    if (foodLevel < eepromSettings.lowFoodThreshold) {
      lcdLine1 = "LOW FOOD WARNING!";
      Firebase.RTDB.setString(&fbdo, "/sensors/low_food_alert", "true");
    } else {
      Firebase.RTDB.setString(&fbdo, "/sensors/low_food_alert", "false");
    }
    FirebaseJson displayJson;
    displayJson.set("lcd_line1", lcdLine1);
    displayJson.set("lcd_line2", lcdLine2);
    displayJson.set("last_updated", getISODateTime());
    Firebase.RTDB.setJSON(&fbdo, "/display", &displayJson);
  
    if (millis() - lastFeedTime > 60000) {
      Firebase.RTDB.setString(&fbdo, "/sensors/last_fed", getISODateTime());
      lastFeedTime = millis();
    }
  }
}

int readFoodLevel() {
  const float MIN_DISTANCE = 2.0;    
  const float MAX_DISTANCE = 11.75;  
  const int SAMPLE_COUNT = 3;        
  
  float totalDistance = 0;
  int validSamples = 0;
  
  for (int i = 0; i < SAMPLE_COUNT; i++) {
    digitalWrite(trigPin, LOW);
    delayMicroseconds(2);
    digitalWrite(trigPin, HIGH);
    delayMicroseconds(10);
    digitalWrite(trigPin, LOW);
  
    long duration = pulseIn(echoPin, HIGH, 30000);
    if (duration <= 0) continue;
    
    float distance = duration * 0.0343 / 2;
    
    if (distance >= 2.0 && distance <= 30.0) {
      totalDistance += distance;
      validSamples++;
    }
    
    delay(50); 
  }

  if (validSamples == 0) {
    Serial.println("Lỗi cảm biến: Không đọc được giá trị hợp lệ");
    return -1; 
  }
  
  float avgDistance = totalDistance / validSamples;
  
  if (avgDistance <= MIN_DISTANCE) {
    Serial.println("Mức thức ăn: 100% (dưới ngưỡng tối thiểu)");
    return 100;
  }
  else if (avgDistance >= MAX_DISTANCE) {
    Serial.println("Mức thức ăn: 0% (trên ngưỡng tối đa)");
    return 0;
  }
  
  int level = 100 - map(avgDistance * 100, MIN_DISTANCE * 100, MAX_DISTANCE * 100, 0, 100);
  
  Serial.print("Mức thức ăn: ");
  Serial.print(level);
  Serial.print("%, Khoảng cách: ");
  Serial.print(avgDistance);
  Serial.println("cm");
  
  return level;
}

void feedManual() {
  showLCDMessage("Manual Feeding", "...");
  
  if (!servo.attached()) servo.attach(servoPin);
  servo.write(90);
  delay(eepromSettings.feedDuration);
  servo.write(0);
  
  logFeeding("manual", 1);
  
  showLCDMessage("Manual Feeding", "Done!");
  delay(1000);
}

void checkAutoSchedule() {
  static int lastCheckedMinute = -1;
  
  int currentMinute;
  int currentHour;
  int currentDay;
  
  if (rtcAvailable) {
    DateTime now = rtc.now();
    currentMinute = now.minute();
    currentHour = now.hour();
    currentDay = now.dayOfTheWeek();
  } else if (timeClient.isTimeSet()) {
    currentMinute = timeClient.getMinutes();
    currentHour = timeClient.getHours();
    currentDay = timeClient.getDay();
  } else {
    return;
  }
  
  if (currentMinute == lastCheckedMinute || !firebaseReady) return;
  lastCheckedMinute = currentMinute;

  if (Firebase.RTDB.getJSON(&fbdo, "/schedule")) {
    FirebaseJson *json = fbdo.jsonObjectPtr();
    size_t count = json->iteratorBegin();

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
  showLCDMessage("Auto Feeding", "...");
  
  if (!servo.attached()) servo.attach(servoPin);
  servo.write(90);
  delay(eepromSettings.feedDuration * portion);
  servo.write(0);
  
  showLCDMessage("Auto Feeding", "Done!");
  delay(1000);
}

void logFeeding(String type, int portion) {
  if (firebaseReady) {
    String path = "/history/" + String(millis());
    FirebaseJson json;
    json.set("timestamp", getISODateTime());
    json.set("type", type);
    json.set("portion", portion);
    json.set("status", "completed");
    
    if (!Firebase.RTDB.setJSON(&fbdo, path.c_str(), &json)) {
      Serial.println("Failed to log feeding: " + fbdo.errorReason());
    }
  } else {
    saveFeedingToEEPROM(type, portion);
  }
  
  lastFeedTime = millis();
}

void saveFeedingToEEPROM(String type, int portion) {
  int address = sizeof(EEPROMSettings);
  while (EEPROM.read(address) != 0xFF && address < EEPROM.length() - 10) {
    address += 10; 
  }
  
  if (address < EEPROM.length() - 10) {
    EEPROM.write(address, type == "manual" ? 1 : 2);
    EEPROM.write(address + 1, portion);
    EEPROM.put(address + 2, getCurrentUnixTime());
    EEPROM.commit();
    Serial.println("Saved feeding to EEPROM");
  }
}

void syncFeedingsFromEEPROM() {
  int address = sizeof(EEPROMSettings);
  while (address < EEPROM.length() - 10) {
    byte feedingType = EEPROM.read(address);
    if (feedingType == 0xFF) break;
    
    byte portion = EEPROM.read(address + 1);
    uint32_t timestamp;
    EEPROM.get(address + 2, timestamp);
    
    logFeeding(feedingType == 1 ? "manual" : "schedule", portion);
    for (int i = 0; i < 10; i++) {
      EEPROM.write(address + i, 0xFF);
    }
    address += 10;
  }
  EEPROM.commit();
}