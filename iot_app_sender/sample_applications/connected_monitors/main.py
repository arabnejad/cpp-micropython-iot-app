"""Show every monitor that was connected when IoT App started."""

from iot import display


BACKGROUND = (8, 13, 22)
PANEL_BACKGROUND = (24, 34, 51)
TEXT = (226, 232, 240)
CYAN = (64, 220, 255)


def text_or_fallback(value, fallback="Not reported"):
    """Replace an empty EDID value with text that is useful on screen."""
    return value if value else fallback


def monitor_summary(monitor_number, monitor):
    """Build the lines shown for one monitor."""
    active_marker = " [IoT App display]" if monitor["active"] else ""
    monitor_name = (monitor["manufacturer"] + " " + monitor["model"]).strip()
    current_mode = monitor["current_mode"]
    if current_mode is None:
        current_mode_text = "Current mode: None"
    else:
        current_mode_text = "Current mode: %dx%d @ %d Hz" % (
            current_mode["width"],
            current_mode["height"],
            current_mode["refresh_rate_hz"],
        )

    return "%d. %s%s\n%s\nSerial: %s\nSize: %d x %d mm\n%s\nSupported modes: %d" % (
        monitor_number,
        monitor["connector_name"],
        active_marker,
        text_or_fallback(monitor_name, "Unknown monitor"),
        text_or_fallback(monitor["serial_number"]),
        monitor["physical_width_mm"],
        monitor["physical_height_mm"],
        current_mode_text,
        len(monitor["supported_modes"]),
    )


monitors = display.monitors()
screen_width, screen_height = display.size()
margin = max(20, screen_width // 25)

monitor_sections = []
for monitor_index in range(len(monitors)):
    monitor_sections.append(monitor_summary(monitor_index + 1, monitors[monitor_index]))

screen_text = "Connected monitors at startup: %d\n\n%s" % (
    len(monitors),
    "\n\n".join(monitor_sections),
)

display.clear(color=BACKGROUND)
display.draw_text_box(
    x=margin,
    y=margin,
    width=screen_width - (2 * margin),
    height=screen_height - (2 * margin),
    text=screen_text,
    text_color=TEXT,
    background_color=PANEL_BACKGROUND,
    border_color=CYAN,
    background_opacity=255,
    border_width=2,
    font_size=24 if screen_width >= 1000 else 16,
)

print("Connected-monitor summary started")
