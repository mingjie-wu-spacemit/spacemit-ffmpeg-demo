CC = gcc
CFLAGS = -Wall -O2 -g
LDFLAGS = -lavcodec -lavutil

TARGET = hw_encode

all: $(TARGET)

$(TARGET): hw_encode.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

clean:
	rm -f $(TARGET) *.o

.PHONY: all clean
