#pragma once
#include <Arduino.h>
#include <Adafruit_Fingerprint.h>

class AS608FingerSensor {
  public:
    // Constructor: truyền số Serial và chân RX/TX
    AS608FingerSensor(HardwareSerial *serialPort, uint8_t rxPin, uint8_t txPin, uint32_t baud = 57600) {
      _serial = serialPort;
      _rxPin = rxPin;
      _txPin = txPin;
      _baud = baud;
      _finger = new Adafruit_Fingerprint(_serial);
    }

    // Khởi tạo cảm biến
    bool begin() {
      _serial->begin(_baud, SERIAL_8N1, _rxPin, _txPin);
      delay(100);
      if (_finger->verifyPassword()) {
        Serial.println("Found fingerprint sensor!");
        _finger->getParameters();
        printSensorParameters();
        return true;
      } else {
        Serial.println("Did not find fingerprint sensor :(");
        return false;
      }
    }

    // In thông số cảm biến
    void printSensorParameters() {
      Serial.println(F("Reading sensor parameters:"));
      Serial.print(F("Status: 0x")); Serial.println(_finger->status_reg, HEX);
      Serial.print(F("Sys ID: 0x")); Serial.println(_finger->system_id, HEX);
      Serial.print(F("Capacity: ")); Serial.println(_finger->capacity);
      Serial.print(F("Security level: ")); Serial.println(_finger->security_level);
      Serial.print(F("Device address: ")); Serial.println(_finger->device_addr, HEX);
      Serial.print(F("Packet len: ")); Serial.println(_finger->packet_len);
      Serial.print(F("Baud rate: ")); Serial.println(_finger->baud_rate);
    }

    // Hàm đọc số từ Serial, hỗ trợ 1-127 để enroll và 666 để search
    uint16_t readNumber() {
      uint16_t num = 0;
      while (true) {
        while (!Serial.available());
        num = Serial.parseInt();
        if ((num >= 1 && num <= 127) || num == 666) return num;
        Serial.println("Invalid input. Enter 1-127 for ID or 666 to search.");
      }
    }

    // Hàm enroll vân tay với ID
    bool enroll(uint16_t id) {
      int p = -1;
      Serial.print("Waiting for valid finger to enroll as #"); Serial.println(id);

      // Lấy ảnh
      while (p != FINGERPRINT_OK) {
        p = _finger->getImage();
        switch (p) {
          case FINGERPRINT_OK: Serial.println("Image taken"); break;
          case FINGERPRINT_NOFINGER: Serial.print("."); break;
          case FINGERPRINT_PACKETRECIEVEERR: Serial.println("Communication error"); return false;
          case FINGERPRINT_IMAGEFAIL: Serial.println("Imaging error"); return false;
          default: Serial.println("Unknown error"); return false;
        }
      }

      // Chuyển ảnh thành template
      p = _finger->image2Tz(1);
      if (p != FINGERPRINT_OK) { Serial.println("Image conversion failed"); return false; }

      Serial.println("Remove finger");
      delay(2000);
      while (_finger->getImage() != FINGERPRINT_NOFINGER);

      // Lấy ảnh lần 2
      p = -1;
      Serial.println("Place same finger again");
      while (p != FINGERPRINT_OK) {
        p = _finger->getImage();
        switch (p) {
          case FINGERPRINT_OK: Serial.println("Image taken"); break;
          case FINGERPRINT_NOFINGER: Serial.print("."); break;
          case FINGERPRINT_PACKETRECIEVEERR: Serial.println("Communication error"); return false;
          case FINGERPRINT_IMAGEFAIL: Serial.println("Imaging error"); return false;
          default: Serial.println("Unknown error"); return false;
        }
      }

      p = _finger->image2Tz(2);
      if (p != FINGERPRINT_OK) { Serial.println("Image conversion failed"); return false; }

      // Tạo model
      Serial.print("Creating model for #");  Serial.println(id);
      p = _finger->createModel();
      if (p != FINGERPRINT_OK) { Serial.println("Fingerprints did not match"); return false; }

      // Lưu model
      p = _finger->storeModel(id);
      if (p != FINGERPRINT_OK) { Serial.println("Could not store model"); return false; }

      Serial.println("Enrollment successful!");
      return true;
    }

    // Hàm tìm kiếm vân tay
    int search() {
      int p = -1;
      Serial.println("Place your finger to search...");
      while (p != FINGERPRINT_OK) {
        p = _finger->getImage();
        switch (p) {
          case FINGERPRINT_OK: Serial.println("Image taken"); break;
          case FINGERPRINT_NOFINGER: Serial.print("."); break;
          case FINGERPRINT_PACKETRECIEVEERR: Serial.println("Communication error"); return -1;
          case FINGERPRINT_IMAGEFAIL: Serial.println("Imaging error"); return -1;
          default: Serial.println("Unknown error"); return -1;
        }
      }

      // Chuyển ảnh thành template
      p = _finger->image2Tz();
      if (p != FINGERPRINT_OK) { Serial.println("Could not convert image"); return -1; }

      // Tìm kiếm trong bộ nhớ
      p = _finger->fingerFastSearch();
      if (p == FINGERPRINT_OK) {
        Serial.print("Found ID #"); 
        Serial.print(_finger->fingerID); 
        return _finger->fingerID;
      } else if (p == FINGERPRINT_NOTFOUND) {
        Serial.println("No match found");
        return 0;
      } else {
        Serial.println("Search error");
        return -1;
      }
    }

    // Hàm xóa toàn bộ dữ liệu vân tay
    int emptyDatabase() {
      int res = _finger->emptyDatabase();
      return res;
    } 

    // Kiểm tra xem ID vân tay đã được lưu trong bộ nhớ chưa
    bool exists(uint8_t id) {
        if (id < 1 || id > 127) return false;
        return (_finger->loadModel(id) == FINGERPRINT_OK);
    }

  private:
    HardwareSerial *_serial;
    Adafruit_Fingerprint *_finger;
    uint8_t _rxPin;
    uint8_t _txPin;
    uint32_t _baud;
};
