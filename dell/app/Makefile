PRODUCT := DellControl
BUNDLE_NAME := Dell Control.app
APP_PATH := build/$(BUNDLE_NAME)
CONTENTS := $(APP_PATH)/Contents
MACOS := $(CONTENTS)/MacOS
HELPERS := $(CONTENTS)/Helpers
RESOURCES := $(CONTENTS)/Resources
DELLCTL ?= ../build/dellctl
VERSION := 0.1.0
ARCH := arm64
DEVELOPER_ID ?= Developer ID Application: James Meyer (2E5Q4CLV2D)
NOTARY_PROFILE ?= j-notary
NOTARY_ARCHIVE := build/Dell-Control-notary.zip
RELEASE_DIR := build/dist
RELEASE_ARCHIVE := $(RELEASE_DIR)/Dell-Control-$(VERSION)-macos-$(ARCH).zip
MODULE_CACHE := $(CURDIR)/.build/ModuleCache
SWIFT_BUILD := CLANG_MODULE_CACHE_PATH="$(MODULE_CACHE)" SWIFTPM_MODULECACHE_OVERRIDE="$(MODULE_CACHE)" swift build --disable-sandbox
SWIFT_PACKAGE := CLANG_MODULE_CACHE_PATH="$(MODULE_CACHE)" SWIFTPM_MODULECACHE_OVERRIDE="$(MODULE_CACHE)" swift package --disable-sandbox

.PHONY: all clean package release signed

all: package

package:
	$(SWIFT_BUILD) -c release --product $(PRODUCT)
	rm -rf "$(APP_PATH)"
	mkdir -p "$(MACOS)" "$(HELPERS)" "$(RESOURCES)"
	cp ".build/release/$(PRODUCT)" "$(MACOS)/$(PRODUCT)"
	cp "Resources/Info.plist" "$(CONTENTS)/Info.plist"
	cp "$(DELLCTL)" "$(HELPERS)/dellctl"
	chmod 755 "$(MACOS)/$(PRODUCT)" "$(HELPERS)/dellctl"
	"$(MACOS)/$(PRODUCT)" --self-test
	"$(HELPERS)/dellctl" self-test
	codesign --force --deep --sign - "$(APP_PATH)"
	plutil -lint "$(CONTENTS)/Info.plist"
	codesign --verify --deep --strict "$(APP_PATH)"

signed: package
	codesign --force --options runtime --timestamp --sign "$(DEVELOPER_ID)" "$(HELPERS)/dellctl"
	codesign --force --options runtime --timestamp --sign "$(DEVELOPER_ID)" "$(APP_PATH)"
	codesign --verify --deep --strict --verbose=2 "$(APP_PATH)"

release: signed
	rm -rf "$(RELEASE_DIR)"
	rm -f "$(NOTARY_ARCHIVE)"
	mkdir -p "$(RELEASE_DIR)"
	ditto -c -k --keepParent "$(APP_PATH)" "$(NOTARY_ARCHIVE)"
	xcrun notarytool submit "$(NOTARY_ARCHIVE)" --keychain-profile "$(NOTARY_PROFILE)" --wait
	xcrun stapler staple "$(APP_PATH)"
	xcrun stapler validate "$(APP_PATH)"
	codesign --verify --deep --strict --verbose=2 "$(APP_PATH)"
	spctl --assess --type execute --verbose=4 "$(APP_PATH)"
	ditto -c -k --keepParent "$(APP_PATH)" "$(RELEASE_ARCHIVE)"
	@echo "Release ready: $(RELEASE_ARCHIVE)"

clean:
	$(SWIFT_PACKAGE) clean
	rm -rf build
