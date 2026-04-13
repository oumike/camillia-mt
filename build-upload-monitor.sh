#!/bin/bash

pio run -e tdeck -t fullclean && \
pio run -e tdeck -t upload && \
pio run -t monitor
