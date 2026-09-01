"""Navigate a text menu with the joystick and gamepad buttons."""

from iot import display, input, scheduler, system


I2C_BUS_NUMBER = 1
I2C_ADDRESS = 0x50
INPUT_REFRESH_MILLISECONDS = 50
MENU_ITEMS = (
    "System information",
    "Network information",
    "Display information",
    "Gamepad information",
)


gamepad = input.AdafruitMiniI2cGamepad(
    i2c_bus_number=I2C_BUS_NUMBER,
    i2c_address=I2C_ADDRESS,
)
gamepad.connect()

# The measured centre and dead zone decide when an up/down movement is real.
gamepad.calibrate_joystick(number_of_samples=20, dead_zone=100)
gamepad.refresh_input_state()

joystick = gamepad.joystick()
buttons = gamepad.buttons()

system_information = system.information()
network_interfaces = system.network_interfaces()
active_monitor = display.active_monitor()
active_monitor_mode = active_monitor["current_mode"]

selected_item_index = 0
joystick_is_ready_for_another_move = True
previously_pressed_buttons = ()
detail_text = "Move the joystick up or down. Press A to open an item."

screen_width, screen_height = display.size()
margin = max(20, screen_width // 20)
display.clear(color=(8, 13, 22))


def details_for_selected_item():
    """Returns the information associated with the highlighted menu entry."""
    if selected_item_index == 0:
        return "%s\n%s\nLinux %s, %s" % (
            system_information["device_model"],
            system_information["operating_system"],
            system_information["kernel_version"],
            system_information["architecture"],
        )

    if selected_item_index == 1:
        lines = []
        for network_interface in network_interfaces:
            state = "Connected" if network_interface["connected"] else "Disconnected"
            address = network_interface["ipv4_address"] or "No IPv4 address"
            lines.append("%s: %s, %s" % (network_interface["name"], state, address))
        return "\n".join(lines) if lines else "No network interfaces found"

    if selected_item_index == 2:
        return "%s\n%s %s\n%dx%d @ %d Hz" % (
            active_monitor["connector_name"],
            active_monitor["manufacturer"],
            active_monitor["model"],
            active_monitor_mode["width"],
            active_monitor_mode["height"],
            active_monitor_mode["refresh_rate_hz"],
        )

    return "%s\nProduct ID: %d\nI2C bus %d at 0x%02X" % (
        gamepad.model_name(),
        gamepad.firmware_product_id(),
        I2C_BUS_NUMBER,
        I2C_ADDRESS,
    )


def create_menu_text():
    """Builds the complete menu with one visibly selected entry."""
    lines = ["Gamepad Menu", ""]
    for item_index in range(len(MENU_ITEMS)):
        marker = ">" if item_index == selected_item_index else " "
        lines.append("%s %s" % (marker, MENU_ITEMS[item_index]))
    lines.extend(("", detail_text, "", "A: open    B: help    Start: first item"))
    return "\n".join(lines)


menu_text_box = display.draw_text_box(
    x=margin,
    y=margin,
    width=screen_width - (2 * margin),
    height=screen_height - (2 * margin),
    text=create_menu_text(),
    text_color=(226, 232, 240),
    background_color=(24, 34, 51),
    border_color=(167, 139, 250),
    background_opacity=255,
    border_width=2,
    font_size=26,
)


def refresh_menu_input():
    """Reads one gamepad sample and applies any new navigation action."""
    global selected_item_index
    global joystick_is_ready_for_another_move
    global previously_pressed_buttons
    global detail_text

    gamepad.refresh_input_state()
    _, y_position = joystick.position()
    _, centre_y = joystick.centre()
    dead_zone = joystick.dead_zone()
    currently_pressed_buttons = buttons.pressed()
    menu_changed = False

    if centre_y - dead_zone <= y_position <= centre_y + dead_zone:
        joystick_is_ready_for_another_move = True
    elif joystick_is_ready_for_another_move:
        if y_position > centre_y + dead_zone:
            selected_item_index = (selected_item_index - 1) % len(MENU_ITEMS)
        else:
            selected_item_index = (selected_item_index + 1) % len(MENU_ITEMS)
        joystick_is_ready_for_another_move = False
        detail_text = "Press A to open %s." % MENU_ITEMS[selected_item_index]
        menu_changed = True

    if "A" in currently_pressed_buttons and "A" not in previously_pressed_buttons:
        detail_text = details_for_selected_item()
        menu_changed = True
    if "B" in currently_pressed_buttons and "B" not in previously_pressed_buttons:
        detail_text = "Move up/down, press A to open, or press Start to reset."
        menu_changed = True
    if "Start" in currently_pressed_buttons and "Start" not in previously_pressed_buttons:
        selected_item_index = 0
        detail_text = "Returned to the first menu item."
        menu_changed = True

    previously_pressed_buttons = currently_pressed_buttons
    if menu_changed:
        display.update_text_box(menu_text_box, create_menu_text())


scheduler.every(
    milliseconds=INPUT_REFRESH_MILLISECONDS,
    callback=refresh_menu_input,
)

print("Gamepad menu application started")
