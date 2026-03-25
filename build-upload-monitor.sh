#!/bin/bash
pio run -e tdeck -t fullclean && \
pio run -e tdeck -t upload --upload-port /dev/cu.usbmodem101 && \
pio run -t monitor
