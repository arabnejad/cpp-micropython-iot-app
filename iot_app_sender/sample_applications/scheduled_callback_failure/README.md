# Scheduled callback failure

This application raises `RuntimeError` from its first scheduled
callback. It verifies that an exception occurring after `main.py` returns is
still treated as an application failure. There is no countdown screen.

Expected result:

1. The application starts and its first callback raises the planned exception.
2. The Raspberry Pi log prints the Python traceback.
3. IoT App destroys the failed external MicroPython session.
4. The C++ runtime shows the traceback on its native emergency screen.
5. No Python application remains running.

The sender may already have reported `started` because startup completed before
the scheduled failure. Observe the Raspberry Pi display and log for the later
failure. No optional hardware is required. Use the separate
`traceback_failure` sample to test an exception during `main.py` startup.

Use this directory in `sender_config.json`:

```json
{
  "directory": "sample_applications/scheduled_callback_failure"
}
```
