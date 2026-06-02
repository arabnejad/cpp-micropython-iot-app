"""Show a clock that updates after the application's startup code returns."""

from iot import display, scheduler, system


screen_width, screen_height = display.size()
display.clear(color=(8, 13, 22))


def create_message():
    """Returns the message with the Raspberry Pi's current local time."""
    return "Application received from Ubuntu\n\n%s" % system.current_time()


clock_text_box = display.draw_text_box(
    x=screen_width // 4,
    y=screen_height // 3,
    width=screen_width // 2,
    height=screen_height // 3,
    text=create_message(),
    text_color=(226, 232, 240),
    background_color=(24, 34, 51),
    border_color=(74, 222, 128),
    background_opacity=255,
    border_width=2,
    font_size=28,
)


def update_clock():
    """Changes the existing text box once per second."""
    display.update_text_box(clock_text_box, create_message())


scheduler.every(milliseconds=1000, callback=update_clock)

print("Ubuntu clock application started")
