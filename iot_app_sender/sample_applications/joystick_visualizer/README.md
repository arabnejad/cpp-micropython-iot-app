# Joystick Visualizer

The current display API cannot move an existing rectangle, so this working
version uses two text bars. The letter `O` shows the current axis position, and
a vertical bar (`|`) shows the measured centre. It also displays the numeric values, dead zone, and
direction name.

The gamepad is sampled every 50 milliseconds. It requires the Adafruit gamepad
on I2C bus 1 at address `0x50`. Leave the joystick untouched during startup
calibration.

A later movable-widget API can turn this into a graphical dot without changing
the gamepad or scheduler design.

Use this directory in `sender_config.json`:

```json
{
  "directory": "sample_applications/joystick_visualizer"
}
```
