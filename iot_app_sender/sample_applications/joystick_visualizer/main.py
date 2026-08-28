"""Represent live joystick movement with text bars and direction names."""

from iot import display, input, scheduler


I2C_BUS_NUMBER = 1
I2C_ADDRESS = 0x50
JOYSTICK_MINIMUM_VALUE = 0
JOYSTICK_MAXIMUM_VALUE = 1023
BAR_CHARACTER_COUNT = 33


gamepad = input.AdafruitMiniI2cGamepad(
    i2c_bus_number=I2C_BUS_NUMBER,
    i2c_address=I2C_ADDRESS,
)
gamepad.connect()
gamepad.calibrate_joystick(number_of_samples=20, dead_zone=100)
gamepad.refresh_input_state()
joystick = gamepad.joystick()

screen_width, screen_height = display.size()
margin = max(20, screen_width // 16)
display.clear(color=(8, 13, 22))


def clamp(value, minimum, maximum):
    """Keeps an axis value inside the range used by the text bar."""
    return max(minimum, min(maximum, value))


def create_axis_bar(axis_value, centre_value):
    """Places O at the live position and a vertical bar (|) at the measured centre."""
    axis_value = clamp(
        axis_value,
        JOYSTICK_MINIMUM_VALUE,
        JOYSTICK_MAXIMUM_VALUE,
    )
    centre_value = clamp(
        centre_value,
        JOYSTICK_MINIMUM_VALUE,
        JOYSTICK_MAXIMUM_VALUE,
    )

    final_index = BAR_CHARACTER_COUNT - 1
    position_index = axis_value * final_index // JOYSTICK_MAXIMUM_VALUE
    centre_index = centre_value * final_index // JOYSTICK_MAXIMUM_VALUE
    characters = ["-"] * BAR_CHARACTER_COUNT
    characters[centre_index] = "|"
    characters[position_index] = "O"
    return "[" + "".join(characters) + "]"


def create_direction(x_position, y_position, centre_x, centre_y, dead_zone):
    """Returns a direction after ignoring small movements near the centre."""
    horizontal = ""
    vertical = ""
    if x_position < centre_x - dead_zone:
        horizontal = "Left"
    elif x_position > centre_x + dead_zone:
        horizontal = "Right"
    if y_position < centre_y - dead_zone:
        vertical = "Down"
    elif y_position > centre_y + dead_zone:
        vertical = "Up"
    if vertical and horizontal:
        return vertical + " + " + horizontal
    return vertical or horizontal or "Centre"


def create_visualizer_text():
    """Builds two live axis bars from the latest cached joystick state."""
    x_position, y_position = joystick.position()
    centre_x, centre_y = joystick.centre()
    dead_zone = joystick.dead_zone()
    return (
        "Text Joystick Visualizer\n\n"
        "X %s  %d\n\n"
        "Y %s  %d\n\n"
        "Centre: %d, %d\n"
        "Dead zone: %d\n"
        "Direction: %s\n\n"
        "O = current position    | = measured centre"
    ) % (
        create_axis_bar(x_position, centre_x),
        x_position,
        create_axis_bar(y_position, centre_y),
        y_position,
        centre_x,
        centre_y,
        dead_zone,
        create_direction(x_position, y_position, centre_x, centre_y, dead_zone),
    )


visualizer_text_box = display.draw_text_box(
    x=margin,
    y=margin,
    width=screen_width - (2 * margin),
    height=screen_height - (2 * margin),
    text=create_visualizer_text(),
    text_color=(226, 232, 240),
    background_color=(24, 34, 51),
    border_color=(244, 114, 182),
    background_opacity=255,
    border_width=2,
    font_size=24,
)


def refresh_joystick_visualizer():
    """Reads the gamepad and changes the two text bars every 50 ms."""
    gamepad.refresh_input_state()
    display.update_text_box(visualizer_text_box, create_visualizer_text())


scheduler.every(milliseconds=50, callback=refresh_joystick_visualizer)

print("Text joystick visualizer started")
