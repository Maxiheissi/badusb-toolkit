# badusb-toolkit

A BadUSB implementation for the ESP32 using TinyUSB and LittleFS. The device emulates a USB HID keyboard and automatically executes keystroke scripts stored on the internal filesystem.

---

## Hardware Requirements

- ESP32 board with USB support (e.g. ESP32-S2, ESP32-S3)
- USB cable

---

## How It Works

1. The ESP32 connects to a host PC via USB and identifies itself as a HID keyboard
2. On connection, it reads a script file (`example.txt`) from LittleFS
3. Each line in the script is interpreted as a command and executed as keyboard input
4. The BOOT button (GPIO0) can be used to re-run the script at any time

---

## Script Commands

Scripts are plain text files stored as `example.txt` on the LittleFS partition.

| Command | Description | Example |
|---|---|---|
| `MOD <char> <MODIFIER>` | Send a key with a modifier | `MOD a CTRL` → CTRL+A |
| `RET` | Press the Enter key | `RET` |
| `WAIT <ms>` | Wait for given milliseconds | `WAIT 1000` |
| `# comment` | Comment line, will be ignored | `# open run dialog` |
| Any other text | Typed out as plain text | `Hello World` |

### Available Modifiers

| Modifier | Description |
|---|---|
| `CTRL` | Left Control |
| `SHIFT` | Left Shift |
| `ALT` | Left Alt |
| `GUI` | Windows / Command key |
| `ALTGR` | Right Alt (AltGr) |

### Example Script

```
# Open Run dialog
MOD r GUI
WAIT 500

# Type command
cmd
RET
WAIT 1000

# Run a command
echo Hello World
RET
```

---

## Keyboard Layout

The ASCII to HID mapping is based on the **QWERTZ** layout (German/Austrian keyboards). Note that Y and Z are swapped compared to QWERTY.

Supported characters:
- a–z, A–Z
- 0–9
- German umlauts: ä ö ü Ä Ö Ü ß
- Special characters: `! " § $ % & / ( ) = ? + # - . , ; : _ *`
- AltGr characters: `@ € { } [ ] \ ~`

---


## Configuration

Key parameters can be adjusted at the top of `main.c`:

| Define | Default | Description |
|---|---|---|
| `APP_BUTTON` | `GPIO_NUM_0` | GPIO pin for the trigger button |
| `MAX_TOKENS` | `10` | Maximum tokens per script line |
| `DELTA_TIME` | `12` | Delay in ms between key presses |

Increase `DELTA_TIME` if the target machine misses key presses.

---

## Disclaimer

This project is intended for **educational purposes and authorized security testing only**. Do not use this tool on systems you do not own or have explicit permission to test.
This README was created by anthropics sonnet ai model
