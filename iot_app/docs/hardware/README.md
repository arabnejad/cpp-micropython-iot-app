# Hardware Notes

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
[gamepad setup guide](https://learn.adafruit.com/gamepad-qt/circuitpython-and-python#python-computer-wiring-3119091)
for its 400 kHz recommendation and Raspberry Pi's
[Device Tree parameter documentation](https://www.raspberrypi.com/documentation/computers/configuration.html#dt-parameters)
for the configuration syntax.

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
`createButtonInputMask()` creates the bit for each input, and the `|` operators
join the six bits into one mask. Because the values are `constexpr`, this work
is completed while the C++ program is compiled.

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
2. `markButtonPressedIfInputIsLow()` checks each input. The inputs are
   active-low, so zero means the button is pressed.
3. The driver adds the corresponding `GamepadButton` value directly to the
   pressed-button mask.
4. `updateButtons()` stores the completed application-level button state.

Application code can use either an `AdafruitMiniI2cGamepad` object or a
`GameController` reference to read the gamepad without depending on its
hardware details:

```cpp
const bool aIsPressed =
    gamepad.buttons().isPressed(iot::input::GamepadButton::A);
```

The application does not need to know that this gamepad wires A to processor
input 5.

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

The driver sees that physical input 5 is low and adds `GamepadButton::A` to the
pressed-button mask. Each enum value is already the mask for that button:

```text
X = 1, Y = 2, A = 4, B = 8, Select = 16, Start = 32
```

`GamepadButton::A` is `1U << 2U`, which is bit 2:

```text
Logical A: 0x00000004 = 0b00000000 00000000 00000000 00000100
```

The physical input mask describes this gamepad's fixed internal wiring. The
logical mask uses `GamepadButton` and describes which named buttons the user is
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
