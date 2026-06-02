"""Count each new gamepad button press without recounting a held button."""

from iot import display, input, scheduler


I2C_BUS_NUMBER = 1
I2C_ADDRESS = 0x50
INPUT_REFRESH_MILLISECONDS = 50
BUTTON_NAMES = ("X", "Y", "A", "B", "Select", "Start")


gamepad = input.AdafruitMiniI2cGamepad(
    i2c_bus_number=I2C_BUS_NUMBER,
    i2c_address=I2C_ADDRESS,
)
gamepad.connect()
buttons = gamepad.buttons()

button_press_counts = {
    "X": 0,
    "Y": 0,
    "A": 0,
    "B": 0,
    "Select": 0,
    "Start": 0,
}
previously_pressed_buttons = ()

screen_width, screen_height = display.size()
margin = max(20, screen_width // 16)
display.clear(color=(8, 13, 22))


def create_counter_text(currently_pressed_buttons):
    """Builds the panel showing every counter and the current button state."""
    lines = ["Gamepad Button Counter", ""]
    for button_name in BUTTON_NAMES:
        lines.append("%-7s : %d" % (button_name, button_press_counts[button_name]))
    lines.append("")
    lines.append(
        "Held now: %s"
        % (", ".join(currently_pressed_buttons) if currently_pressed_buttons else "None")
    )
    return "\n".join(lines)


counter_text_box = display.draw_text_box(
    x=margin,
    y=margin,
    width=screen_width - (2 * margin),
    height=screen_height - (2 * margin),
    text=create_counter_text(()),
    text_color=(226, 232, 240),
    background_color=(24, 34, 51),
    border_color=(96, 165, 250),
    background_opacity=255,
    border_width=2,
    font_size=28,
)


def refresh_button_counts():
    """Counts only transitions from released to pressed."""
    global previously_pressed_buttons

    gamepad.refresh_input_state()
    currently_pressed_buttons = buttons.pressed()

    for button_name in BUTTON_NAMES:
        if (
            button_name in currently_pressed_buttons
            and button_name not in previously_pressed_buttons
        ):
            button_press_counts[button_name] += 1

    previously_pressed_buttons = currently_pressed_buttons
    display.update_text_box(
        counter_text_box,
        create_counter_text(currently_pressed_buttons),
    )


scheduler.every(
    milliseconds=INPUT_REFRESH_MILLISECONDS,
    callback=refresh_button_counts,
)

print("Gamepad button counter application started")
