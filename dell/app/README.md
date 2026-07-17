# Dell Control menu-bar app

`Dell Control` is a small native macOS menu-bar app for the Dell U4323QE.
It bundles this repository's original `dellctl` helper; Dell Display and
Peripheral Manager is not installed, launched, linked, or otherwise required.

The first version provides:

- a selectable USB-C, DisplayPort 1/2, or HDMI 1/2 switch target;
- a configurable, exclusive global hotkey (default: `Option-A`);
- a manual **Switch Now** action and visible success/error status;
- persistent settings; and
- Launch at Login through Apple's `SMAppService`.

The app uses Carbon's system hotkey registration, so keyboard capture does
not require Accessibility or Input Monitoring permission. Monitor writes use
the same public IOKit HID path as `dellctl`.

## Build

From `dell/`:

```sh
make app
```

The signed local bundle is created at:

```text
app/build/Dell Control.app
```

## Developer ID release

The release target uses the Developer ID identity and notarization profile
configured for this project:

```sh
make release
```

It signs the nested helper and app inside-out with Hardened Runtime and secure
timestamps, submits a temporary ZIP to Apple's notary service, staples and
validates the returned ticket, runs a Gatekeeper assessment, and creates:

```text
app/build/dist/Dell-Control-0.1.0-macos-arm64.zip
```

The defaults can be overridden without editing the Makefile:

```sh
make release \
  DEVELOPER_ID="Developer ID Application: Example Name (TEAMID)" \
  NOTARY_PROFILE="example-notary"
```

The identity name and Keychain profile name are not secrets. The private key
and notarization credentials remain in Keychain and are never copied into the
repository or release archive.

Install and launch it:

```sh
make install
open "/Applications/Dell Control.app"
```

Then configure each computer's target:

- on the HDMI 1 computer, target USB-C;
- on the USB-C computer, target HDMI 1.

The app requests Launch at Login on its first launch. If macOS requires user
approval, the menu and settings window show a mixed state; click it to open
the Login Items settings.

Stop any older `dellctl hotkey` terminal process before using the app so a
single program owns the shortcut.

## Design boundary

The app launches its bundled `dellctl` only when a switch is requested. It
does not perform a preliminary monitor read. This keeps the first version
small and preserves the live-tested protocol implementation. A future
version can move the HID session into the app if measured process startup
time becomes material.
