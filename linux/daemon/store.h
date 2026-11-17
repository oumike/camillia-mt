// SPDX-License-Identifier: GPL-3.0-or-later
// Message history for the CardputerZero daemon: ~/.config/camillia/messages.
// The file holds the same M and S records the socket carries (common/ipc.h),
// appended as they happen and compacted on start.
#pragma once

#include <stdint.h>

#include <deque>
#include <string>
#include <vector>

struct Msg {
    uint32_t id = 0;
    uint32_t epoch = 0;
    std::string conv;      // c<idx> or d<node hex>
    uint32_t from = 0, to = 0;
    char state = 'r';
    float rssi = 0, snr = 0;
    int hops = -1;         // -1: unknown (our own messages)
    std::string text;

    std::string record() const;        // the M line, without a newline
};

class MessageStore {
public:
    static constexpr size_t kKeep = 1000;

    void load(const std::string &path);
    // Appends and returns the stored message.
    const Msg &add(const Msg &m);
    // Changes the state of a message we sent; false when it is unknown or the
    // state is already final.
    bool setState(uint32_t myId, uint32_t pktId, char state);
    const std::deque<Msg> &all() const { return _msgs; }

private:
    void append(const std::string &line);
    void rewrite();
    std::string _path;
    std::deque<Msg> _msgs;
    size_t _fileLines = 0;
};

// Parses an M record (fields already split); false when malformed.
bool parseMsgRecord(const std::vector<std::string> &f, Msg &m);
