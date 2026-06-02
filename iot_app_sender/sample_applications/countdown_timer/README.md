# Countdown Timer

This application combines two independent schedules:

- gamepad input is read every 50 milliseconds; and
- the active countdown advances every 1000 milliseconds.

Start runs or pauses the countdown, Select resets it, A adds ten seconds, and B
subtracts ten seconds. It demonstrates that fast input handling and a slow
clock can coexist without a Python `while True` loop.

It requires the Adafruit gamepad on I2C bus 1 at address `0x50`.

Use this directory in `sender_config.json`:

```json
{
  "directory": "sample_applications/countdown_timer"
}
```
