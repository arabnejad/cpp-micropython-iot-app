# Traceback Failure Demo

This application intentionally imports a module that does not exist. It is the
smallest sample for testing an exception raised while `main.py` starts.

Expected result:

1. MicroPython raises `ImportError: no module named 'os1'`.
2. The Raspberry Pi terminal prints the traceback and its source line.
3. IoT App destroys the failed MicroPython session.
4. The native emergency screen shows the same traceback.
5. The sender reports `failed`.

Use this directory in `sender_config.json`:

```json
{
  "directory": "sample_applications/traceback_failure"
}
```

No optional hardware is required.
