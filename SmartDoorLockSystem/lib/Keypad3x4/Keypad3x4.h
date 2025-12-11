#pragma once
#include <Arduino.h>

class Keypad3x4 {
public:
    // Constructor: mảng các chân Rows và Cols
    Keypad3x4(uint8_t rowPins[4], uint8_t colPins[3]) {
        for (uint8_t i = 0; i < 4; i++) _rowPins[i] = rowPins[i];
        for (uint8_t i = 0; i < 3; i++) _colPins[i] = colPins[i];

        _keys[0][0] = '1'; _keys[0][1] = '2'; _keys[0][2] = '3';
        _keys[1][0] = '4'; _keys[1][1] = '5'; _keys[1][2] = '6';
        _keys[2][0] = '7'; _keys[2][1] = '8'; _keys[2][2] = '9';
        _keys[3][0] = '*'; _keys[3][1] = '0'; _keys[3][2] = '#';
    }

    void begin() {
        // Rows là output, Cols là input pullup
        for (uint8_t i = 0; i < 4; i++) pinMode(_rowPins[i], OUTPUT);
        for (uint8_t i = 0; i < 3; i++) pinMode(_colPins[i], INPUT_PULLUP);

        // Tất cả row HIGH
        for (uint8_t i = 0; i < 4; i++) digitalWrite(_rowPins[i], HIGH);
    }

    // Trả về ký tự phím bấm, '\0' nếu không có phím
    char getKey() {
        for (uint8_t r = 0; r < 4; r++) {
            // Kéo row hiện tại xuống LOW
            digitalWrite(_rowPins[r], LOW);
            for (uint8_t c = 0; c < 3; c++) {
                if (digitalRead(_colPins[c]) == LOW) {
                    // Đợi thả phím
                    while (digitalRead(_colPins[c]) == LOW);
                    digitalWrite(_rowPins[r], HIGH);
                    return _keys[r][c];
                }
            }
            digitalWrite(_rowPins[r], HIGH);
        }
        delay(20);
        return '\0'; // Không có phím
    }

private:
    uint8_t _rowPins[4];
    uint8_t _colPins[3];
    char _keys[4][3];
};
