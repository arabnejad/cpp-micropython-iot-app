"""Show a simple system summary when IoT App starts."""

from iot import display, scheduler, system


BACKGROUND = (8, 13, 22)
PANEL_BACKGROUND = (24, 34, 51)
TEXT = (226, 232, 240)
MUTED_TEXT = (166, 178, 196)
CYAN = (64, 220, 255)
GREEN = (74, 222, 128)
PURPLE = (167, 139, 250)
ORANGE = (251, 146, 60)
BLUE = (96, 165, 250)
PINK = (244, 114, 182)


def format_duration(total_seconds):
    """Change an uptime such as 6138 seconds into 01:42:18."""
    hours = total_seconds // 3600
    minutes = (total_seconds % 3600) // 60
    seconds = total_seconds % 60
    return "%02d:%02d:%02d" % (hours, minutes, seconds)


def format_storage_size(number_of_bytes):
    """Shorten a byte count so it fits on the dashboard."""
    number_of_megabytes = number_of_bytes // (1024 * 1024)
    if number_of_megabytes < 1024:
        return "%d MB" % number_of_megabytes
    whole_gigabytes = number_of_megabytes // 1024
    gigabyte_tenths = (number_of_megabytes % 1024) * 10 // 1024
    return "%d.%d GB" % (whole_gigabytes, gigabyte_tenths)


def text_or_fallback(value, fallback="Unavailable"):
    """Use friendly text when Linux did not provide a value."""
    return value if value else fallback


def main():
    """Read the system details, draw the dashboard, and start its timers."""
    system_information = system.information()
    resource_information = system.resources()
    system_interface_counts = system.interfaces()
    connected_device_counts = system.devices()
    application_information = system.app_information()
    connected_monitors = display.monitors()
    active_monitor = display.active_monitor()
    active_mode = active_monitor["current_mode"]

    screen_width, screen_height = display.size()

    # Use fewer columns on small screens so the panels still have enough room.
    column_count = 3 if screen_width >= 1000 else 2
    row_count = 2 if column_count == 3 else 3
    screen_margin = max(16, min(48, screen_width // 30))
    panel_gap = max(10, min(24, screen_width // 70))
    header_height = max(72, min(116, screen_height // 8))
    footer_height = max(38, min(56, screen_height // 16))
    panel_top = screen_margin + header_height + panel_gap
    panel_area_height = screen_height - panel_top - footer_height - screen_margin - panel_gap
    panel_width = (
        screen_width - (2 * screen_margin) - ((column_count - 1) * panel_gap)
    ) // column_count
    panel_height = (panel_area_height - ((row_count - 1) * panel_gap)) // row_count

    if screen_width >= 1600 and screen_height >= 900:
        panel_font_size = 24
        header_font_size = 32
    elif screen_width >= 1000:
        panel_font_size = 20
        header_font_size = 24
    else:
        panel_font_size = 14
        header_font_size = 20


    def draw_text_box(x, y, width, height, text, border_color, font_size, text_color=TEXT):
        """Draw a text box using the dashboard colours and spacing."""
        return display.draw_text_box(
            x=x,
            y=y,
            width=width,
            height=height,
            text=text,
            text_color=text_color,
            background_color=PANEL_BACKGROUND,
            border_color=border_color,
            background_opacity=255,
            border_width=2,
            font_size=font_size,
        )


    def draw_panel(panel_index, title, lines, border_color):
        """Place one information panel in the next grid position."""
        column = panel_index % column_count
        row = panel_index // column_count
        x = screen_margin + column * (panel_width + panel_gap)
        y = panel_top + row * (panel_height + panel_gap)
        content = title + "\n\n" + "\n".join(lines)
        return draw_text_box(x, y, panel_width, panel_height, content, border_color, panel_font_size)


    display.clear(color=BACKGROUND)


    def create_header_text():
        """Build the header text with the current time and uptime."""
        return "IoT App  |  Running  |  %s\n%s  |  %s  |  Uptime %s" % (
            system.current_time(),
            system_information["hostname"],
            application_information["application_name"],
            format_duration(system.uptime_seconds()),
        )


    header_text_box = draw_text_box(
        screen_margin,
        screen_margin,
        screen_width - (2 * screen_margin),
        header_height,
        create_header_text(),
        GREEN,
        header_font_size,
    )


    def update_header_time():
        """Refresh the time in the existing header box."""
        display.update_text_box(header_text_box, create_header_text())


    scheduler.every(milliseconds=1000, callback=update_header_time)

    system_lines = [
        text_or_fallback(system_information["device_model"]),
        text_or_fallback(system_information["operating_system"]),
        "%s, %s" % (
            text_or_fallback(system_information["kernel_version"]),
            text_or_fallback(system_information["architecture"]),
        ),
    ]

    cpu_temperature_celsius = resource_information["cpu_temperature_celsius"]
    temperature_text = (
        "Unavailable"
        if cpu_temperature_celsius is None
        else "%d C" % int(cpu_temperature_celsius + 0.5)
    )
    one_minute_load_average = resource_information["one_minute_load_average"]
    load_text = (
        "Unavailable" if one_minute_load_average is None else "%.2f" % one_minute_load_average
    )
    used_memory = max(
        0,
        resource_information["total_memory_bytes"] - resource_information["available_memory_bytes"],
    )
    used_storage = max(
        0,
        resource_information["root_storage_total_bytes"]
        - resource_information["root_storage_available_bytes"],
    )
    resource_lines = [
        "CPU: %d cores, %s" % (resource_information["logical_cpu_count"], temperature_text),
        "Load: %s" % load_text,
        "Memory: %s / %s"
        % (
            format_storage_size(used_memory),
            format_storage_size(resource_information["total_memory_bytes"]),
        ),
        "Storage: %s / %s"
        % (
            format_storage_size(used_storage),
            format_storage_size(resource_information["root_storage_total_bytes"]),
        ),
    ]


    def create_network_lines():
        """Read Linux again and build the current network status text."""
        network_interfaces = system.network_interfaces()
        network_lines = []
        connected_interface = None

        # Prefer an interface with an address because it can already be used by
        # SSH and application deployment.
        for network_interface in network_interfaces:
            if network_interface["connected"] and network_interface["ipv4_address"]:
                connected_interface = network_interface
                break

        # An interface can be linked before DHCP gives it an address.
        if connected_interface is None:
            for network_interface in network_interfaces:
                if network_interface["connected"]:
                    connected_interface = network_interface
                    break

        if connected_interface is None:
            network_lines.append("No connected interface")
        else:
            network_lines.append("%s: Connected" % connected_interface["name"])
            network_lines.append(
                "IPv4: %s" % text_or_fallback(connected_interface["ipv4_address"], "Not assigned")
            )
            if connected_interface["speed_megabits_per_second"] is not None:
                network_lines.append("Speed: %d Mbps" % connected_interface["speed_megabits_per_second"])

        for network_interface in network_interfaces:
            if not network_interface["connected"]:
                network_lines.append("%s: Disconnected" % network_interface["name"])
                break
        return network_lines

    monitor_name = (active_monitor["manufacturer"] + " " + active_monitor["model"]).strip()
    display_lines = [
        "Connected displays: %d" % len(connected_monitors),
        text_or_fallback(active_monitor["connector_name"]),
        text_or_fallback(monitor_name, "Unknown monitor"),
        "%dx%d @ %d Hz"
        % (
            active_mode["width"],
            active_mode["height"],
            active_mode["refresh_rate_hz"],
        ),
    ]

    interface_lines = [
        "I2C interfaces: %d" % system_interface_counts["i2c"],
        "GPIO controllers: %d" % system_interface_counts["gpio_controllers"],
        "SPI interfaces: %d" % system_interface_counts["spi"],
        "Serial interfaces: %d" % system_interface_counts["serial"],
    ]

    device_lines = [
        "USB devices: %d" % connected_device_counts["usb"],
        "Input devices: %d" % connected_device_counts["input"],
        "Block devices: %d" % connected_device_counts["block"],
    ]

    draw_panel(0, "System", system_lines, CYAN)
    network_text_box = draw_panel(1, "Network", create_network_lines(), BLUE)
    draw_panel(2, "Display", display_lines, PURPLE)
    draw_panel(3, "Resources at startup", resource_lines, ORANGE)
    draw_panel(4, "Interfaces", interface_lines, PINK)
    draw_panel(5, "Devices", device_lines, GREEN)


    def update_network_panel():
        """Refresh the existing network panel with the latest Linux state."""
        network_text = "Network\n\n" + "\n".join(create_network_lines())
        display.update_text_box(network_text_box, network_text)


    scheduler.every(milliseconds=5000, callback=update_network_panel)

    footer_text = "IoT App %s  |  MicroPython %s  |  LVGL %s" % (
        application_information["app_version"],
        application_information["micropython_version"],
        application_information["lvgl_version"],
    )
    footer_y = screen_height - footer_height - screen_margin
    draw_text_box(
        screen_margin,
        footer_y,
        screen_width - (2 * screen_margin),
        footer_height,
        footer_text,
        CYAN,
        14 if panel_font_size <= 20 else 20,
        MUTED_TEXT,
    )

    print("Embedded MicroPython system dashboard started")
    print("Display size:", screen_width, "x", screen_height)
    print("Hostname:", system_information["hostname"])


main()
