"""Control a countdown timer with independently scheduled gamepad input."""

from iot import display, input, scheduler


I2C_BUS_NUMBER = 1
I2C_ADDRESS = 0x50
INPUT_REFRESH_MILLISECONDS = 50
DEFAULT_COUNTDOWN_SECONDS = 60


gamepad = input.AdafruitMiniI2cGamepad(
    i2c_bus_number=I2C_BUS_NUMBER,
    i2c_address=I2C_ADDRESS,
)
gamepad.connect()
buttons = gamepad.buttons()

remaining_seconds = DEFAULT_COUNTDOWN_SECONDS
countdown_is_running = False
previously_pressed_buttons = ()

screen_width, screen_height = display.size()
display.clear(color=(8, 13, 22))


def create_timer_text():
    """Formats the current countdown and gamepad controls."""
    minutes = remaining_seconds // 60
    seconds = remaining_seconds % 60
    if remaining_seconds == 0:
        state = "Finished"
    else:
        state = "Running" if countdown_is_running else "Paused"

    return (
        "Countdown Timer\n\n"
        "%02d:%02d\n"
        "%s\n\n"
        "Start: run or pause\n"
        "Select: reset to 60 seconds\n"
        "A: add 10 seconds\n"
        "B: subtract 10 seconds"
    ) % (minutes, seconds, state)


timer_text_box = display.draw_text_box(
    x=screen_width // 5,
    y=screen_height // 6,
    width=(screen_width * 3) // 5,
    height=(screen_height * 2) // 3,
    text=create_timer_text(),
    text_color=(226, 232, 240),
    background_color=(24, 34, 51),
    border_color=(251, 146, 60),
    background_opacity=255,
    border_width=2,
    font_size=30,
)


def update_timer_text():
    """Changes the existing timer panel after its state changes."""
    display.update_text_box(timer_text_box, create_timer_text())


def refresh_gamepad_controls():
    """Applies only new button presses to the countdown state."""
    global countdown_is_running
    global previously_pressed_buttons
    global remaining_seconds

    gamepad.refresh_input_state()
    currently_pressed_buttons = buttons.pressed()
    state_changed = False

    if "Start" in currently_pressed_buttons and "Start" not in previously_pressed_buttons:
        if remaining_seconds == 0:
            remaining_seconds = DEFAULT_COUNTDOWN_SECONDS
        countdown_is_running = not countdown_is_running
        state_changed = True

    if "Select" in currently_pressed_buttons and "Select" not in previously_pressed_buttons:
        remaining_seconds = DEFAULT_COUNTDOWN_SECONDS
        countdown_is_running = False
        state_changed = True

    if "A" in currently_pressed_buttons and "A" not in previously_pressed_buttons:
        remaining_seconds += 10
        state_changed = True

    if "B" in currently_pressed_buttons and "B" not in previously_pressed_buttons:
        remaining_seconds = max(0, remaining_seconds - 10)
        if remaining_seconds == 0:
            countdown_is_running = False
        state_changed = True

    previously_pressed_buttons = currently_pressed_buttons
    if state_changed:
        update_timer_text()


def advance_countdown():
    """Removes one second while the countdown is running."""
    global countdown_is_running
    global remaining_seconds

    if not countdown_is_running:
        return

    remaining_seconds = max(0, remaining_seconds - 1)
    if remaining_seconds == 0:
        countdown_is_running = False
    update_timer_text()


scheduler.every(
    milliseconds=INPUT_REFRESH_MILLISECONDS,
    callback=refresh_gamepad_controls,
)
scheduler.every(milliseconds=1000, callback=advance_countdown)

print("Gamepad countdown timer application started")
