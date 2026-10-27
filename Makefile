CC ?= cc
CFLAGS ?= -std=c11 -O2 -Wall -Wextra
SRC = backend/server.c backend/music.c backend/json.c
ifeq ($(OS),Windows_NT)
TARGET = echo_server.exe
LIBS = -lws2_32
else
TARGET = echo_server
LIBS =
endif

.PHONY: all run test clean
all: $(TARGET)
$(TARGET): $(SRC) backend/json.h backend/music.h
	$(CC) $(CFLAGS) -o $(TARGET) $(SRC) $(LIBS)
run: all
	./$(TARGET)
test: all
	python tests/integration_test.py
clean:
	rm -f echo_server echo_server.exe
