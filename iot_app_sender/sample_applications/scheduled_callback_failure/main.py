"""Raise a planned callback exception so IoT App can show its error screen."""

from iot import scheduler


def raise_planned_callback_error():
    """Fails after startup so this still tests the scheduled-callback path."""
    raise RuntimeError("Planned scheduled failure from the sample app")


scheduler.every(milliseconds=100, callback=raise_planned_callback_error)

print("Failure recovery demonstration started")
