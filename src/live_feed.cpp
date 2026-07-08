#include "live_feed.h"

#include "channel_mgr.h"
#include "config.h"
#include "live_util.h"

void liveFeedAddPrefixed(const char *prefix,
                         const char *text,
                         uint16_t color,
                         uint32_t packetId,
                         bool trackAck) {
    Channels.addMessage(CHAN_LIVE,
                        (prefix && prefix[0]) ? prefix : "",
                        text ? text : "",
                        color,
                        packetId,
                        trackAck);
}

void liveFeedAddLine(const char *text, uint16_t color) {
    char prefix[12];
    liveBuildPrefix(prefix, sizeof(prefix));
    liveFeedAddPrefixed(prefix, text, color, 0, false);
}
