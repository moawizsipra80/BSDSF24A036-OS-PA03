CC = gcc
CFLAGS = -Wall -Wextra -Wpedantic -std=c11 -Iinclude
TARGET = bin/myshell
OBJECTS = obj/main.o obj/shell.o obj/execute.o

all: $(TARGET)

$(TARGET): $(OBJECTS)
	mkdir -p bin
	$(CC) $(OBJECTS) -o $(TARGET)

obj/%.o: src/%.c include/shell.h
	mkdir -p obj
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf bin obj

.PHONY: all clean
