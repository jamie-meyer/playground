CC := xcrun clang
CFLAGS := -std=c17 -Wall -Wextra -Wpedantic -Werror -O2
FRAMEWORKS := -framework ApplicationServices -framework CoreFoundation -framework IOKit
TARGET := build/dellctl

.PHONY: all clean test

all: $(TARGET)

$(TARGET): src/dellctl.c
	mkdir -p build
	$(CC) $(CFLAGS) $< -o $@ $(FRAMEWORKS)

test: $(TARGET)
	$(TARGET) self-test

clean:
	rm -rf build
