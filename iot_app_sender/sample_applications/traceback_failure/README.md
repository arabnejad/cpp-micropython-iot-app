# Traceback Failure Demo

This application intentionally imports a module that does not exist. It is the
smallest sample for testing an exception raised while `main.py` starts.

Expected result:

1. IoT App installs the package and replies `accepted`. The sender exits
   successfully; it does not wait for Python execution.
2. MicroPython raises `ImportError: no module named 'os1'`.
3. The Raspberry Pi log prints the traceback and its source line.
4. IoT App destroys the failed MicroPython session.
5. The native emergency screen shows the same traceback. There is no second
   deployment reply for this Python error.

Use this directory in `sender_config.json`:

```json
{
  "directory": "sample_applications/traceback_failure"
}
```

No optional hardware is required.
