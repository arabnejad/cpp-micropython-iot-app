# Hardware Notes

Use this reference for supported boards, wiring, device permissions, and
hardware troubleshooting. For Python controller classes and their methods,
see the [API reference](../micropython-api/README.md).

On Raspberry Pi OS, the user running IoT App normally needs membership in the
`i2c` group to open `/dev/i2c-1`. SeenGreat also needs GPIO access through the
`gpio` group. The
[Raspberry Pi OS guide](../raspberry-pi-os/README.md#display-device-access)
shows the command that grants the display, I2C, input, and GPIO groups used by the
application.

## Troubleshooting

### `/dev/i2c-1` does not exist on Raspberry Pi OS

Gamepad applications open I2C bus 1 when they start. If the interface is
disabled, the application stops and the emergency screen shows an error like:

```text
RuntimeError: Could not open /dev/i2c-1: No such file or directory
```

Enable the Raspberry Pi ARM I2C interface and reboot:

```bash
sudo raspi-config nonint do_i2c 0
sudo reboot
```

Here, `0` means enable. After rebooting, verify that the operating system
created the bus device:

```bash
ls -l /dev/i2c-1
```

Install the I2C command-line tools if they are not already available, then scan
bus 1:

```bash
sudo apt install i2c-tools
i2cdetect -y 1
```

The Adafruit gamepad normally appears as `50` at address `0x50`. If
`/dev/i2c-1` exists but the scan does not show `50`, check the gamepad's power,
SDA, SCL, and ground connections.

You can also run `sudo raspi-config` and select `Interface Options`, `I2C`, and
`Enable`. See the official
[Raspberry Pi configuration guide](https://www.raspberrypi.com/documentation/configuration/raspberry-pi.html)
for both methods.

### Buttons appear pressed when nobody is pressing them

First check whether the Raspberry Pi has reported a power or temperature
problem:

```bash
watch -n 1 'vcgencmd get_throttled; vcgencmd measure_temp'
```

`throttled=0x0` means that the Pi has not detected undervoltage or throttling.
If false button presses still happen without other CPU-heavy programs running,
the framebuffer recorder or other application load is probably not the cause.

Raspberry Pi OS normally runs I2C at 100 kHz. Adafruit recommends 400 kHz for
this gamepad. Check the existing settings before editing them:

```bash
grep -n 'i2c' /boot/firmware/config.txt
sudo nano /boot/firmware/config.txt
```

Replace the existing ARM I2C setting with this line:

```ini
dtparam=i2c_arm=on,i2c_arm_baudrate=400000
```

Do not leave another `i2c_arm_baudrate` setting elsewhere in the file. Reboot
to apply the change:

```bash
sudo reboot
```

Test the gamepad before starting FFmpeg or another high-load program. If false
presses continue, stop `iot_app` and test the gamepad with a separate,
known-working gamepad program. Do not run both programs at the same time
because they would access the same I2C device.

- If both programs report false presses, check the STEMMA QT cable, power,
  ground, SDA, SCL, and the gamepad itself.
- If the separate program is stable, but `iot_app` reports false presses,
  investigate the `iot_app` gamepad driver and polling logic.
- If all buttons appear pressed together, the active-low inputs may have been
  read as all zeroes. Random individual buttons more often suggest an
  intermittent connection or signal problem.

See Adafruit's
[Raspberry Pi wiring section](https://learn.adafruit.com/gamepad-qt/circuitpython-and-python#python-computer-wiring-3148962)
for the connections and its
[gamepad setup guide](https://learn.adafruit.com/gamepad-qt/circuitpython-and-python)
for the 400 kHz recommendation. Raspberry Pi's
[Device Tree parameter documentation](https://www.raspberrypi.com/documentation/computers/configuration.html#dt-parameters)
explains the configuration syntax.

## SeenGreat 1.3-inch OLED HAT

This project supports the SeenGreat HAT with a 128-by-64 SH1106 OLED, a
five-way joystick, and three buttons. The OLED uses I2C; the joystick and
buttons use separate GPIO lines. Use the
[manufacturer's HAT(A) guide](https://seengreat.com/wiki/178/1-3inch-oled-hat-a)
to identify the board and its mode switches.

### Wiring and interface settings

Power off the Pi before fitting or removing the HAT. Fit it onto the 40-pin
header in the correct orientation. The board uses 3.3 V and ground from that
header. Set both mode switches to the I2C position; the manufacturer's table
labels this as `0` for both SW1 and SW2. IoT App uses I2C, so the SPI setting
will not work with this driver.

The Raspberry Pi 4 build is configured to use these connections. GPIO numbers
below are **BCM numbers**, not physical header positions or wiringPi numbers:

| Signal | BCM GPIO | What IoT App uses it for |
|---|---:|---|
| SDA | 2 | OLED data on I2C bus 1 |
| SCL | 3 | OLED clock on I2C bus 1 |
| D/C | 25 | Driver holds this line high |
| Reset | 17 | Pulsed low, then high when the OLED opens |
| Joystick up | 19 | Up direction |
| Joystick down | 13 | Down direction |
| Joystick left | 26 | Left direction |
| Joystick right | 6 | Right direction |
| Joystick centre | 5 | `Press` button |
| K1 | 16 | `K1` button |
| K2 | 20 | `K2` button |
| K3 | 21 | `K3` button |

SeenGreat's online guide lists D/C as GPIO25 and reset as GPIO17, matching
this configuration. However, its linked
[demo code](https://seengreat.com/upload/file/130%2013OLED/13OELD_Demo_codes.zip)
(`python/config.py`) and
[schematic](https://seengreat.com/upload/file/130%2013OLED/1.3inch%20OLED%20HAT%EF%BC%88A%EF%BC%89.pdf)
use D/C GPIO24 and reset GPIO25. The demo confirms the eight control pins
above, including joystick right GPIO6, which is missing from the online
guide's table. These vendor sources disagree on the OLED control pins;
check your board's actual wiring before changing the project's settings.
The table describes IoT App's configuration, not a confirmed pinout for every
board sold under this name.

IoT App expects the OLED at address `0x3c` on `/dev/i2c-1` and uses
`/dev/gpiochip0` for GPIO. These values and the input pins are defined in
[`controller_hardware_settings.h`](../../src/input/controller_hardware_settings.h).
Change that file and rebuild IoT App if the connections differ. Check the
GPIO device and its line numbering before using this setup on another Pi
model.

The driver requests pull-ups for the input lines. A pressed switch pulls its
line low. Before opening I2C, this driver claims GPIO25 and GPIO17, holds
GPIO25 high, and pulses GPIO17 low then high. An I2C scan does not perform
this initialization. These are the current driver's actions; they do not
resolve the conflicting vendor pinouts above.
The shared `SeenGreatOledHat` object keeps these output lines claimed while
either the status display or controller is using the board.

### Enable access on Raspberry Pi OS

Enable I2C using the instructions above, then check the device permissions:

```bash
ls -l /dev/i2c-1 /dev/gpiochip0
groups
sudo usermod -aG i2c,gpio "$USER"
```

Log out and back in after changing group membership. Raspberry Pi documents
this requirement in its
[GPIO permissions guide](https://www.raspberrypi.com/documentation/computers/raspberry-pi.html#permissions).
The [Raspberry Pi OS guide](../raspberry-pi-os/README.md#display-device-access)
also covers the display permissions needed by IoT App.

### Use the controls and status display

A Python application selects the SeenGreat controller explicitly:

```python
from iot import input

controller = input.SeenGreatOledHatController()
controller.connect()
controller.refresh_input_state()
print(controller.joystick().direction())
print(controller.buttons().pressed())
```

The joystick is digital and needs no calibration. The button names are `K1`,
`K2`, `K3`, and `Press`; they are not mapped to Adafruit's A/B/X buttons.
See the [SeenGreat controls example](../../../iot_app_sender/sample_applications/seengreat_controls/README.md)
for repeated input reads and the [input API](../micropython-api/README.md#iotinput)
for the common methods.

The native runtime checks for the OLED at startup and updates it about once
per second with the IP address, application name, CPU temperature, and local
time. This is separate from the HDMI display and does not require a Python
controller. If the OLED cannot be opened, IoT App logs the reason and
continues. A Python application that explicitly requires SeenGreat still
reports an error if that board is unavailable.

### SeenGreat troubleshooting

| Symptom | What to check |
|---|---|
| `/dev/i2c-1` is missing | Enable I2C and reboot as described above. |
| `/dev/gpiochip0` is missing | Check the kernel's GPIO devices and the configured device path. On another Pi model, also check which chip owns the header lines. |
| Opening either device reports `Permission denied` | Check `ls -l` and `groups`, add the user to `i2c` and `gpio`, then log in again. |
| Claiming GPIO lines reports `Device or resource busy` | Stop other board demos or services using those lines. Do not run a vendor demo alongside IoT App. |
| Claiming input lines reports `Invalid argument` | Check that the kernel and GPIO driver support the requested input pull-ups and the configured line offsets. |
| The OLED is blank or an I2C write fails | Check power, board orientation, both I2C mode switches, address `0x3c`, and GPIO25/GPIO17 access. A missing address in a scan does not by itself identify which of these failed. |
| The OLED works but controls do not | Check that the app calls `connect()` and `refresh_input_state()`, and that the eight input lines are available. |

The startup message `SeenGreat OLED is not available: ...` includes the
underlying error. Start with that message before changing wiring or software
settings. Shut down IoT App before changing connections or running another
program that controls the HAT.

## Adafruit Mini I2C STEMMA QT Gamepad button inputs

The Raspberry Pi talks to the gamepad over I2C. Inside the gamepad, each
button is connected to a numbered processor input. One bit selects each input:

```text
X input 6:      0x00000040 = 0b00000000 00000000 00000000 01000000
Y input 2:      0x00000004 = 0b00000000 00000000 00000000 00000100
A input 5:      0x00000020 = 0b00000000 00000000 00000000 00100000
B input 1:      0x00000002 = 0b00000000 00000000 00000000 00000010
Select input 0: 0x00000001 = 0b00000000 00000000 00000000 00000001
Start input 16: 0x00010000 = 0b00000000 00000001 00000000 00000000
```

Combining these values creates `allGamepadButtonInputsMask`:

```text
0x00010067 = 0b00000000 00000001 00000000 01100111
```

This fixed mask identifies inputs 0, 1, 2, 5, 6, and 16.
The driver keeps each physical input number beside its `ControllerButton` value
in `gamepadButtonInputMappings`. `createAllGamepadButtonInputsMask()` loops
through that table, creates a bit for each input, and joins them into this
mask. The mask is calculated while the C++ program is compiled.

### What happens during connection

When `connect()` runs:

1. `configureButtonInputs()` converts the fixed mask into four bytes using
   `encodeUint32AsBigEndianBytes()`.
2. The driver sends those bytes over I2C to configure the six button inputs and
   enable their pull-up resistors.
3. `readPressedButtonMask()` reads the current state so the application
   starts with correct button information.

### What happens during an input refresh

When `refreshInputState()` runs:

1. `readButtonInputLevels()` reads all six inputs over I2C.
2. A loop uses `gamepadButtonInputMappings` to check each input. The inputs
   are active-low, so zero means the button is pressed.
3. The driver adds the corresponding `ControllerButton` value directly to the
   pressed-button mask.
4. `updateButtons()` stores the completed application-level button state.

Application code can use either an `AdafruitMiniI2cGamepad` object or a
`InputControllerBase` reference to read the gamepad without depending on its
hardware details:

```cpp
iot::input::AdafruitMiniI2cGamepad gamepad;
gamepad.connect();
gamepad.calibrateJoystick();
gamepad.refreshInputState();

const auto direction = gamepad.joystick().direction();
const bool aIsPressed =
    gamepad.buttons().isPressed(iot::input::ControllerButton::A);
```

The application does not need to know that this gamepad wires A to processor
input 5.

The I2C bus, Adafruit address, SeenGreat OLED address, and SeenGreat GPIO pins
are kept in `iot_app/src/input/controller_hardware_settings.h`. Python apps do
not pass these values. Update that file and rebuild if the wiring changes.

This is the native C++ API. In a MicroPython application, the equivalent code
uses Python naming:

```python
gamepad_buttons = gamepad.buttons()
a_is_pressed = gamepad_buttons.is_pressed("A")
```

### Example: pressing A

With no buttons pressed, every selected input is high:

```text
0x00010067 = 0b00000000 00000001 00000000 01100111
```

A is connected to input 5. Pressing A makes that input low:

```text
A input value: 0x00000020 = 0b00000000 00000000 00000000 00100000
After A press: 0x00010047 = 0b00000000 00000001 00000000 01000111
```

The driver sees that physical input 5 is low and adds `ControllerButton::A` to the
pressed-button mask. Each enum value is already the mask for that button:

```text
X = 1, Y = 2, A = 4, B = 8, Select = 16, Start = 32
```

`ControllerButton::A` is `1U << 2U`, which is bit 2:

```text
Logical A: 0x00000004 = 0b00000000 00000000 00000000 00000100
```

The physical input mask describes this gamepad's fixed internal wiring. The
logical mask uses `ControllerButton` and describes which named buttons the user is
currently pressing.

## Seesaw protocol values

The Raspberry Pi communicates with the gamepad over I2C. The gamepad's Seesaw
processor organizes its internal registers into modules named Status, GPIO, and
ADC. Names such as `gpioModuleAddress` describe that internal register group;
they do not mean the Raspberry Pi is controlling the buttons through its own
GPIO pins.

### Product and firmware date

The processor returns the product ID and firmware date in one 32-bit value:

```text
31                       16 15                         0
+--------------------------+----------------------------+
| Product ID               | Encoded firmware date      |
+--------------------------+----------------------------+
```

For example, the gamepad used during development returned `0x166F7A97`:

```text
0x166F = 5743          product ID
0x7A97 = 2023-05-15   encoded firmware date
```

The firmware date is stored as bits rather than text. Adafruit's
[`getProdDatecode()` implementation](https://github.com/adafruit/Adafruit_Seesaw/blob/master/Adafruit_seesaw.cpp#L172-L179)
shows how those bits are decoded.

### Byte order

Multi-byte Seesaw values use big-endian order, which means the highest part of
the number is sent first. For example:

```text
0x12345678 <-> {0x12, 0x34, 0x56, 0x78}
```

`encodeUint32AsBigEndianBytes()` prepares a 32-bit mask for an I2C write.
`decodeBigEndianBytesAsUint32()` rebuilds a 32-bit value from four bytes read
from the gamepad.
