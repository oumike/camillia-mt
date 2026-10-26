# Stops a malformed MQTT PUBLISH from overflowing PubSubClient's heap buffer.
#
# PubSubClient 2.8 trusts the 16-bit topic length that arrives on the wire. In
# PubSubClient::loop() it does
#
#   uint16_t tl = (buffer[llen+1]<<8)+buffer[llen+2];
#   memmove(buffer+llen+2, buffer+llen+3, tl);
#   buffer[llen+2+tl] = 0;
#   callback(topic, payload, len-llen-3-tl);
#
# without ever comparing tl against len, the number of bytes actually received.
# A PUBLISH with tl = 0xFFFF and a short body memmoves ~64 KB within and past
# the (512-byte) heap buffer, writes a NUL far outside it, and hands the
# callback a length that has wrapped to ~4e9. readPacket() also reads the two
# topic-length bytes of a PUBLISH before checking that the remaining length is
# even 2, so they can come from the next packet.
#
# A compliant broker never sends this, but MQTT here is plaintext TCP: a hostile
# broker or anyone on-path can. See logs/audit-codebase-2026-10-20/
# final-audit-netsvc.md.
#
# After patching:
#   * readPacket() drops the connection on a PUBLISH whose remaining length is
#     below 2 (no room for the topic-length field).
#   * loop() drops the connection on a PUBLISH whose header + topic (+ message
#     id for QoS 1) is longer than what was received, or whose received length
#     exceeds the buffer. Framing that lies about its own lengths cannot be
#     trusted for the rest of the stream, so the connection is closed (the
#     bridge reconnects with its normal backoff) rather than the packet skipped.
#
# Scope: PlatformIO gives each environment its own libdeps copy, so this only
# touches the envs that list the script in extra_scripts.
#
# Idempotent. Dependencies are installed before SCons runs, so for a build the
# library is always present here; anything short of a fully patched source
# fails the build rather than shipping the overflow. The one exception is the
# clean pass, which PlatformIO runs before installing anything.
Import("env")
import os

from SCons.Script import COMMAND_LINE_TARGETS

MARKER = "camillia-pubsub-topiclen-patch"
TAG = "[patch_pubsubclient_topiclen]"

OLD_READ = """    if (isPublish) {
        // Read in topic length to calculate bytes to skip over for Stream writing
        if(!readByte(this->buffer, &len)) return 0;"""

NEW_READ = """    if (isPublish) {
        // camillia-pubsub-topiclen-patch: a PUBLISH needs at least the 2-byte
        // topic length. Without this the two reads below take bytes from the
        // next packet and the stream is desynchronised.
        if (length < 2) {
            _state = MQTT_DISCONNECTED;
            _client->stop();
            return 0;
        }
        // Read in topic length to calculate bytes to skip over for Stream writing
        if(!readByte(this->buffer, &len)) return 0;"""

OLD_LOOP = """                if (type == MQTTPUBLISH) {
                    if (callback) {
                        uint16_t tl = (this->buffer[llen+1]<<8)+this->buffer[llen+2]; /* topic length in bytes */"""

NEW_LOOP = """                if (type == MQTTPUBLISH) {
                    // camillia-pubsub-topiclen-patch: the topic length comes
                    // straight off the wire. Reject a PUBLISH whose header,
                    // topic and (QoS 1) message id do not fit in what was
                    // actually received, before the memmove and NUL write
                    // below use it.
                    {
                        const uint32_t rxLen = len;
                        const uint32_t hdrLen = (uint32_t)llen + 3;
                        const uint32_t idLen = ((this->buffer[0]&0x06) == MQTTQOS1) ? 2 : 0;
                        bool bad = (rxLen > this->bufferSize) || (rxLen < hdrLen);
                        if (!bad) {
                            const uint32_t topicLen = ((uint32_t)this->buffer[llen+1]<<8) | this->buffer[llen+2];
                            bad = (hdrLen + topicLen + idLen > rxLen);
                        }
                        if (bad) {
                            _state = MQTT_DISCONNECTED;
                            _client->stop();
                            return false;
                        }
                    }
                    if (callback) {
                        uint16_t tl = (this->buffer[llen+1]<<8)+this->buffer[llen+2]; /* topic length in bytes */"""

HUNKS = ((OLD_READ, NEW_READ), (OLD_LOOP, NEW_LOOP))

libdeps = os.path.join(env.subst("$PROJECT_LIBDEPS_DIR"), env.subst("$PIOENV"))

# Recognised by its source file rather than by folder name, so a renamed
# install cannot pass for one not yet fetched.
sources = []
if os.path.isdir(libdeps):
    for name in sorted(os.listdir(libdeps)):
        path = os.path.join(libdeps, name, "src", "PubSubClient.cpp")
        if os.path.isfile(path):
            sources.append(path)

# PlatformIO runs a clean target in its own SCons pass, before it installs
# dependencies; the build pass that follows does not carry the clean target.
# The clean does not arrive as a target name: PlatformIO turns clean/fullclean
# into SCons' --clean plus FULLCLEAN=0/1 (platform/_run.py), so ask the builder,
# as builder/main.py does. A fullclean also deletes libdeps, so the next one
# finds nothing here -- which is how this used to fail `release.sh --dist-only`.
clean_only = env.IsCleanTarget() or any(
    t in ("clean", "cleanall", "fullclean") for t in COMMAND_LINE_TARGETS)

if not sources:
    if clean_only:
        print("%s PubSubClient not installed (clean run), nothing to patch" % TAG)
    else:
        raise RuntimeError(
            "%s PubSubClient.cpp not found under %s - refusing to build an "
            "unpatched MQTT client" % (TAG, libdeps))

drifted = []
for path in sources:
    rel = os.path.relpath(path, libdeps)
    with open(path, encoding="utf-8") as f:
        src = f.read()
    count = src.count(MARKER)
    if count == len(HUNKS):
        print("%s already patched: %s" % (TAG, rel))
        continue
    if count != 0:
        print("%s ERROR: partial patch in %s" % (TAG, rel))
        drifted.append(rel)
        continue
    if not all(src.count(old) == 1 for old, _ in HUNKS):
        print("%s ERROR: pattern not found in %s - version drift?" % (TAG, rel))
        drifted.append(rel)
        continue
    for old, new in HUNKS:
        src = src.replace(old, new, 1)
    with open(path, "w", encoding="utf-8") as f:
        f.write(src)
    print("%s patched PUBLISH topic-length validation: %s" % (TAG, rel))

if drifted:
    raise RuntimeError(
        "%s refusing to build with an unpatched PubSubClient: %s"
        % (TAG, ", ".join(drifted)))
