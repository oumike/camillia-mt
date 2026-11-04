#pragma once
#include "../Arduino.h"
#include <string>

// Only the String operations used by map_tiles.h's path builders.
class String {
public:
    String() = default;
    String(const char *text) : value(text) {}
    String &operator+=(const char *text) { value += text; return *this; }
    String &operator+=(int number) { value += std::to_string(number); return *this; }
    String &operator+=(long number) { value += std::to_string(number); return *this; }
    bool isEmpty() const { return value.empty(); }
    const char *c_str() const { return value.c_str(); }

private:
    std::string value;
};
