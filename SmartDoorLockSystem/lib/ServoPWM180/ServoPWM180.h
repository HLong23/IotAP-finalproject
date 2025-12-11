#pragma once
#include <Arduino.h>

class ServoPWM180 {
private:
    int _pin = -1;
    int _channel = -1;
    int _minUs = 500;
    int _maxUs = 2400;
    bool _attached = false;

public:
    void attach(int pin, int channel = 0, int minUs = 500, int maxUs = 2400) {
        _pin = pin;
        _channel = channel;
        _minUs = minUs;
        _maxUs = maxUs;

        // 50Hz + 8bit
        ledcSetup(_channel, 50, 8);
        ledcAttachPin(_pin, _channel);

        _attached = true;
    }

    void write(int angle) {
        if (!_attached) return;

        angle = constrain(angle, 0, 180);

        int pulseUs = map(angle, 0, 180, _minUs, _maxUs);

        // 20,000µs chu kỳ tại 50Hz
        uint32_t duty = (pulseUs * 255) / 20000;

        ledcWrite(_channel, duty);
    }
};
