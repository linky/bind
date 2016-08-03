TARGET=bind

CC = gcc
CFLAGS = -Wall -Os
SOURCES = bind.c

all:
	$(CC) $(CFLAGS) -DNOMAIN -fPIC -shared $(SOURCES) -o $(TARGET).so
	$(CC) $(CFLAGS) $(SOURCES) -o $(TARGET)
clean:
	rm -f bind bind.so
