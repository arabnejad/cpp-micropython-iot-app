# Gamepad Diagnostics

This application shows the joystick direction, pressed buttons, processor ID,
product ID, and firmware date code. The C++ gamepad driver uses the calibrated
X/Y readings internally to calculate the direction. The Python application does
not need to read or interpret those values. It refreshes the gamepad over I2C
every 50 milliseconds.

Required hardware:

- Adafruit Mini I2C STEMMA QT Gamepad;
- I2C bus 1 enabled on the Raspberry Pi; and
- the gamepad available at address `0x50`.

Do not touch the joystick while the application starts. It takes 20 samples to
measure the centre position. Change `I2C_BUS_NUMBER` or `I2C_ADDRESS` at the top
of `main.py` when the hardware uses different values.

Use this directory in `sender_config.json`:

```json
{
  "directory": "sample_applications/gamepad_diagnostics"
}
```
