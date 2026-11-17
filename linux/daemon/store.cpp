// SPDX-License-Identifier: GPL-3.0-or-later
#include "store.h"

#include "common/ipc.h"

#include <stdio.h>
#include <stdlib.h>

std::string Msg::record() const {
    return ipc::join(std::string("M"), ipc::hex32(id), std::to_string(epoch), conv, ipc::hex32(from),
                     ipc::hex32(to), std::string(1, state), ipc::fmt("%.0f", rssi), ipc::fmt("%.2f", snr),
                     std::to_string(hops), ipc::esc(text));
}

bool parseMsgRecord(const std::vector<std::string> &f, Msg &m) {
    if (f.size() < 11 || f[0] != "M" || f[6].size() != 1) return false;
    m.id = (uint32_t)strtoul(f[1].c_str(), nullptr, 16);
    m.epoch = (uint32_t)strtoul(f[2].c_str(), nullptr, 10);
    m.conv = f[3];
    m.from = (uint32_t)strtoul(f[4].c_str(), nullptr, 16);
    m.to = (uint32_t)strtoul(f[5].c_str(), nullptr, 16);
    m.state = f[6][0];
    m.rssi = strtof(f[7].c_str(), nullptr);
    m.snr = strtof(f[8].c_str(), nullptr);
    m.hops = atoi(f[9].c_str());
    m.text = f[10];
    return !m.conv.empty();
}

void MessageStore::load(const std::string &path) {
    _path = path;
    _msgs.clear();
    _fileLines = 0;
    if (FILE *f = fopen(path.c_str(), "r")) {
        std::string line;
        int c;
        while ((c = fgetc(f)) != EOF) {
            if (c != '\n') { line += (char)c; continue; }
            _fileLines++;
            auto fields = ipc::split(line);
            line.clear();
            Msg m;
            if (parseMsgRecord(fields, m)) {
                _msgs.push_back(m);
                if (_msgs.size() > kKeep) _msgs.pop_front();
            } else if (fields.size() == 3 && fields[0] == "S" && fields[2].size() == 1) {
                uint32_t id = (uint32_t)strtoul(fields[1].c_str(), nullptr, 16);
                for (auto it = _msgs.rbegin(); it != _msgs.rend(); ++it)
                    if (it->id == id && it->state != 'r') { it->state = fields[2][0]; break; }
            }
        }
        fclose(f);
    }
    // Anything still waiting when we stopped will never hear its ACK now.
    for (auto &m : _msgs)
        if (m.state == 'p') m.state = 'F';
    if (_fileLines > _msgs.size()) rewrite();
}

void MessageStore::append(const std::string &line) {
    if (_path.empty()) return;
    if (FILE *f = fopen(_path.c_str(), "a")) {
        fputs(line.c_str(), f);
        fputc('\n', f);
        fclose(f);
        _fileLines++;
    }
    if (_fileLines > 2 * kKeep) rewrite();
}

void MessageStore::rewrite() {
    std::string tmp = _path + ".tmp";
    FILE *f = fopen(tmp.c_str(), "w");
    if (!f) return;
    for (auto &m : _msgs) {
        fputs(m.record().c_str(), f);
        fputc('\n', f);
    }
    fclose(f);
    rename(tmp.c_str(), _path.c_str());
    _fileLines = _msgs.size();
}

const Msg &MessageStore::add(const Msg &m) {
    _msgs.push_back(m);
    if (_msgs.size() > kKeep) _msgs.pop_front();
    append(m.record());
    return _msgs.back();
}

bool MessageStore::setState(uint32_t myId, uint32_t pktId, char state) {
    for (auto it = _msgs.rbegin(); it != _msgs.rend(); ++it) {
        if (it->id != pktId || it->from != myId) continue;
        // ACK and NAK are final; a relay sighting only upgrades a plain send.
        if (it->state == 'A' || it->state == 'N' || it->state == state) return false;
        if (state == 'R' && it->state != 's') return false;
        it->state = state;
        append(ipc::join(std::string("S"), ipc::hex32(pktId), std::string(1, state)));
        return true;
    }
    return false;
}
