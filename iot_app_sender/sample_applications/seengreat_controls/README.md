# SeenGreat Controls

This application shows the SeenGreat 1.3-inch OLED HAT's joystick direction
and position in one panel. A second panel counts presses of K1, K2, K3, and
Press (pushing the joystick in), and shows which buttons are held. Holding a
button counts as one press; releasing and pressing it again adds another.
The screen updates only when the input changes.
It creates the SeenGreat controller directly. If the HAT is not connected,
the application reports an error instead of reading an Adafruit gamepad.

Select this directory in `sender_config.json`:

```json
"application": {
  "directory": "sample_applications/seengreat_controls"
}
```

The [Adafruit joystick visualizer](../joystick_visualizer/README.md) is a
separate example for the Adafruit Mini I2C gamepad.
