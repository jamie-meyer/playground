CC := xcrun clang
CFLAGS := -std=c17 -Wall -Wextra -Wpedantic -Werror -O2
FRAMEWORKS := -framework ApplicationServices -framework CoreFoundation -framework IOKit
TARGET := build/dellctl

.PHONY: all app clean install release test

all: $(TARGET)

$(TARGET): src/dellctl.c
	mkdir -p build
	$(CC) $(CFLAGS) $< -o $@ $(FRAMEWORKS)

test: $(TARGET)
	$(TARGET) self-test

app: $(TARGET)
	$(MAKE) -C app package DELLCTL="$(abspath $(TARGET))"

release: $(TARGET)
	$(MAKE) -C app release DELLCTL="$(abspath $(TARGET))"

install: app
	rm -rf "/Applications/Dell Control.app"
	ditto "$(abspath app/build/Dell Control.app)" "/Applications/Dell Control.app"

clean:
	rm -rf build
	$(MAKE) -C app clean
