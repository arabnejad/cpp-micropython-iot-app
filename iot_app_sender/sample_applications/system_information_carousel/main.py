"""Rotate through operating-system information while showing a live clock."""

from iot import display, scheduler, system


system_information = system.information()
resource_information = system.resources()
network_interfaces = system.network_interfaces()
system_interfaces = system.interfaces()
connected_devices = system.devices()
display_information = display.information()


def format_megabytes(number_of_bytes):
    """Converts a byte count into a compact whole-megabyte value."""
    return "%d MB" % (number_of_bytes // (1024 * 1024))


network_lines = []
for network_interface in network_interfaces:
    network_lines.append(
        "%s: %s\nIPv4: %s"
        % (
            network_interface["name"],
            "Connected" if network_interface["connected"] else "Disconnected",
            network_interface["ipv4_address"] or "Not assigned",
        )
    )

pages = (
    "System\n\n%s\n%s\n%s\nLinux %s, %s"
    % (
        system_information["hostname"],
        system_information["device_model"],
        system_information["operating_system"],
        system_information["kernel_version"],
        system_information["architecture"],
    ),
    "Resources\n\nCPU cores: %d\nLoad: %s\nMemory: %s total\nStorage: %s total"
    % (
        resource_information["logical_cpu_count"],
        "Unavailable"
        if resource_information["one_minute_load_average"] is None
        else "%.2f" % resource_information["one_minute_load_average"],
        format_megabytes(resource_information["total_memory_bytes"]),
        format_megabytes(resource_information["root_storage_total_bytes"]),
    ),
    "Network\n\n%s" % ("\n\n".join(network_lines) if network_lines else "No interfaces"),
    "Display\n\nConnected displays: %d\n%s\n%s %s\n%dx%d @ %d Hz"
    % (
        display_information["connected_display_count"],
        display_information["connector_name"],
        display_information["manufacturer"],
        display_information["model"],
        display_information["width"],
        display_information["height"],
        display_information["refresh_rate_hz"],
    ),
    "Interfaces and devices\n\nI2C: %d\nGPIO controllers: %d\nSPI: %d\nSerial: %d\n\nUSB: %d\nInput: %d\nBlock: %d"
    % (
        system_interfaces["i2c"],
        system_interfaces["gpio_controllers"],
        system_interfaces["spi"],
        system_interfaces["serial"],
        connected_devices["usb"],
        connected_devices["input"],
        connected_devices["block"],
    ),
)
current_page = 0

screen_width, screen_height = display.size()
margin = max(20, screen_width // 20)
header_height = max(80, screen_height // 8)
gap = max(12, screen_height // 50)
display.clear(color=(8, 13, 22))

clock_text_box = display.draw_text_box(
    x=margin,
    y=margin,
    width=screen_width - (2 * margin),
    height=header_height,
    text="System Information  |  %s" % system.current_time(),
    text_color=(226, 232, 240),
    background_color=(24, 34, 51),
    border_color=(74, 222, 128),
    background_opacity=255,
    border_width=2,
    font_size=28,
)

page_text_box = display.draw_text_box(
    x=margin,
    y=margin + header_height + gap,
    width=screen_width - (2 * margin),
    height=screen_height - (2 * margin) - header_height - gap,
    text=pages[current_page],
    text_color=(226, 232, 240),
    background_color=(24, 34, 51),
    border_color=(96, 165, 250),
    background_opacity=255,
    border_width=2,
    font_size=26,
)


def update_clock():
    """Refreshes the header once per second."""
    display.update_text_box(
        clock_text_box,
        "System Information  |  %s" % system.current_time(),
    )


def show_next_page():
    """Shows the next startup snapshot page every five seconds."""
    global current_page
    current_page = (current_page + 1) % len(pages)
    display.update_text_box(page_text_box, pages[current_page])


scheduler.every(milliseconds=1000, callback=update_clock)
scheduler.every(milliseconds=5000, callback=show_next_page)

print("System information carousel started")
