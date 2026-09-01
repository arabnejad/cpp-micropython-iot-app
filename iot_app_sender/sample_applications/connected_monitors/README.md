# Connected Monitors

This application shows every monitor that was connected when IoT App started.
For each monitor it displays the connector, monitor name, serial number,
physical size, current mode, and number of supported modes. The monitor used by
IoT App is marked with `[IoT App display]`.

The application needs no optional hardware. Its monitor list is a startup
snapshot, so connecting or disconnecting a monitor does not change the screen.
Restart IoT App to perform another display scan.

Use this directory in `sender_config.json`:

```json
{
  "directory": "sample_applications/connected_monitors"
}
```
