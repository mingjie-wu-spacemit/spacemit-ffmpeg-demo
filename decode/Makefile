CC = gcc
CFLAGS = -Wall -O2 -g
LDFLAGS = -lavformat -lavcodec -lavutil

TARGET = hw_decode

all: $(TARGET)

$(TARGET): hw_decode.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

clean:
	rm -f $(TARGET) *.o

.PHONY: all clean
