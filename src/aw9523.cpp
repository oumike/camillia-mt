#include "aw9523.h"

bool Aw9523::writeReg(uint8_t reg, uint8_t value) {
    if (!_bus) return false;
    _bus->beginTransmission(_addr);
    _bus->write(reg);
    _bus->write(value);
    return _bus->endTransmission() == 0;
}

bool Aw9523::readReg(uint8_t reg, uint8_t &value) {
    if (!_bus) return false;
    _bus->beginTransmission(_addr);
    _bus->write(reg);
    // Repeated start: a stop here lets another master interleave and the
    // expander would answer the wrong register.
    if (_bus->endTransmission(false) != 0) return false;
    if (_bus->requestFrom((int)_addr, 1) != 1) return false;
    value = (uint8_t)_bus->read();
    return true;
}

bool Aw9523::begin(uint8_t addr, TwoWire &bus) {
    _bus = &bus;
    _addr = addr;
    _present = false;

    // Retry the identify read. After a warm reset the first transfer on the bus
    // can time out while it settles — which shows up as whichever chip happens
    // to be probed first reporting absent, while every later one enumerates
    // fine. On this hardware that meant the left keyboard half looked missing
    // after a flash but was present after a power cycle, purely because it is
    // probed first.
    uint8_t id = 0;
    bool identified = false;
    for (int attempt = 0; attempt < 3 && !identified; attempt++) {
        if (attempt) delay(5);
        identified = readReg(REG_CHIPID, id) && (id == kChipId);
    }
    if (!identified) return false;

    // Soft reset first: the frame keeps these powered across a CPU reset, so a
    // warm boot would otherwise inherit whatever the last run left configured.
    writeReg(REG_SOFTRST, 0x00);
    delay(2);

    // Both ports as plain GPIO rather than the constant-current LED function.
    // Out of reset the LED mode registers are 0x00, which is LED mode — leaving
    // them would make every pin a current sink instead of a readable input.
    if (!writeReg(REG_LEDMODE0, 0xFF)) return false;
    if (!writeReg(REG_LEDMODE1, 0xFF)) return false;

    if (!writeReg(REG_CONFIG0, 0xFF)) return false;   // all inputs
    if (!writeReg(REG_CONFIG1, 0xFF)) return false;
    writeReg(REG_INTMASK0, 0xFF);                     // all masked
    writeReg(REG_INTMASK1, 0xFF);

    _present = true;
    return true;
}

bool Aw9523::readPort(uint8_t port, uint8_t &value) {
    if (!_present) return false;
    return readReg(port ? REG_INPUT1 : REG_INPUT0, value);
}

bool Aw9523::writePort(uint8_t port, uint8_t value) {
    if (!_present) return false;
    return writeReg(port ? REG_OUTPUT1 : REG_OUTPUT0, value);
}

bool Aw9523::configPort(uint8_t port, uint8_t inputMask) {
    if (!_present) return false;
    return writeReg(port ? REG_CONFIG1 : REG_CONFIG0, inputMask);
}

bool Aw9523::maskInterrupts(uint8_t port, uint8_t mask) {
    if (!_present) return false;
    return writeReg(port ? REG_INTMASK1 : REG_INTMASK0, mask);
}

bool Aw9523::setPort0PushPull(bool pushPull) {
    if (!_present) return false;
    uint8_t gcr = 0;
    if (!readReg(REG_GCR, gcr)) return false;
    if (pushPull) gcr |= 0x10;
    else          gcr &= (uint8_t)~0x10;
    return writeReg(REG_GCR, gcr);
}
