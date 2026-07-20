CC = gcc
CFLAGS = -Wall -O2 -g
LDFLAGS = -lavformat -lavcodec -lavutil

TARGETS = demo_h264_decode demo_mjpeg_decode

all: $(TARGETS)

demo_h264_decode: demo_h264_decode.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

demo_mjpeg_decode: demo_mjpeg_decode.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

clean:
	rm -f $(TARGETS) *.o

.PHONY: all clean
