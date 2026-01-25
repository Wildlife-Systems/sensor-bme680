CC=gcc
CFLAGS=-Wall -g

SRC=src/main.c
OBJ=$(SRC:.c=.o)
TARGET=bme680-sensor

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) -o $@ $^

clean:
	rm -f $(OBJ) $(TARGET)
