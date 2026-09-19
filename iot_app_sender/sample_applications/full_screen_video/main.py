from iot import display


VIDEO_FILE_PATH = "/data/iot-app/videos/demo.mp4"
SCREEN_BACKGROUND_COLOR = (8, 13, 22)
SCREEN_MARGIN = 120


screen_width, screen_height = display.size()
message_width = screen_width - (2 * SCREEN_MARGIN)
message_height = screen_height - (2 * SCREEN_MARGIN)


try:
    display.play_video(VIDEO_FILE_PATH)
except RuntimeError as playback_error:
    display.clear(color=SCREEN_BACKGROUND_COLOR)
    display.draw_text_box(
        x=SCREEN_MARGIN,
        y=SCREEN_MARGIN,
        width=message_width,
        height=message_height,
        text="Video could not be played\n\n" + str(playback_error),
        text_color=(255, 255, 255),
        background_color=(69, 10, 10),
        border_color=(255, 90, 90),
        background_opacity=255,
        border_width=3,
        font_size=32,
    )
else:
    display.clear(color=SCREEN_BACKGROUND_COLOR)
    display.draw_text_box(
        x=SCREEN_MARGIN,
        y=SCREEN_MARGIN,
        width=message_width,
        height=message_height,
        text="Video finished",
        text_color=(255, 255, 255),
        background_color=(18, 28, 45),
        border_color=(0, 220, 170),
        background_opacity=255,
        border_width=3,
        font_size=32,
    )
