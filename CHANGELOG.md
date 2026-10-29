# Changelog

All notable changes to this project will be documented in this file.

## Unreleased
- Added: Fahrenheit option and Invert Display Colors toggle (desktop tray menu, saved on device; defaults in `app_config.h`)
- Added: physical MX key inputs on CN1/P3 (see docs/KEYS_WIRING.md)
- Fixed: sub-zero temperatures displayed as 2xx values
- Fixed: resistive touch boards now run calibration on first boot when none is saved (previously only when BOOT was held at power-up)
- Added: tap the weather panel on the device to toggle C/F
- Fixed: `npm install` in app/ failed because `postinstall` called an undeclared `electron-rebuild`
- Added: DEVELOPMENT.md and scripts/setup-dev.sh
- Added: widget layouts: the board can show pages of widgets from a small JSON layout sent over USB, with safe mode (see docs/WIDGETS.md). Serial packets 0x0B-0x11 (upload), board ack 0x10.
- Added: widget builder on the web page (templates, drag and resize, undo, export/import, dry run).
- Added: Phase 3 widgets: typing cat, pomodoro, countdown, stopwatch, date, focus sign (LED), reminders, buttons with 20 actions, Spotify controls and album art, pictures (8 slots, backgrounds too), 18 ASCII pets from claude-desktop-buddy (MIT). Packets 0x12 (date) and 0x13 (board event).
- Added: tap the cat (or a pet) to pet it; "Waiting cat" setting: cheerful (default) or sleepy (original).
- Added: `esp32dev_ble` build: the board is also a Bluetooth keyboard for button shortcuts and media keys.
- Changed: layout uploads are 3.3x faster (free-space reading cached during uploads).

The format is based on Keep a Changelog, and this project adheres to Semantic Versioning.

## [Unreleased]

## [1.0.5] - 2026-05-24
### Changed
- Faster tap response lower lag
- Update the background + sound effect when climate changed.

## [1.0.4] - 2026-04-24
### Changed
- High cpu usage on windows client app fixed.
- No change on macOS silicon

## [1.0.3] - 2026-02-24
### Added
- Firmware version check packet (host sends 0xFF 0x00; device replies with version string).
- 'Client App' and 'Firmware' new version update notification.

### Changed
- WAV playback volume now follows global volume setting.
- Add connect and disconnect wav sound effects.


## [1.0.2] - 2025-11-19
### Added
- Initial public release.
