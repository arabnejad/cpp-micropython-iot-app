# Moving Text in a Frame

This application draws a fixed frame and moves one text box diagonally inside
it. The text reverses direction whenever it reaches a horizontal or vertical
edge. A second box shows the Raspberry Pi's current local time and moves to a
new pseudo-random position once per second.

It demonstrates:

- drawing a fixed frame and a separate text box;
- retaining the widget ID returned by `display.draw_text_box()`;
- moving the same widget with `display.move_text_box(widget_id, x, y)`;
- calculating movement limits from the current display size; and
- running a 30 millisecond animation and a separate 1000 millisecond clock
  update.

The callbacks move the existing LVGL widgets. They do not create another text
box on every update, so the number of widgets stays constant. The clock uses
two hashes of the changing time text to calculate a random-looking horizontal
and vertical position, so it does not need an optional MicroPython random
module. No optional hardware is required.

The clock uses the transparent, borderless defaults, leaving only its text
visible when it passes over another widget. The frame and moving box explicitly
request solid backgrounds and two-pixel borders.

An application can later remove any of these boxes with
`display.delete_text_box(widget_id)`. This animation keeps all three boxes
because they are used for the application's full lifetime.

Use this directory in `sender_config.json`:

```json
{
  "directory": "sample_applications/moving_text_in_frame"
}
```
