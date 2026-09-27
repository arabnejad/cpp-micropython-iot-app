"""Show SeenGreat joystick movement and count new button presses."""

from iot import display, input, scheduler


INPUT_REFRESH_MILLISECONDS = 50
BUTTON_NAMES = ("K1", "K2", "K3", "Press")

controller = input.SeenGreatOledHatController()
controller.connect()

joystick = controller.joystick()
buttons = controller.buttons()
button_press_counts = {button_name: 0 for button_name in BUTTON_NAMES}
previously_pressed_buttons = ()
previous_direction = joystick.direction()

screen_width, screen_height = display.size()
margin = max(16, min(48, screen_width // 25, screen_height // 20))
gap = max(12, margin // 2)
header_height = max(84, screen_height // 8)
panel_y = margin + header_height + gap
panel_height = screen_height - panel_y - margin
panel_width = (screen_width - 2 * margin - gap) // 2
panel_font_size = 28 if screen_width >= 1200 else 20

display.clear(color=(8, 13, 22))

display.draw_text_box(
    x=margin,
    y=margin,
    width=screen_width - 2 * margin,
    height=header_height,
    text="SeenGreat Controls\nMove the stick or press K1, K2, K3, or Press",
    text_color=(226, 232, 240),
    background_color=(24, 34, 51),
    border_color=(74, 222, 128),
    background_opacity=255,
    border_width=2,
    font_size=32 if screen_width >= 1200 else 22,
)


def create_joystick_text():
    """Show the direction and position from the latest input reading."""
    x_position, y_position = joystick.position()
    return (
        "Joystick\n\n"
        "Direction: %s\n\n"
        "X: %d\n"
        "Y: %d\n\n"
        "Push stick for Press."
    ) % (joystick.direction().replace("_", " ").upper(), x_position, y_position)


def create_button_text(pressed_buttons):
    """Show each press count and which buttons are held now."""
    lines = ["Button press counts", ""]
    for button_name in BUTTON_NAMES:
        lines.append("%s: %d" % (button_name, button_press_counts[button_name]))
    held_buttons = ", ".join(pressed_buttons) if pressed_buttons else "None"
    lines.extend(("", "Held now: %s" % held_buttons))
    return "\n".join(lines)


joystick_text_box = display.draw_text_box(
    x=margin,
    y=panel_y,
    width=panel_width,
    height=panel_height,
    text=create_joystick_text(),
    text_color=(226, 232, 240),
    background_color=(24, 34, 51),
    border_color=(96, 165, 250),
    background_opacity=255,
    border_width=2,
    font_size=panel_font_size,
)

button_text_box = display.draw_text_box(
    x=margin + panel_width + gap,
    y=panel_y,
    width=panel_width,
    height=panel_height,
    text=create_button_text(()),
    text_color=(226, 232, 240),
    background_color=(24, 34, 51),
    border_color=(244, 114, 182),
    background_opacity=255,
    border_width=2,
    font_size=panel_font_size,
)


def refresh_controls():
    """Update the panels when a direction or button state changes."""
    global previous_direction
    global previously_pressed_buttons

    controller.refresh_input_state()
    current_direction = joystick.direction()
    currently_pressed_buttons = buttons.pressed()

    if current_direction != previous_direction:
        previous_direction = current_direction
        display.update_text_box(joystick_text_box, create_joystick_text())

    if currently_pressed_buttons != previously_pressed_buttons:
        for button_name in BUTTON_NAMES:
            if (
                button_name in currently_pressed_buttons
                and button_name not in previously_pressed_buttons
            ):
                button_press_counts[button_name] += 1
        previously_pressed_buttons = currently_pressed_buttons
        display.update_text_box(button_text_box, create_button_text(currently_pressed_buttons))


scheduler.every(milliseconds=INPUT_REFRESH_MILLISECONDS, callback=refresh_controls)

print("SeenGreat controls application started")
