// Host stand-in for <Arduino.h>: just what Arduino-free code under test needs.
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <algorithm>
#include <string>
using std::max;
using std::min;

class String {
public:
    String(const char *text = "") : _text(text) {}
    String operator+(const char *text) const {
        String out(*this);
        out._text += text;
        return out;
    }
    const char *c_str() const { return _text.c_str(); }
private:
    std::string _text;
};

#ifndef IRAM_ATTR
#define IRAM_ATTR
#endif
uint32_t millis();
