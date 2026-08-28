# Gamepad Menu

This application demonstrates a small stateful user interface controlled by
the gamepad:

- move the joystick up or down to highlight a menu entry;
- return the joystick to the centre before moving again;
- press A to show the selected information;
- press B to show help; and
- press Start to return to the first entry.

It requires the Adafruit gamepad on I2C bus 1 at address `0x50`. Leave the
joystick untouched for about one second during startup calibration. Navigation
reads the gamepad every 50 milliseconds. One calibrated dead zone decides when
the joystick has moved, and the joystick must return to the centre before the
next menu movement. Button actions run only when a button changes from released
to pressed.

Use this directory in `sender_config.json`:

```json
{
  "directory": "sample_applications/gamepad_menu"
}
```
