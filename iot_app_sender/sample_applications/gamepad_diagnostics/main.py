"""Show live values and hardware details from the Adafruit I2C gamepad."""

from iot import display, input, scheduler


I2C_BUS_NUMBER = 1
I2C_ADDRESS = 0x50
INPUT_REFRESH_MILLISECONDS = 50

BACKGROUND = (8, 13, 22)
PANEL = (24, 34, 51)
TEXT = (226, 232, 240)
GREEN = (74, 222, 128)


gamepad = input.AdafruitMiniI2cGamepad(
    i2c_bus_number=I2C_BUS_NUMBER,
    i2c_address=I2C_ADDRESS,
)
gamepad.connect()

# Leave the joystick untouched while these startup samples measure its centre.
gamepad.calibrate_joystick(number_of_samples=20, dead_zone=100)
gamepad.refresh_input_state()

joystick = gamepad.joystick()
buttons = gamepad.buttons()

model_name = gamepad.model_name()
processor_hardware_id = gamepad.processor_hardware_id()
firmware_product_id = gamepad.firmware_product_id()
firmware_date_code = gamepad.firmware_date_code()

screen_width, screen_height = display.size()
margin = max(20, screen_width // 20)
display.clear(color=(BACKGROUND[0], BACKGROUND[1], BACKGROUND[2]))


def create_status_text():
    """Reads the latest cached state and formats it for the diagnostic panel."""
    pressed_buttons = buttons.pressed()
    pressed_text = ", ".join(pressed_buttons) if pressed_buttons else "None"

    return (
        "%s\n\n"
        "Connection: %s\n"
        "I2C bus: %d    address: 0x%02X\n\n"
        "Direction: %s\n\n"
        "Pressed buttons: %s\n\n"
        "Processor hardware ID: %d\n"
        "Firmware product ID: %d\n"
        "Firmware date code: 0x%04X"
    ) % (
        model_name,
        "Connected" if gamepad.is_connected() else "Disconnected",
        I2C_BUS_NUMBER,
        I2C_ADDRESS,
        joystick.direction(),
        pressed_text,
        processor_hardware_id,
        firmware_product_id,
        firmware_date_code,
    )


status_text_box = display.draw_text_box(
    x=margin,
    y=margin,
    width=screen_width - (2 * margin),
    height=screen_height - (2 * margin),
    text=create_status_text(),
    text_color=(TEXT[0], TEXT[1], TEXT[2]),
    background_color=(PANEL[0], PANEL[1], PANEL[2]),
    border_color=(GREEN[0], GREEN[1], GREEN[2]),
    background_opacity=255,
    border_width=2,
    font_size=24,
)


def refresh_gamepad_status():
    """Reads one new I2C sample and refreshes the existing diagnostic panel."""
    gamepad.refresh_input_state()
    display.update_text_box(status_text_box, create_status_text())


scheduler.every(
    milliseconds=INPUT_REFRESH_MILLISECONDS,
    callback=refresh_gamepad_status,
)

print("Gamepad diagnostic application started")
