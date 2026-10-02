#pragma once
// Wardrive log: one CSV line per radio sighting of a node, stamped with our own
// GPS fix, appended to file storage as it happens.
//
// The node CSV export (/nodes.csv) is a snapshot of the table -- one row per
// node, whatever the table holds at the moment you download it. That loses the
// drive itself: every place a node was heard, at what signal, and anything that
// fell out of the table on a busy route. This log keeps that, and because it is
// appended continuously, a crash or a flat battery mid-drive costs only the
// last few seconds rather than the whole session.
//
// Same shape as the evicted-node archive in node_db: the packet path only
// queues a small record, and wardriveLogFlush() does the file I/O from the main
// loop, because SD shares the SPI bus with the LoRa radio.
//
// Rate: one line per node per kWardriveMinIntervalMs, or sooner when we have
// moved kWardriveMinMoveM since that node's last line. A node chattering every
// few seconds while you sit at a light does not fill the card with duplicates,
// and one heard continuously along a road still gets a trail of points.
//
// Only radio sightings are logged (MQTT ones say nothing about where our radio
// was), and only while the GPS has a fix: a line with no position is useless to
// a map, so those are counted in wardriveLogSkippedNoFix() instead.
#include <Arduino.h>

// Mirrored from RhinoConfig::wardriveLogEnabled by the main loop each pass.
void wardriveLogSetEnabled(bool enabled);
bool wardriveLogIsEnabled();

// Packet path. Cheap: a throttle lookup and a struct copy, no I/O. hops is -1
// when the packet did not carry hop_start.
void wardriveLogNoteSighting(uint32_t nodeId, float rssi, float snr, int hops,
                             int portnum, int chanIdx);

// Main loop. Writes everything queued in one file open; no-op when idle.
void wardriveLogFlush();

// Path of the log file, or null on a board with no file storage.
const char *wardriveLogFilePath();
bool        wardriveLogAvailable();   // storage exists and is mounted

// Delete the log and reset the session counters. True when the file is gone
// afterwards, including when there was none.
bool wardriveLogClear();

// Session counters (since boot or the last clear).
uint32_t wardriveLogLines();          // lines written to the file
uint32_t wardriveLogNodes();          // distinct nodes logged (capped, see .cpp)
uint32_t wardriveLogSkippedNoFix();   // sightings dropped for want of a GPS fix
uint32_t wardriveLogDropped();        // queued lines lost (no storage / write error)
