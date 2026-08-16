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
uses a five-sample median filter, separate movement and release thresholds, and
confirmation of joystick and button states so input noise does not change the
selected item.

Use this directory in `sender_config.json`:

```json
{
  "directory": "sample_applications/gamepad_menu"
}
```
