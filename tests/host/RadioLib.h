#pragma once
#include <stdint.h>
#include <memory>

class Module {
public:
    Module(uint32_t, int, int, int) {}
};

class SX1262 {
public:
    explicit SX1262(Module *module) : _module(module) {}
private:
    std::unique_ptr<Module> _module;
};
