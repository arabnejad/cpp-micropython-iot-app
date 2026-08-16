"""Move text continuously and place a live clock at changing positions."""

from iot import display, scheduler, system


screen_width, screen_height = display.size()
display.clear(color=(8, 13, 22))

frame_margin = max(20, min(screen_width, screen_height) // 12)
frame_x = frame_margin
frame_y = frame_margin
frame_width = screen_width - (2 * frame_margin)
frame_height = screen_height - (2 * frame_margin)
frame_padding = max(12, min(frame_width, frame_height) // 30)

moving_box_width = min(max(120, frame_width // 4), frame_width - (2 * frame_padding))
moving_box_height = min(max(70, frame_height // 6), frame_height - (2 * frame_padding))
clock_box_width = min(max(240, frame_width // 5), frame_width - (2 * frame_padding))
clock_box_height = min(max(80, frame_height // 6), frame_height - (2 * frame_padding))

moving_text_minimum_x = frame_x + frame_padding
moving_text_maximum_x = frame_x + frame_width - frame_padding - moving_box_width
moving_text_minimum_y = frame_y + frame_padding
moving_text_maximum_y = frame_y + frame_height - frame_padding - moving_box_height

clock_minimum_x = frame_x + frame_padding
clock_maximum_x = frame_x + frame_width - frame_padding - clock_box_width
clock_minimum_y = frame_y + frame_padding
clock_maximum_y = frame_y + frame_height - frame_padding - clock_box_height

current_x = moving_text_minimum_x
current_y = moving_text_minimum_y
horizontal_step = max(2, screen_width // 240)
vertical_step = max(2, screen_height // 240)


def choose_clock_position(time_text):
    """Uses the changing time text to choose a random-looking position."""
    horizontal_position_count = max(1, (clock_maximum_x - clock_minimum_x) + 1)
    vertical_position_count = max(1, (clock_maximum_y - clock_minimum_y) + 1)

    x = clock_minimum_x + (abs(hash(time_text)) % horizontal_position_count)
    y = clock_minimum_y + (
        abs(hash("vertical-position:" + time_text)) % vertical_position_count
    )
    return x, y


current_clock_text = system.current_time()
current_clock_x, current_clock_y = choose_clock_position(current_clock_text)

# This empty text box provides the fixed background and border for the motion.
display.draw_text_box(
    x=frame_x,
    y=frame_y,
    width=frame_width,
    height=frame_height,
    text="",
    background_color=(16, 23, 38),
    border_color=(64, 220, 255),
    background_opacity=255,
    border_width=2,
)

moving_text_box = display.draw_text_box(
    x=current_x,
    y=current_y,
    width=moving_box_width,
    height=moving_box_height,
    text="Moving text",
    text_color=(255, 247, 237),
    background_color=(154, 52, 18),
    border_color=(251, 146, 60),
    background_opacity=255,
    border_width=2,
    font_size=24,
)

# LVGL draws newer widgets above older widgets on the same screen. The clock
# is created after the moving text box, so the clock appears on top whenever
# their positions overlap. Moving either box later does not change this order.
clock_text_box = display.draw_text_box(
    x=current_clock_x,
    y=current_clock_y,
    width=clock_box_width,
    height=clock_box_height,
    text=current_clock_text,
    text_color=(226, 232, 240),
    font_size=24,
)


def move_text_box_inside_frame():
    """Moves the text once and reverses direction when it reaches an edge."""
    global current_x, current_y, horizontal_step, vertical_step

    next_x = current_x + horizontal_step
    if next_x < moving_text_minimum_x or next_x > moving_text_maximum_x:
        horizontal_step = -horizontal_step
        next_x = current_x + horizontal_step

    next_y = current_y + vertical_step
    if next_y < moving_text_minimum_y or next_y > moving_text_maximum_y:
        vertical_step = -vertical_step
        next_y = current_y + vertical_step

    current_x = next_x
    current_y = next_y
    display.move_text_box(moving_text_box, current_x, current_y)


def update_clock_at_random_position():
    """Refreshes the time and moves its box somewhere else in the frame."""
    current_time = system.current_time()
    new_clock_x, new_clock_y = choose_clock_position(current_time)
    display.update_text_box(clock_text_box, current_time)
    display.move_text_box(clock_text_box, new_clock_x, new_clock_y)


scheduler.every(milliseconds=30, callback=move_text_box_inside_frame)
scheduler.every(milliseconds=1000, callback=update_clock_at_random_position)

print("Moving text and random-position clock application started")
