# Makefile for GStreamer llama.cpp examples

CC = gcc
CFLAGS = -Wall -Wextra $(shell pkg-config --cflags gstreamer-1.0)
LDFLAGS = $(shell pkg-config --libs gstreamer-1.0)

EXAMPLES = signal-example control-pad-example

all: $(EXAMPLES)

signal-example: signal-example.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

control-pad-example: control-pad-example.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

clean:
	rm -f $(EXAMPLES)

.PHONY: all clean
