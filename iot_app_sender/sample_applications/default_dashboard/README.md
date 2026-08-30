# Default Dashboard

The default dashboard is the Python application that IoT App starts after the
C++ runtime launches. It shows system, network, display, resource, interface,
and device information.

The application files are kept in:

```text
iot_app/default_python_application/
├── app.json
└── main.py
```

This sample directory contains only this guide. It does not contain another
copy of the application, so the version sent over MQTT is always the same one
that CMake, Buildroot, and Yocto install on the Raspberry Pi.

## Restore the dashboard over MQTT

Set the application directory in `iot_app_sender/sender_config.json` to:

```json
{
  "application": {
    "directory": "../iot_app/default_python_application"
  }
}
```

The path is relative to `sender_config.json`. Check the application package
without contacting the Raspberry Pi:

```bash
cd iot_app_sender
python send_app.py --dry-run
```

Then send it:

```bash
python send_app.py
```

The running C++ process stops the current Python application and starts the
default dashboard received through MQTT. Restarting IoT App also starts the
installed default dashboard as usual.
