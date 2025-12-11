#include <Arduino.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <Preferences.h>

#include "Keypad3x4.h"
#include "AS608FingerSensorWithAdafruitFingerprintSensorLibrary.h"
#include "ServoPWM180.h"
#include "LED.h"

#include <WiFi.h>
#include <WiFiClientSecure.h> 
#include <PubSubClient.h>

// ===================== CONFIG =====================
#define DEFAULT_PASSWORD "1234"
#define MAX_FAIL_COUNT 3
#define LOCKOUT_TIME 30000

// WiFi & MQTT
const char* WIFI_SSID = "RN12T"; // test bằng 4g cho khỏe :))))))))
const char* WIFI_PASS = "1234567890";

const char* MQTT_HOST = "h0911427.ala.asia-southeast1.emqxsl.com";
const int   MQTT_PORT = 8883;
const char* MQTT_USER = "doorlockopen";
const char* MQTT_PASS = "123456789";

// ===================== PINOUT =====================
WiFiClientSecure espClient;
PubSubClient mqttClient(espClient);

// MQTT Topics
#define TOPIC_STATUS "door/status"
#define TOPIC_CMD    "door/command"
#define TOPIC_FINGER "door/fingerprint"

// ===================== HARDWARE OBJECTS =====================
LED ledRed(LED_RED_PIN, HIGH);
LED ledGreen(LED_GREEN_PIN, HIGH);
LiquidCrystal_I2C lcd(0x3F, 20, 4);
ServoPWM180 doorServo;
AS608FingerSensor finger(&Serial2, RX_PIN, TX_PIN);

Keypad3x4 keypad(
    (uint8_t[]){ROW0_PIN, ROW1_PIN, ROW2_PIN, ROW3_PIN},
    (uint8_t[]){COL0_PIN, COL1_PIN, COL2_PIN}
);

// ===================== STATE =====================
Preferences prefs;
String password, inputPassword;
uint8_t failCount = 0;
unsigned long lockoutTimer = 0;
unsigned long lastMqttAttempt = 0;
const unsigned long mqttRetryInterval = 5000;
bool firsttimeEnteringMenu = false;

// ===================== FORWARD DECLARATIONS =====================
void handleMenu();
void lcdMsg(const String &l1 = "", const String &l2 = "", const String &l3 = "", const String &l4 = "");
void beep(int ms = 50);
void openDoor();
void closeDoor();
int getNextFingerID();
void openDoorMenu();
void changePassword();
void addFinger();
void clearAllFingers();
void resetPassword();
void exitMenu();
void FingerSearchMode();
void lockMenu();
void mqttReconnect();
void mqttCallback(char* topic, byte* payload, unsigned int length);

// ===================== HELPERS =====================
void lcdMsg(const String &l1, const String &l2, const String &l3, const String &l4) {
    lcd.clear();
    lcd.setCursor(0,0); lcd.print(l1);
    lcd.setCursor(0,1); lcd.print(l2);
    lcd.setCursor(0,2); lcd.print(l3);
    lcd.setCursor(0,3); lcd.print(l4);
}

void beep(int ms) {
    digitalWrite(BUZZER_PIN, HIGH);
    delay(ms);
    digitalWrite(BUZZER_PIN, LOW);
}

int getNextFingerID() {
    for(int id = 1; id <= 127; id++) {
        if(!finger.exists(id)) return id;
    }
    return -1;
}

// ===================== MENU FUNCTIONS =====================
void openDoor() {
    lcdMsg("Door Opening...");
    doorServo.write(90);
    delay(3000);
    closeDoor();
    handleMenu();
}

void closeDoor() {
    doorServo.write(0);
}

void changePassword() {
    lcdMsg("New Pass:");
    String newPass = "";
    while(newPass.length() < 4) {
        char k = keypad.getKey();
        if(k >= '0' && k <= '9') {
            newPass += k;
            lcd.setCursor(newPass.length(), 1);
            lcd.print("*");
            beep(30);
        }
    }
    password = newPass;
    prefs.putString("password", newPass);
    if(mqttClient.connected()) {
        mqttClient.publish(TOPIC_STATUS, "password_changed");
    }
    lcdMsg("Pass Changed!");
    delay(500);
    handleMenu();
}

void addFinger() {
    lcdMsg("Add Finger...");
    int id = getNextFingerID();
    if(id == -1) {
        lcdMsg("DB Full");
        delay(500);
        return;
    }
    bool success = finger.enroll(id);
    lcdMsg(success ? "Add Success" : "Add Fail");
    if(mqttClient.connected()) {
        String payload;
        if (success) {
            payload = "add_success\nnew_id: " + String(id);
        } else {
            payload = "add_fail";
        }
        mqttClient.publish(TOPIC_FINGER, payload.c_str());
    }
    delay(500);
    handleMenu();
}

void clearAllFingers() {
    Serial.printf("Clear all fingers...");
    lcdMsg("Clear all fingers...");
    bool success = (finger.emptyDatabase() == 0);
    if(mqttClient.connected()) {
        mqttClient.publish(TOPIC_FINGER, success ? "clear_all_fingers_success" : "clear_all_fingers_fail");
    }
    delay(200);
    Serial.printf("Clear all fingers: %s\n", success ? "OK" : "FAIL");
    lcdMsg(success ? "OK" : "Fail");
    delay(200);
    lockMenu();
}

void exitMenu() {
    if(mqttClient.connected()) {
        mqttClient.publish(TOPIC_STATUS, "door_locked");
    }
    lcdMsg("Exit Menu");
    delay(500);
    ledGreen.off();
    ledRed.on();
    lockMenu();
}

void lockMenu() {
    lcd.clear();
    lcdMsg("Enter Password:","","" ,"Press # for finger");
}

// ===================== MENU HANDLER =====================
struct MenuItem { 
    String title; 
    void (*callback)(); 
};

MenuItem menuItems[] = {
    {"1:OpenDoor", openDoor},
    {"2:ChangePass", changePassword},
    {"3:AddFinger", addFinger},
    {"4:Exit", exitMenu}
};

void handleMenu() {
    ledGreen.on();
    ledRed.off();
    beep(100);

    const uint8_t itemsPerPage = 3;  
    const uint8_t totalItems = sizeof(menuItems)/sizeof(MenuItem);
    const uint8_t totalPages = (totalItems + itemsPerPage - 1) / itemsPerPage;

    uint8_t page = 0;
    unsigned long lastScroll = 0;
    const unsigned long scrollInterval = 3000;
    unsigned long lastActivity = millis();
    const unsigned long menuTimeout = 10000;

    if(mqttClient.connected() && firsttimeEnteringMenu == true) {
        mqttClient.publish(TOPIC_STATUS, "door_unlocked");
        firsttimeEnteringMenu = false;
    }

    // Hiển thị menu page hiện tại
    auto showPage = [&]() {
        lcd.clear();
        lcd.setCursor(0,0);
        lcd.print("========MENU========");

        uint8_t start = page * itemsPerPage;
        for(uint8_t i = 0; i < itemsPerPage; i++) {
            uint8_t idx = start + i;
            if(idx < totalItems) {
                lcd.setCursor(0, i+1);
                lcd.print(menuItems[idx].title);
            }
        }
    };

    showPage();

    while(true) {
        // 1. Đọc phím liên tục
        char key = keypad.getKey();
        if(key != '\0') {
            for(uint8_t i = 0; i < totalItems; i++) {
                if(key == menuItems[i].title[0]) {
                    menuItems[i].callback();
                    return;
                }
            }
        }

        // 2. Scroll menu mỗi scrollInterval
        if(millis() - lastScroll >= scrollInterval) {
            page = (page + 1) % totalPages;
            showPage();
            lastScroll = millis();
        }

        // 3. Kiểm tra timeout
        if(millis() - lastActivity >= menuTimeout) {
            Serial.println("Menu timeout - auto exiting");
            lcdMsg("Timeout", "Auto exiting...");
            delay(1000);
            exitMenu();
            return;
        }

        delay(5); // vòng lặp nhanh, đọc phím liên tục
    }
}

// ===================== FINGER MODE =====================
void FingerSearchMode() {
    int id=finger.search();
    if(id>0){
        lcdMsg("Finger OK!");
        beep(100);
        failCount=0;
        if(mqttClient.connected()) {
            mqttClient.publish(TOPIC_FINGER,("check_success\nID_found: " + String(id)).c_str());
        }
        firsttimeEnteringMenu = true;
        handleMenu();
    } else {
        lcdMsg("Finger Not Found");
        beep(200);
        failCount++;
        if(mqttClient.connected()) {
            mqttClient.publish(TOPIC_FINGER,"check_fail\nID_not_found");
            mqttClient.publish(TOPIC_STATUS, ("wrong_pass: " + String(failCount)).c_str());
        }
        lockMenu();
    }
}

// ===================== MQTT CALLBACK =====================
void mqttCallback(char* topic, byte* payload, unsigned int length){
    String msg;
    for(unsigned int i=0;i<length;i++) msg += (char)payload[i];
    Serial.printf("📨 MQTT IN [%s] => %s\n", topic, msg.c_str());
    // Xử lý mở cửa
    if(msg == "unlock") {
        handleMenu();
        firsttimeEnteringMenu = true;
    }
    // Xử lý xóa vân tay
    if(msg == "clear_all_fingers") {
        Serial.println("→ Processing CLEAR ALL FINGERS command from MQTT");
        clearAllFingers();
    }
    // Xử lý đổi mật khẩu
    if(msg.startsWith("change_password")) {
        Serial.println("→ Processing PASSWORD CHANGE");
        
        // Loại bỏ khoảng trắng và ký tự xuống dòng
        msg = msg.substring(strlen("change_password"));
        msg.trim();
        
        Serial.printf("   Raw message length: %d\n", msg.length());
        Serial.printf("   Raw message (hex): ");
        for(int i = 0; i < msg.length(); i++){
            Serial.printf("%02X ", msg[i]);
        }
        Serial.println();
        Serial.printf("   Message content: [%s]\n", msg.c_str());
        
        // Kiểm tra độ dài
        if(msg.length() < 4 || msg.length() > 4){
            Serial.printf("✗ Password must be exactly 4 digits! (got %d)\n", msg.length());
            mqttClient.publish(TOPIC_STATUS, "password_error_length");
            return;
        }
        
        // Kiểm tra chỉ có số
        bool allDigits = true;
        for(int i = 0; i < 4; i++){
            if(msg[i] < '0' || msg[i] > '9'){
                Serial.printf("✗ Character at position %d is not a digit: '%c' (0x%02X)\n", 
                              i, msg[i], msg[i]);
                allDigits = false;
                break;
            }
        }
        
        if(!allDigits){
            Serial.println("✗ Password must contain only digits (0-9)!");
            mqttClient.publish(TOPIC_STATUS, "password_error_format");
            return;
        }
        
        // Lưu mật khẩu mới
        String oldPassword = password;
        password = msg;
        
        // Lưu vào flash
        bool saved = prefs.putString("password", password);
        
        Serial.println("========================================");
        Serial.println("✓✓✓ PASSWORD CHANGED SUCCESSFULLY ✓✓✓");
        Serial.println("   Old: " + oldPassword);
        Serial.println("   New: " + password);
        Serial.printf("   Saved to flash: %s\n", saved ? "YES" : "NO");
        Serial.println("========================================");
        
        // Thông báo thành công
        mqttClient.publish(TOPIC_STATUS, "password_changed");
        
        // Hiển thị LCD
        lcdMsg("Password Changed", "New: " + password);
        beep(100);
        delay(2000);
        
        // Quay về màn hình khóa
        inputPassword = "";
        failCount = 0;
        lockMenu();
    }
}

// ===================== MQTT ERROR HANDLER =====================
void printMQTTError(int errorCode) {
    Serial.print("⚠️  MQTT Error: ");
    switch(errorCode) {
        case -4: Serial.println("CONNECTION_TIMEOUT"); break;
        case -3: Serial.println("CONNECTION_LOST"); break;
        case -2: Serial.println("CONNECT_FAILED"); break;
        case -1: Serial.println("DISCONNECTED"); break;
        case  1: Serial.println("BAD_PROTOCOL"); break;
        case  2: Serial.println("BAD_CLIENT_ID"); break;
        case  3: Serial.println("UNAVAILABLE"); break;
        case  4: Serial.println("BAD_CREDENTIALS"); break;
        case  5: Serial.println("UNAUTHORIZED"); break;
        default: Serial.println("UNKNOWN"); break;
    }
}

// ===================== MQTT RECONNECT =====================
void mqttReconnect(){
    // Không thử quá thường xuyên
    unsigned long now = millis();
    if(now - lastMqttAttempt < mqttRetryInterval) {
        return;
    }
    lastMqttAttempt = now;

    if(!mqttClient.connected()){
        Serial.print("MQTT connecting to ");
        Serial.print(MQTT_HOST);
        Serial.print(":");
        Serial.println(MQTT_PORT);
        
        String clientId="ESP32_Door_";
        clientId+=String((uint32_t)ESP.getEfuseMac(), HEX);
        
        Serial.print("Client ID: ");
        Serial.println(clientId);
        Serial.print("Username: ");
        Serial.println(MQTT_USER);
        
        if(mqttClient.connect(clientId.c_str(), MQTT_USER, MQTT_PASS)){
            Serial.println("✓ MQTT connected!");
            
            // Subscribe topics
            mqttClient.subscribe(TOPIC_CMD);
            
            Serial.println("✓ Subscribed to topics");
            
            // Publish online status
            mqttClient.publish(TOPIC_STATUS,"connected", true);
            
        } else {
            Serial.print("✗ MQTT connect failed, rc=");
            Serial.println(mqttClient.state());
            printMQTTError(mqttClient.state());
        }
    }
}

// ===================== SETUP =====================
void setup(){
    Serial.begin(115200);
    
    pinMode(BUZZER_PIN,OUTPUT);

    Wire.begin(LCD_SDA, LCD_SCL);
    lcd.begin();
    lcd.backlight();
    lcdMsg("System Starting...");

    doorServo.attach(SERVO_PIN,0);
    doorServo.write(0);

    ledRed.on();
    ledGreen.off();

    keypad.begin();
    finger.begin();

    prefs.begin("locksys", false);
    password=prefs.getString("password", DEFAULT_PASSWORD);
    
    Serial.print("Password loaded: ");
    Serial.println(password);

    // ==================== WIFI ====================
    Serial.println("\n========================================");
    Serial.print("Connecting to WiFi: ");
    Serial.println(WIFI_SSID);
    Serial.println("========================================");
    
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    
    unsigned long startWifi = millis();
    int dots = 0;
    while(WiFi.status() != WL_CONNECTED && millis() - startWifi < 20000){
        Serial.print(".");
        dots++;
        if(dots % 50 == 0) Serial.println();
        delay(200);
    }
    
    Serial.println();
    
    if(WiFi.status() == WL_CONNECTED){
        Serial.println("✓✓✓ WiFi CONNECTED ✓✓✓");
        Serial.print("✓ IP Address: ");
        Serial.println(WiFi.localIP());
        Serial.print("✓ Signal: ");
        Serial.print(WiFi.RSSI());
        Serial.println(" dBm");
        Serial.println("========================================\n");
        
        lcdMsg("WiFi Connected", WiFi.localIP().toString());
        delay(1500);
    } else {
        Serial.println("✗✗✗ WiFi NOT CONNECTED ✗✗✗");
        Serial.println("System will work in OFFLINE mode");
        Serial.println("========================================\n");
        
        lcdMsg("WiFi Failed", "Offline Mode");
        delay(2000);
    }

    // ==================== MQTT SETUP ====================
    Serial.println("Configuring MQTT...");
    
    // ← QUAN TRỌNG: Bỏ qua xác thực SSL certificate
    espClient.setInsecure();  // Cho phép kết nối mà không cần verify CA
    
    mqttClient.setServer(MQTT_HOST, MQTT_PORT);
    mqttClient.setCallback(mqttCallback);
    mqttClient.setKeepAlive(60);
    mqttClient.setSocketTimeout(30);
    mqttClient.setBufferSize(512);
    
    Serial.print("MQTT Server: ");
    Serial.print(MQTT_HOST);
    Serial.print(":");
    Serial.println(MQTT_PORT);
    Serial.println("========================================\n");

    closeDoor();
    lockMenu();
    
    Serial.println("System Ready!\n");
}

// ===================== LOOP =====================
void loop(){
    ledRed.loop();
    ledGreen.loop();

    // MQTT handling
    if(WiFi.status() == WL_CONNECTED){
        if(!mqttClient.connected()) {
            mqttReconnect();
        } else {
            mqttClient.loop();
        }
    }

    // Khóa nếu nhập sai quá nhiều lần
    //if(failCount >= MAX_FAIL_COUNT){
    //    if(lockoutTimer == 0) {
    //        lockoutTimer = millis();
    //        Serial.println("SYSTEM LOCKED due to too many failed attempts!");
    //    }
    //   unsigned long remain = (LOCKOUT_TIME - (millis() - lockoutTimer)) / 1000;
    //   lcdMsg("Locked!", String(remain) + "s");
    //    
    //    if(millis() - lockoutTimer >= LOCKOUT_TIME){
    //        Serial.println("✓ Lockout period ended");
    //        failCount = 0;
    //        lockoutTimer = 0;
    //        inputPassword = "";
    //        lockMenu();
    //    }
    //    return;
    // }

    // Password input
    char key = keypad.getKey();
    if(key >= '0' && key <= '9' && inputPassword.length() < 4){
        inputPassword += key;
        lcd.setCursor(inputPassword.length(), 1);
        lcd.print("*");
        beep(30);
        
        if(inputPassword.length() == 4){
            if(inputPassword == password){
                Serial.println("✓ Password correct!");
                lcd.clear();
                lcd.print("Correct Pass!");
                beep(100);
                ledGreen.on(); 
                ledRed.off();
                inputPassword = "";
                failCount = 0;
                delay(500);
                firsttimeEnteringMenu = true;
                handleMenu();
            } else {
                Serial.println("✗ Wrong password! Attempt: " + String(failCount + 1));
                lcd.clear();
                lcd.print("Wrong Pass!");
                beep(200);
                failCount++;
                if(mqttClient.connected()) {
                    mqttClient.publish(TOPIC_STATUS, ("wrong_pass: " + String(failCount)).c_str());
                }
                inputPassword = "";
                delay(500);
                lockMenu();
            }
        }
    }

    if(key == '#'){
        Serial.println("Fingerprint mode activated");
        inputPassword = "";
        lcd.clear();
        lcd.print("Scan Finger...");
        beep(50);
        FingerSearchMode();
    }
}