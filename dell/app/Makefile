PRODUCT := DellControl
BUNDLE_NAME := Dell Control.app
APP_PATH := build/$(BUNDLE_NAME)
CONTENTS := $(APP_PATH)/Contents
MACOS := $(CONTENTS)/MacOS
HELPERS := $(CONTENTS)/Helpers
RESOURCES := $(CONTENTS)/Resources
DELLCTL ?= ../build/dellctl
VERSION := 0.1.1
ARCH := arm64
DEVELOPER_ID ?= Developer ID Application: James Meyer (2E5Q4CLV2D)
NOTARY_PROFILE ?= j-notary
NOTARY_ARCHIVE := build/Dell-Control-notary.zip
RELEASE_DIR := build/dist
RELEASE_DMG := $(RELEASE_DIR)/Dell-Control-$(VERSION)-macos-$(ARCH).dmg
DMG_STAGING := build/dmg
ICON_SOURCE := Resources/AppIcon.svg
ICON_PNG := build/AppIcon-1024.png
ICONSET := build/AppIcon.iconset
ICON := build/AppIcon.icns
MODULE_CACHE := $(CURDIR)/.build/ModuleCache
SWIFT_BUILD := CLANG_MODULE_CACHE_PATH="$(MODULE_CACHE)" SWIFTPM_MODULECACHE_OVERRIDE="$(MODULE_CACHE)" swift build --disable-sandbox
SWIFT_PACKAGE := CLANG_MODULE_CACHE_PATH="$(MODULE_CACHE)" SWIFTPM_MODULECACHE_OVERRIDE="$(MODULE_CACHE)" swift package --disable-sandbox

.PHONY: all clean package release signed

all: package

package: $(ICON)
	$(SWIFT_BUILD) -c release --product $(PRODUCT)
	rm -rf "$(APP_PATH)"
	mkdir -p "$(MACOS)" "$(HELPERS)" "$(RESOURCES)"
	cp ".build/release/$(PRODUCT)" "$(MACOS)/$(PRODUCT)"
	cp "Resources/Info.plist" "$(CONTENTS)/Info.plist"
	cp "$(ICON)" "$(RESOURCES)/AppIcon.icns"
	cp "$(DELLCTL)" "$(HELPERS)/dellctl"
	chmod 755 "$(MACOS)/$(PRODUCT)" "$(HELPERS)/dellctl"
	"$(MACOS)/$(PRODUCT)" --self-test
	"$(HELPERS)/dellctl" self-test
	codesign --force --deep --sign - "$(APP_PATH)"
	plutil -lint "$(CONTENTS)/Info.plist"
	codesign --verify --deep --strict "$(APP_PATH)"

$(ICON): $(ICON_SOURCE)
	rm -rf "$(ICONSET)"
	mkdir -p "$(ICONSET)"
	qlmanage -t -s 1024 -o build "$(ICON_SOURCE)"
	mv "build/$(notdir $(ICON_SOURCE)).png" "$(ICON_PNG)"
	sips -z 16 16 "$(ICON_PNG)" --out "$(ICONSET)/icon_16x16.png"
	sips -z 32 32 "$(ICON_PNG)" --out "$(ICONSET)/icon_16x16@2x.png"
	sips -z 32 32 "$(ICON_PNG)" --out "$(ICONSET)/icon_32x32.png"
	sips -z 64 64 "$(ICON_PNG)" --out "$(ICONSET)/icon_32x32@2x.png"
	sips -z 128 128 "$(ICON_PNG)" --out "$(ICONSET)/icon_128x128.png"
	sips -z 256 256 "$(ICON_PNG)" --out "$(ICONSET)/icon_128x128@2x.png"
	sips -z 256 256 "$(ICON_PNG)" --out "$(ICONSET)/icon_256x256.png"
	sips -z 512 512 "$(ICON_PNG)" --out "$(ICONSET)/icon_256x256@2x.png"
	sips -z 512 512 "$(ICON_PNG)" --out "$(ICONSET)/icon_512x512.png"
	cp "$(ICON_PNG)" "$(ICONSET)/icon_512x512@2x.png"
	iconutil -c icns "$(ICONSET)" -o "$(ICON)"

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
	rm -rf "$(DMG_STAGING)"
	mkdir -p "$(DMG_STAGING)"
	ditto "$(APP_PATH)" "$(DMG_STAGING)/$(BUNDLE_NAME)"
	ln -s /Applications "$(DMG_STAGING)/Applications"
	hdiutil create -volname "Dell Control" -srcfolder "$(DMG_STAGING)" -ov -format UDZO "$(RELEASE_DMG)"
	codesign --force --timestamp --sign "$(DEVELOPER_ID)" "$(RELEASE_DMG)"
	codesign --verify --verbose=2 "$(RELEASE_DMG)"
	xcrun notarytool submit "$(RELEASE_DMG)" --keychain-profile "$(NOTARY_PROFILE)" --wait
	xcrun stapler staple "$(RELEASE_DMG)"
	xcrun stapler validate "$(RELEASE_DMG)"
	hdiutil verify "$(RELEASE_DMG)"
	spctl --assess --type open --context context:primary-signature --verbose=4 "$(RELEASE_DMG)"
	@echo "Release ready: $(RELEASE_DMG)"

clean:
	$(SWIFT_PACKAGE) clean
	rm -rf build
