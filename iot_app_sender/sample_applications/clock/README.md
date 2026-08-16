# Clock

This is the simplest scheduled application. It draws one text box and updates
that same widget with the Raspberry Pi's local date and time once per second.

It demonstrates:

- deploying an application from Ubuntu;
- reading `system.current_time()`;
- retaining the widget ID returned by `display.draw_text_box()`;
- changing existing text with `display.update_text_box()`; and
- registering a repeating callback with `scheduler.every()`.

No optional hardware is required.

Use this directory in `sender_config.json`:

```json
{
  "directory": "sample_applications/clock"
}
```
