# Xteink Custom Firmware

Custom firmware for the Xteink X4 e-paper reader, forked from [CrossPoint Reader](https://github.com/crosspoint-reader/crosspoint-reader). Based on upstream v1.1.0.

## What We Changed

### Sleep/Wake Screens
- **Boot screen**: `\[T]/` (Praise the Sun) + "Hello, my king" in Bookerly 18 Bold
- **Sleep popup**: "Goodbye, my king [T]7" replaces "Entering Sleep..."
- **Default sleep screen**: `[T]7` + "Goodbye, my king" replaces logo + "SLEEPING"

### Home Screen — Mac OS 9 Finder Style
- Classic Finder window with title bar stripes, close box, info bar
- Menu bar at top: current book reading progress ("Reading: XX%") on left + battery icon with percentage on right
- 3-column icon grid with unique per-item icons:
  - **Book cover thumbnail** (44px, with double-border selection) for "Currently Reading" — falls back to document icon if no cover
  - **Hard drive icon** (rectangle with divider + LED dot) for Browse Files
  - **Folder icon** (tab + body) for Recents, OPDS Browser
  - **Paper airplane icon** (right-pointing dart with fold line) for File Transfer
  - **Three sliders icon** (horizontal lines with knobs at different positions) for Settings
- Selected item uses classic Mac highlight (inverted icon + label with generous padding)
- Window title: "Xteink HD"
- Info bar: three-column classic Mac layout — "X items" (left), "X.X GB in disk" (center), "XX.X GB available" (right)
- SD card storage read via `Storage.cardSizeBytes()` / `Storage.freeSpaceBytes()` (added to HalStorage + SDCardManager)
- Drop shadow on window for authentic look
- `loadRecentBooks(1)` + `loadRecentCovers(44)` loads most recent book with cover thumbnail
- Book progress loaded from `/.crosspoint/epub_<hash>/progress.bin` using `Epub::calculateProgress()`
- `mockup.py` generates a PNG preview of the home screen layout (Pillow required)

## Build & Flash

Requires: Python 3.12+, PlatformIO

```bash
pip install platformio
git submodule update --init --recursive

# Build (PYTHONIOENCODING needed on Windows to avoid PlatformIO Unicode crash)
PYTHONIOENCODING=utf-8 pio run

# Flash (adjust COM port as needed)
PYTHONIOENCODING=utf-8 pio run --target upload --upload-port COM3

# Preview home screen layout (optional)
pip install Pillow
python mockup.py --selected 0
```

## Key Files

| File | Purpose |
|---|---|
| `src/activities/boot_sleep/BootActivity.cpp` | Wake/boot screen |
| `src/activities/boot_sleep/SleepActivity.cpp` | Sleep screen + popup |
| `src/activities/home/HomeActivity.cpp` | Home screen (Mac OS 9 style) |
| `src/activities/home/HomeActivity.h` | Home screen header |
| `src/CrossPointSettings.h` | All device settings |
| `lib/hal/HalGPIO.cpp` | Wake-up reason detection |
| `lib/hal/HalPowerManager.cpp` | Deep sleep entry, GPIO holds |
| `platformio.ini` | Build configuration |

## Hardware

- **MCU**: ESP32-C3 (RISC-V, 320KB RAM, 16MB Flash)
- **Display**: 800x480 e-ink panel, portrait mode = 480x800
- **Input**: Physical buttons (no touch screen)
- **Storage**: MicroSD card

## Drawing API (GfxRenderer)

- `drawRect(x, y, w, h)` / `fillRect(x, y, w, h, state)` — rectangles
- `drawLine(x1, y1, x2, y2)` — lines
- `drawText(fontId, x, y, text, black, style)` — positioned text
- `drawCenteredText(fontId, y, text, black, style)` — centered text
- `clearScreen()` / `invertScreen()` / `displayBuffer(mode)` — screen ops
- `getTextWidth(fontId, text)` / `truncatedText(fontId, text, maxW)` — measurement

## Fonts

| Font | ID | Sizes | Notes |
|---|---|---|---|
| Bookerly | BOOKERLY_*_FONT_ID | 12-18 | Serif, used for sleep/wake |
| Ubuntu | UI_10_FONT_ID, UI_12_FONT_ID | 10, 12 | UI font |
| Noto Sans | SMALL_FONT_ID (8), NOTOSANS_* | 8-18 | Sans-serif |
| OpenDyslexic | OPENDYSLEXIC_* | 8-14 | Accessibility |

## Known Electrical Issues

### Spurious Deep Sleep Wake-ups
The device can randomly wake from deep sleep when near strong EMI sources (e.g. attached to a phone via MagSafe-style mount). Root cause: EMI from the phone couples into GPIO 3 (power button wake pin), and the ESP32-C3's internal pull-up (~45kΩ) is too weak to reject it.

**Fix 1 — GPIO pull-downs during sleep (`HalPowerManager::startDeepSleep()`):**
- GPIO 1, 2, 0 switched to `INPUT_PULLDOWN` before sleep to prevent floating pins from coupling noise into adjacent GPIO 3
- `gpio_hold_en()` locks all pin states through deep sleep
- `gpio_deep_sleep_hold_en()` keeps RTC holds active
- On wake, `gpio_hold_dis()` releases holds in `InputManager::begin()` and `HalPowerManager::begin()`

**Fix 2 — Wake classification bug (`HalGPIO::getWakeupReason()`):**
- GPIO deep sleep wakes without USB connected were classified as `Other` instead of `PowerButton`, skipping all power button verification and letting EMI wakes boot the device unchecked
- Removed the `usbConnected` requirement from the `WAKEUP_GPIO + RST_DEEPSLEEP` condition

**Fix 3 — Raw button check on wake (`verifyPowerButtonDuration()` in main.cpp):**
- Added a `digitalRead` of GPIO 3 at the start of verification, before any fast-path bypass
- By the time the CPU boots (~100-200ms), EMI spikes are gone but real button presses are still held
- If button is not physically pressed, immediately returns to deep sleep

**If wake-ups still occur**, next steps would be:
- Add an external 10kΩ pull-up resistor on GPIO 3 (power button) to strengthen the pull-up
- Check for PCB crosstalk between GPIO traces near the MagSafe magnet area
- Consider adding a small capacitor (100nF) on GPIO 3 to filter high-frequency noise

## Constraints

- Flash at ~68.3% (down from 97.3% after v1.1.0 font compression)
- Fonts only support Latin, Cyrillic, basic symbols — no CJK/box-drawing Unicode
- Screen is 1-bit (black/white) with 4-level grayscale for images only
