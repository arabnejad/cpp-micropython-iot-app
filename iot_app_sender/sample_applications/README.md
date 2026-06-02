# Sample Applications

This directory contains small MicroPython applications that can be sent from
Ubuntu to the running IoT App on a Raspberry Pi. Every runnable application is
a self-contained package with:

```text
application_package/
├── app.json
├── main.py
└── README.md
```

The current MQTT protocol sends one Python entry-point file, so each runnable
example keeps all its Python code in `main.py`.

## Shipped default dashboard

The [`default_dashboard`](default_dashboard/README.md) directory explains how
to resend the dashboard installed with IoT App. Its Python files remain in
`iot_app/default_python_application`, so the Raspberry Pi build and MQTT
recovery use one authoritative copy.

## Runnable applications

| Directory | Purpose | Timers | Extra hardware |
|---|---|---|---|
| `clock` | Updates one text box with local time | 1000 ms | None |
| `moving_text_in_frame` | Moves text continuously and relocates a live clock | 30 ms and 1000 ms | None |
| `gamepad_diagnostics` | Shows live joystick, buttons, and hardware details | 50 ms | Adafruit I2C gamepad |
| `button_counter` | Counts new button presses using edge detection | 50 ms | Adafruit I2C gamepad |
| `gamepad_menu` | Navigates a text menu with joystick and buttons | 50 ms | Adafruit I2C gamepad |
| `countdown_timer` | Controls a countdown with the gamepad | 50 ms and 1000 ms | Adafruit I2C gamepad |
| `system_information_carousel` | Rotates through operating-system information | 1000 ms and 5000 ms | None |
| `scheduled_callback_failure` | Shows the emergency screen when a scheduled callback fails | 100 ms | None |
| `traceback_failure` | Raises an import error during startup and displays its traceback | None | None |
| `joystick_visualizer` | Shows live joystick movement with text bars | 50 ms | Adafruit I2C gamepad |

The gamepad examples expect the Adafruit Mini I2C STEMMA QT Gamepad on I2C bus
1 at address `0x50`. Those values are explicit constants near the top of each
`main.py`; change them before sending when the hardware configuration differs.

## Planned applications

The following directories contain design READMEs but no `app.json` or
`main.py`, because the required runtime API does not exist yet:

| Directory | Missing capability |
|---|---|
| `network_weather_dashboard` | Safe asynchronous HTTP/HTTPS requests and app configuration |

Keeping these as documentation avoids presenting a placeholder as a working
application.

## Select an application

The sender reads its application directory from `sender_config.json`. For example,
to send the gamepad diagnostic application, use:

```json
"application": {
  "directory": "sample_applications/gamepad_diagnostics"
}
```

Paths are relative to `sender_config.json`. Validate the selected package
without connecting to MQTT:

```bash
python send_app.py --dry-run
```

Then send it to the Raspberry Pi:

```bash
python send_app.py
```

To restore the shipped default dashboard, use the authoritative application
directory rather than this sample directory:

```json
"application": {
  "directory": "../iot_app/default_python_application"
}
```

See [`default_dashboard/README.md`](default_dashboard/README.md) for the full
recovery example.

Read the README inside an application's directory for its controls, expected
result, and hardware requirements.
