# Button Counter

This application counts new presses of X, Y, A, B, Select, and Start. Holding a
button down counts once because the app compares the current sample with the
previous sample. This is called edge detection and is the same technique a
menu or game uses to avoid repeating one press every 50 milliseconds.

It requires the Adafruit gamepad on I2C bus 1 at address `0x50`. Change the two
constants at the top of `main.py` if necessary.

Use this directory in `sender_config.json`:

```json
{
  "directory": "sample_applications/button_counter"
}
```
