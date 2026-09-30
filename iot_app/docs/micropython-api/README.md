# MicroPython API guide

Python applications running inside IoT App can import the project-owned
`iot` module:

```python
from iot import display, input, network, scheduler, system
```

These modules are built into the IoT App executable. They are not part of
standard MicroPython or CPython, and they are not available when the same
script is run with a normal `python3` command.

The [system design document](../system-design/README.md) explains the C++
ownership, threading, deployment, and failure handling behind these modules.

The public modules are:

| Module | What it provides |
|---|---|
| `iot.display` | Screen size, monitor details, text boxes, filled areas, JPEG images, and full-screen video |
| `iot.input` | Controller input from the Adafruit gamepad or SeenGreat OLED HAT |
| `iot.network` | HTTP and HTTPS downloads with size, timeout, and optional SHA-256 checks |
| `iot.scheduler` | Repeating callbacks for live applications |
| `iot.system` | Linux, resource, network, device, and runtime information |

Names beginning with `_iot_` are private implementation modules. Applications
should always import through `iot` as shown above.

Complete applications using these APIs are available in
[`iot_app_sender/sample_applications`](../../../iot_app_sender/sample_applications/README.md).

## Application lifecycle

An application's `main.py` creates its screen and then returns. Returning does
not stop the application. IoT App keeps the MicroPython interpreter alive and
runs callbacks registered with `scheduler.every()`.

Do not put a permanent loop in `main.py`, and do not create a background thread
for screen updates. A long-running loop would prevent IoT App from processing
scheduled work or switching to a newly received application.

All positions and sizes in the display API are measured in pixels. RGB colour
components use integers from `0` to `255`.

## `iot.display`

The display module draws through the screen service owned by IoT App. Drawing
commands are sent to the LVGL render thread; Python code does not call LVGL
directly. In the traces below, `ScreenManager::runRenderLoop()` takes each
queued command and calls the named LVGL backend method.

### `display.clear()`

```python
from iot import display

display.clear(color=(0, 0, 0))
```

Removes everything drawn by the application and fills the screen with one
colour. It returns `None`.

All previous text-box and image IDs become invalid when this call returns,
even if the render thread has not cleared the screen yet.

| Parameter | Required? | Meaning |
|---|---|---|
| `color` | No, default `(0, 0, 0)` | `(red, green, blue)` background colour; each value is from 0 to 255 |

The colour arguments are keyword-only.

```text
Python: display.clear()
 │
 v
MicroPython module table maps "clear" to display_clear_object [mod_iot_display.c]
 │
 v
display_clear() checks the colour values [mod_iot_display.c]
 │
 v
iot_display_clear() passes the colour across the C/C++ boundary [display_cpp_bridge.cpp]
 │
 v
ScreenManager::clear() replaces pending commands with a screen-clear command [screen_manager.cpp]
 │
 v
LvglFramebufferRenderBackend::clear() clears the LVGL screen [lvgl_framebuffer_render_backend.cpp]
```

### `display.draw_text_box()`

```python
from iot import display

widget_id = display.draw_text_box(
    x=40,
    y=40,
    width=500,
    height=100,
    text="IoT App is running",
    text_color=(255, 255, 255),
    background_color=(0, 0, 0),
    border_color=(255, 255, 255),
    background_opacity=0,
    border_width=0,
    font_size=24,
)
```

Creates a text box and returns its positive integer widget ID. Keep this ID if
the box will be updated, moved, or deleted later.

Width and height must be greater than zero. IoT App checks them before queuing
the drawing request and raises `RuntimeError` for a zero or negative size.
Negative positions are allowed; content outside the screen is clipped.

The defaults shown above come from the C++ `TextBoxSpec`. The MicroPython
binding only replaces a style when the application supplies that argument.

| Parameter | Required? | Meaning |
|---|---|---|
| `x` | Yes | Horizontal position of the left edge |
| `y` | Yes | Vertical position of the top edge |
| `width` | Yes | Width of the text box; use a positive value |
| `height` | Yes | Height of the text box; use a positive value |
| `text` | Yes | Text shown in the box |
| `text_color` | No | `(red, green, blue)` text colour; each value is from 0 to 255 |
| `background_color` | No | `(red, green, blue)` box colour; each value is from 0 to 255 |
| `border_color` | No | `(red, green, blue)` border colour; each value is from 0 to 255 |
| `background_opacity` | No | `0` is transparent and `255` is fully solid |
| `border_width` | No | Border thickness in pixels; `0` hides the border |
| `font_size` | No | Requested font size in pixels; must be from 1 to 65535 |

The style arguments after `text` are keyword-only. Position and size values
must fit in a signed 32-bit integer. `border_width` and `font_size` may not be
larger than 65535.

Only fonts compiled into the C++ application are available. The current build
contains Montserrat at 14, 20, 24, and 32 pixels. The requested size selects
one of those fonts:

| Requested `font_size` | Font used |
|---|---|
| 1 to 14 | Montserrat 14 |
| 15 to 20 | Montserrat 20 |
| 21 to 24 | Montserrat 24 |
| 25 to 65535 | Montserrat 32 |

The API does not currently accept a font-family name.

```text
Python: display.draw_text_box()
 │
 v
MicroPython module table maps "draw_text_box" to display_draw_text_box_object [mod_iot_display.c]
 │
 v
display_draw_text_box() parses and checks the Python arguments [mod_iot_display.c]
 │
 v
iot_display_draw_text_box() builds a TextBoxSpec from the C values [display_cpp_bridge.cpp]
 │
 v
ScreenManager::drawTextBox() gives the box an ID and queues it [screen_manager.cpp]
 │
 v
LvglFramebufferRenderBackend::createTextBox() creates the box and label [lvgl_framebuffer_render_backend.cpp]
```

### `display.update_text_box()`

```python
from iot import display

widget_id = display.draw_text_box(
    x=40,
    y=40,
    width=500,
    height=100,
    text="Starting",
)

display.update_text_box(widget_id, "IoT App is ready")
```

Changes the text in an existing text box and returns `None`.

| Parameter | Required? | Meaning |
|---|---|---|
| `widget_id` | Yes | Positive ID returned by `draw_text_box()` |
| `text` | Yes | Replacement text |

```text
Python: display.update_text_box()
 │
 v
MicroPython module table maps "update_text_box" to display_update_text_box_object [mod_iot_display.c]
 │
 v
display_update_text_box() checks the ID and text arguments [mod_iot_display.c]
 │
 v
iot_display_update_text_box() passes the ID and text into C++ [display_cpp_bridge.cpp]
 │
 v
ScreenManager::updateTextBox() checks the ID and queues the text change [screen_manager.cpp]
 │
 v
LvglFramebufferRenderBackend::updateTextBox() changes the LVGL label [lvgl_framebuffer_render_backend.cpp]
```

### `display.move_text_box()`

```python
from iot import display

widget_id = display.draw_text_box(
    x=40,
    y=40,
    width=500,
    height=100,
    text="Move me",
)

display.move_text_box(widget_id, 200, 160)
```

Moves an existing text box to a new absolute position and returns `None`. Its
size and text do not change.

| Parameter | Required? | Meaning |
|---|---|---|
| `widget_id` | Yes | Positive ID returned by `draw_text_box()` |
| `x` | Yes | New horizontal position of the left edge |
| `y` | Yes | New vertical position of the top edge |

```text
Python: display.move_text_box()
 │
 v
MicroPython module table maps "move_text_box" to display_move_text_box_object [mod_iot_display.c]
 │
 v
display_move_text_box() checks the ID and coordinates [mod_iot_display.c]
 │
 v
iot_display_move_text_box() passes the new position into C++ [display_cpp_bridge.cpp]
 │
 v
ScreenManager::moveTextBox() checks the ID and queues the new position [screen_manager.cpp]
 │
 v
LvglFramebufferRenderBackend::moveTextBox() moves the LVGL box [lvgl_framebuffer_render_backend.cpp]
```

### `display.delete_text_box()`

```python
from iot import display

widget_id = display.draw_text_box(
    x=40,
    y=40,
    width=500,
    height=100,
    text="Delete me",
)

display.delete_text_box(widget_id)
```

Deletes one existing text box and returns `None`. The widget ID must not be
used again after this call returns, even if the render thread has not drawn
the deletion yet. If the queue is full and deletion raises an error, the box
has not been scheduled for deletion and its ID remains valid.

| Parameter | Required? | Meaning |
|---|---|---|
| `widget_id` | Yes | Positive ID returned by `draw_text_box()` |

```text
Python: display.delete_text_box()
 │
 v
MicroPython module table maps "delete_text_box" to display_delete_text_box_object [mod_iot_display.c]
 │
 v
display_delete_text_box() checks the ID argument [mod_iot_display.c]
 │
 v
iot_display_delete_text_box() passes the ID into C++ [display_cpp_bridge.cpp]
 │
 v
ScreenManager::deleteTextBox() checks the ID and queues deletion [screen_manager.cpp]
 │
 v
LvglFramebufferRenderBackend::deleteTextBox() removes the LVGL box [lvgl_framebuffer_render_backend.cpp]
```

### Text-box errors

Updating, moving, or deleting an unknown text-box ID raises `RuntimeError`
during that Python call. The same applies to an ID from a deleted box, a
previously cleared screen, or another application. An image ID cannot be used
as a text-box ID. Non-positive IDs are rejected by the Python binding with
`ValueError`.

You can catch these errors and continue drawing:

```python
from iot import display

text_box_id = display.draw_text_box(40, 40, 300, 80, "Temporary text")
display.delete_text_box(text_box_id)

try:
    display.update_text_box(text_box_id, "This box was deleted")
except RuntimeError as error:
    print(error)
    display.draw_text_box(40, 40, 300, 80, "The app is still running")
```

If the app does not catch the error, IoT App stops Python and shows the
traceback on its emergency screen. The invalid request is never sent to
LVGL, so it does not stop the render thread. Unexpected failures inside the
render backend are still treated as runtime failures.

### JPEG images

The display module accepts JPEG image data. It checks the file itself rather
than relying on the filename extension. Other image formats are rejected.

IoT App decodes images with libjpeg-turbo on the main MicroPython thread. The
LVGL render thread continues refreshing the screen during the decode. Once the
pixels are ready, `ScreenManager` sends them to the render thread.

`scale_percent` accepts a value from 13 to 100. A value of 100 keeps the
original size. A smaller value asks libjpeg-turbo for the largest supported
decoder size that does not exceed that percentage. Images are never enlarged.
The minimum is 13 because libjpeg-turbo's smallest supported ratio is one
eighth, or 12.5 percent, while this API accepts whole numbers.

The decoded-image cache can retain up to 32 MiB of pixels for reuse. Drawing
the same unchanged file again with the same size limits normally avoids a
second decode. The cache only evicts pixels that no widget or queued command
still uses.

Replacing an image needs room for the old and new pixels at the same time.
If they cannot fit, the call raises `RuntimeError` and the original image stays
in place. A smaller scale uses less memory. Deleting a widget queues its removal,
so deleting and immediately drawing another large image can still hit the
limit. After the renderer handles the deletion, the unused pixels can be
evicted from the cache.

Clearing the screen, starting another application, or showing the emergency
screen empties the reusable cache. Pixels already used by the renderer stay
alive until it removes those widgets. Temporary decoding buffers and LVGL's
screen buffer also use memory, so 32 MiB is not a total process-memory limit.

| JPEG check | Limit |
|---|---|
| Compressed file | 10 MiB |
| Original width or height | 8192 pixels each |
| Original pixel count | 16,777,216 pixels |
| One decoded pixel buffer | 32 MiB |
| Reusable decoded-image cache | 32 MiB |

IoT App checks the original dimensions before decoding. A smaller scale does
not bypass the original dimension or pixel-count limits.

### `display.draw_image()`

```python
from iot import display, network

downloaded_image = network.download_file("https://example.com/photo.jpg")

image_id = display.draw_image(
    path=downloaded_image["path"],
    x=100,
    y=80,
    scale_percent=75,
)
```

Draws a JPEG as a normal widget and returns its positive image ID. Keep the ID
if the image will be changed, moved, resized, or deleted later.

`draw_image()` reads a local file. It does not download it or choose its
location. `network.download_file()` returns a suitable local path when the
image comes from a URL.

| Parameter | Required? | Meaning |
|---|---|---|
| `path` | Yes | Local path to a file containing JPEG image data |
| `x` | Yes | Horizontal position of the image's left edge |
| `y` | Yes | Vertical position of the image's top edge |
| `scale_percent` | No, default `100` | Largest allowed size, from 13 to 100 percent |

```text
Python: display.draw_image()
 │
 v
MicroPython module table maps "draw_image" to display_draw_image_object [mod_iot_display.c]
 │
 v
display_draw_image() checks the path, position, and scale arguments [mod_iot_display.c]
 │
 v
iot_display_draw_image() turns the C values into a JPEG drawing request [display_cpp_bridge.cpp]
 │
 v
ScreenManager::drawJpegImage() loads or reuses pixels, then queues the image [screen_manager.cpp]
 │
 v
LvglFramebufferRenderBackend::createJpegImage() creates the image widget [lvgl_framebuffer_render_backend.cpp]
```

### Changing a normal image

```python
from iot import display, network

first_download = network.download_file("https://example.com/first.jpg")
second_download = network.download_file("https://example.com/second.jpg")

image_id = display.draw_image(first_download["path"], x=40, y=60)
display.update_image(image_id, second_download["path"])
display.move_image(image_id, 200, 120)
display.set_image_scale(image_id, 50)
display.delete_image(image_id)
```

| Function | What it does |
|---|---|
| `display.update_image(image_id, path)` | Replaces the JPEG but keeps the position and scale limit |
| `display.move_image(image_id, x, y)` | Moves the image without decoding it again |
| `display.set_image_scale(image_id, scale_percent)` | Decodes the current JPEG at a new 13-to-100 percent limit |
| `display.delete_image(image_id)` | Removes the image; its ID must not be used again |

The four image changes follow the same C binding and bridge pattern, but end
at different ScreenManager methods:

```text
Python: display.update_image(image_id, path)
 │
 v
MicroPython module table maps "update_image" to display_update_image_object [mod_iot_display.c]
 │
 v
display_update_image() checks the ID and path arguments [mod_iot_display.c]
 │
 v
iot_display_update_image() passes the ID and path into C++ [display_cpp_bridge.cpp]
 │
 v
ScreenManager::replaceJpegImage() loads the new JPEG and queues the update [screen_manager.cpp]
 │
 v
LvglFramebufferRenderBackend::replaceJpegImage() changes the widget's pixels [lvgl_framebuffer_render_backend.cpp]
```

```text
Python: display.move_image(image_id, x, y)
 │
 v
MicroPython module table maps "move_image" to display_move_image_object [mod_iot_display.c]
 │
 v
display_move_image() checks the ID and coordinates [mod_iot_display.c]
 │
 v
iot_display_move_image() passes the ID and position into C++ [display_cpp_bridge.cpp]
 │
 v
ScreenManager::moveJpegImage() queues the position without decoding again [screen_manager.cpp]
 │
 v
LvglFramebufferRenderBackend::moveJpegImage() moves the image widget [lvgl_framebuffer_render_backend.cpp]
```

```text
Python: display.set_image_scale(image_id, scale_percent)
 │
 v
MicroPython module table maps "set_image_scale" to display_set_image_scale_object [mod_iot_display.c]
 │
 v
display_set_image_scale() checks the ID and scale percentage [mod_iot_display.c]
 │
 v
iot_display_set_image_scale() passes the ID and scale into C++ [display_cpp_bridge.cpp]
 │
 v
ScreenManager::setJpegImageScale() loads pixels at the new scale and queues them [screen_manager.cpp]
 │
 v
LvglFramebufferRenderBackend::replaceJpegImage() shows the newly scaled pixels [lvgl_framebuffer_render_backend.cpp]
```

```text
Python: display.delete_image(image_id)
 │
 v
MicroPython module table maps "delete_image" to display_delete_image_object [mod_iot_display.c]
 │
 v
display_delete_image() checks the ID argument [mod_iot_display.c]
 │
 v
iot_display_delete_image() passes the ID into C++ [display_cpp_bridge.cpp]
 │
 v
ScreenManager::deleteJpegImage() checks the ID and queues deletion [screen_manager.cpp]
 │
 v
LvglFramebufferRenderBackend::deleteJpegImage() removes the image widget [lvgl_framebuffer_render_backend.cpp]
```

### `display.set_background_image()`

```python
from iot import display, network

background_download = network.download_file("https://example.com/background.jpg")

display.set_background_image(
    background_download["path"],
    mode="fit",
    scale_percent=100,
)
```

Places one JPEG behind the normal screen widgets. Calling it again replaces
the current background.

| Parameter | Required? | Meaning |
|---|---|---|
| `path` | Yes | Local path to a file containing JPEG image data |
| `mode` | No, default `"center"` | `"center"`, `"fit"`, or `"tile"` |
| `scale_percent` | No, default `100` | Largest allowed size, from 13 to 100 percent |

The modes work as follows:

- `center` keeps the decoded size, centres the image, and crops any part that
  lies outside the screen. The screen colour remains visible around a smaller
  image.
- `fit` reduces a large image until the whole image fits. It does not enlarge
  a small image.
- `tile` repeats the decoded image across the screen.

`display.clear_background_image()` removes only the background image. Other
widgets remain on screen.

```text
Python: display.set_background_image()
 │
 v
MicroPython module table maps "set_background_image" to display_set_background_image_object [mod_iot_display.c]
 │
 v
display_set_background_image() checks the path, mode, and scale [mod_iot_display.c]
 │
 v
iot_display_set_background_image() converts the mode into a C++ value [display_cpp_bridge.cpp]
 │
 v
ScreenManager::setBackgroundJpegImage() loads the JPEG and queues it [screen_manager.cpp]
 │
 v
LvglFramebufferRenderBackend::setBackgroundJpegImage() shows the background [lvgl_framebuffer_render_backend.cpp]
```

```text
Python: display.clear_background_image()
 │
 v
MicroPython module table maps "clear_background_image" to display_clear_background_image_object [mod_iot_display.c]
 │
 v
display_clear_background_image() forwards the request [mod_iot_display.c]
 │
 v
iot_display_clear_background_image() crosses into C++ [display_cpp_bridge.cpp]
 │
 v
ScreenManager::clearBackgroundJpegImage() queues removal of the background [screen_manager.cpp]
 │
 v
LvglFramebufferRenderBackend::clearBackgroundJpegImage() removes the background widget [lvgl_framebuffer_render_backend.cpp]
```

### `display.play_video()`

```python
from iot import display

display.play_video("/data/iot-app/videos/demo.mp4")

# The old screen was removed while mpv owned the monitor. Draw it again after
# playback returns.
display.clear(color=(8, 13, 22))
display.draw_text_box(40, 40, 600, 100, "Video finished")
```

Plays one local video full-screen and returns after it finishes. H.264 video
in an MP4 container is the format this project tests and supports on the
Raspberry Pi 4. libmpv may open other formats, but IoT App does not guarantee
them. Audio is not played.

The `path` argument must point to a normal local file that the `iot-app` user
can read. To play a file from the internet, download it first with
`network.download_file()` and pass the returned path. That downloader accepts
files up to 10 MiB. A larger video can be copied to persistent storage such as
`/data/iot-app/videos/`.

Video uses the same monitor and mode that IoT App selected during startup.
Before playback, `ScreenManager` stops the LVGL render thread and closes the
framebuffer. libmpv can then use OpenGL and DRM without competing with LVGL.
When playback ends, IoT App reopens the framebuffer and starts the render
thread again.

Stopping LVGL removes the current widgets and decoded JPEG cache. Text-box and
image IDs created before `play_video()` must not be used afterwards. Draw the
next screen after the function returns. Those IDs referred to LVGL objects
that were destroyed when the framebuffer closed, so clearing them is safer
than keeping IDs which no longer point to a valid widget.

The call is synchronous. Scheduled Python callbacks and received application
processing wait until the video ends. The MQTT network thread can still receive
and queue a deployment message during that time. Stopping the IoT App process
interrupts playback, so a long file does not delay service shutdown.

If playback fails, IoT App reopens the framebuffer and raises `RuntimeError`.
When mpv provides an error message, the exception includes up to 1 KiB of that
detail. This can help explain problems such as an unsupported video or a file
which mpv cannot read. Python may catch the error and draw another screen. An
unhandled error follows the normal application-failure path and shows the
emergency screen.

```text
Python: display.play_video(path)
 │
 v
MicroPython module table maps "play_video" to display_play_video_object [mod_iot_display.c]
 │
 v
display_play_video() converts the Python path to text [mod_iot_display.c]
 │
 v
iot_display_play_video() passes the path into C++ and handles errors [display_cpp_bridge.cpp]
 │
 v
ScreenManager::playExclusiveVideoAndWait() releases the framebuffer [screen_manager.cpp]
 │
 v
MpvExclusiveVideoPlayer::playVideoAndWait() uses libmpv for full-screen playback [mpv_exclusive_video_player.cpp]
 │
 v
ScreenManager::start() restores framebuffer rendering after playback [screen_manager.cpp]
```

### `display.fill_area()`

```python
from iot import display

display.fill_area(
    x=40,
    y=40,
    width=300,
    height=150,
    color=(0, 0, 0),
)
```

Draws a solid rectangle and returns `None`. A filled area does not currently
have an ID and cannot be moved, updated, or deleted separately. Use
`display.clear()` to remove it.

Each call adds a new rectangle, even when it covers the same position as an
earlier one. A screen can have at most 128 filled rectangles, including those
still waiting to be drawn. Further calls raise `RuntimeError` until the screen
is cleared. Invalid requests and requests rejected by a full drawing queue do
not count toward this limit.

Use filled areas for shapes drawn once when setting up a screen. For changes
from a timer, update or move an existing text box or image. If you need to
rebuild the whole screen, call `display.clear()` first; this also removes all
text boxes and images.

| Parameter | Required? | Meaning |
|---|---|---|
| `x` | Yes | Horizontal position of the left edge |
| `y` | Yes | Vertical position of the top edge |
| `width` | Yes | Rectangle width; use a positive value |
| `height` | Yes | Rectangle height; use a positive value |
| `color` | No, default `(0, 0, 0)` | `(red, green, blue)` fill colour; each value is from 0 to 255 |

The colour arguments are keyword-only.

```text
Python: display.fill_area()
 │
 v
MicroPython module table maps "fill_area" to display_fill_area_object [mod_iot_display.c]
 │
 v
display_fill_area() checks the rectangle and colour arguments [mod_iot_display.c]
 │
 v
iot_display_fill_area() builds a FilledAreaSpec [display_cpp_bridge.cpp]
 │
 v
ScreenManager::fillArea() queues the rectangle for the render thread [screen_manager.cpp]
 │
 v
LvglFramebufferRenderBackend::fillArea() creates the filled LVGL area [lvgl_framebuffer_render_backend.cpp]
```

### `display.size()`

```python
from iot import display

width, height = display.size()
```

Takes no arguments and returns the active display size as a `(width, height)`
tuple. IoT App uses the Linux display mode that was already active; this
function does not change the resolution.

```text
Python: display.size()
 │
 v
MicroPython module table maps "size" to display_size_object [mod_iot_display.c]
 │
 v
display_size() builds a Python tuple for the width and height [mod_iot_display.c]
 │
 v
iot_display_size() reads the startup display width and height from the active context [display_cpp_bridge.cpp]
```

### `display.monitors()`

```python
from iot import display

monitors = display.monitors()
print("Connected monitors:", len(monitors))
```

Takes no arguments and returns a list containing every monitor found when IoT
App started. For example:

```python
[
    {
        "connector_name": "HDMI-A-1",
        "manufacturer": "AOC",
        "model": "U27B3A",
        "serial_number": "123456",
        "physical_width_mm": 600,
        "physical_height_mm": 340,
        "active": True,
        "current_mode": {
            "name": "1920x1080",
            "width": 1920,
            "height": 1080,
            "refresh_rate_hz": 60,
            "preferred": True,
            "interlaced": False,
        },
        "supported_modes": [
            {
                "name": "1920x1080",
                "width": 1920,
                "height": 1080,
                "refresh_rate_hz": 60,
                "preferred": True,
                "interlaced": False,
            },
        ],
    },
]
```

`active` identifies the monitor used by IoT App. `current_mode` is `None` when
the monitor is connected but does not have an active DRM mode. A preferred mode
is the mode recommended by the monitor. An interlaced mode draws alternating
sets of lines rather than a complete frame at once.

The manufacturer, model, serial number, and mode name may be empty strings when
Linux or the monitor does not provide them. Physical width and height may be
zero for the same reason.

The list is a startup snapshot. Connecting or disconnecting a monitor later
does not update it. Restart IoT App to scan the displays again.

This list can contain several monitors, but the current LVGL and full-screen
video setup supports one connected monitor. The `active` field identifies the
monitor selected for its mode and for video playback; it does not route
`/dev/fb0` to a particular HDMI connector. See the
[LVGL guide](../lvgl/README.md#21-display) for why HDMI-A-2 still works when it
is the only connected monitor.

```text
Python: display.monitors()
 │
 v
MicroPython module table maps "monitors" to display_monitors_object [mod_iot_display.c]
 │
 v
display_monitors() builds one dictionary per monitor [mod_iot_display.c]
 │
 v
iot_display_monitor_count() reads the number of monitors in the startup list [display_cpp_bridge.cpp]
 │
 v
iot_display_monitor_information() supplies each monitor's details [display_cpp_bridge.cpp]
 │
 v
iot_display_supported_mode_information() supplies each monitor's mode list [display_cpp_bridge.cpp]
```

### `display.active_monitor()`

```python
from iot import display

monitor = display.active_monitor()
current_mode = monitor["current_mode"]

print(monitor["connector_name"])
print(current_mode["width"], current_mode["height"])
```

Takes no arguments and returns the monitor dictionary marked as active in
`display.monitors()`. IoT App always has an active monitor while a Python
application is running, so `current_mode` is available in this result.

```text
Python: display.active_monitor()
 │
 v
MicroPython module table maps "active_monitor" to display_active_monitor_object [mod_iot_display.c]
 │
 v
display_active_monitor() searches the startup list for its active entry [mod_iot_display.c]
 │
 v
iot_display_monitor_information() marks the monitor selected at startup [display_cpp_bridge.cpp]
 │
 v
monitor_dictionary() builds the result, including its current and supported modes [mod_iot_display.c]
```

### Display example

```python
from iot import display

screen_width, screen_height = display.size()
display.clear(color=(8, 13, 22))

message_box = display.draw_text_box(
    x=40,
    y=40,
    width=screen_width - 80,
    height=100,
    text="IoT App is running",
    background_color=(24, 34, 51),
    border_color=(64, 220, 255),
    background_opacity=255,
    border_width=2,
    font_size=24,
)

display.update_text_box(message_box, "Display is ready")
display.move_text_box(message_box, 40, 160)
```

## `iot.network`

The network module downloads one file at a time over HTTP or HTTPS. The call is
synchronous: Python waits while the file is downloaded, checked, and moved to
the application's private download directory. The LVGL render thread keeps
refreshing the screen, but Python timers and MQTT deployment work wait for the
call to return.

The fixed limits are:

- 10 MiB for one file;
- 50 MiB for all downloaded files stored by one Python application;
- 10 seconds to connect;
- 30 seconds for the complete transfer, including the connection time;
- 5 redirects;
- HTTP and HTTPS URLs only;
- normal certificate and hostname checks for HTTPS.

Downloaded files are removed before the next Python application starts.

### `network.download_file()`

```python
from iot import network

downloaded_file = network.download_file(
    "https://example.com/photo.jpg",
    expected_sha256="0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",
)

print(downloaded_file["path"])
```

| Parameter | Required? | Meaning |
|---|---|---|
| `url` | Yes | An HTTP or HTTPS URL |
| `expected_sha256` | No | Expected SHA-256 as exactly 64 hexadecimal characters |

Use `expected_sha256` when the expected file is known. A mismatch raises
`RuntimeError`, and IoT App removes the incomplete or unexpected file.
If a connection or transfer reaches its time limit, the call raises
`RuntimeError` with a timeout message and removes the incomplete file. The
message lists both limits because connecting can time out before the full
30 seconds have passed. Local file checking and JPEG decoding happen outside
the transfer timeout.

The downloader requires libcurl with asynchronous DNS (a threaded or c-ares
resolver). Otherwise it reports an error before making a request: with signals
disabled, a blocking DNS lookup could ignore the timeout. Both project image
builds select a suitable resolver. See libcurl's
[`CURLOPT_NOSIGNAL`](https://curl.se/libcurl/c/CURLOPT_NOSIGNAL.html) and
[`CURLOPT_TIMEOUT_MS`](https://curl.se/libcurl/c/CURLOPT_TIMEOUT_MS.html) references.

You can catch download or JPEG errors and leave the current screen in place:

```python
from iot import display, network

try:
    downloaded_file = network.download_file("https://example.com/photo.jpg")
    display.set_background_image(downloaded_file["path"], mode="center")
except RuntimeError as error:
    print("Could not show the new picture:", error)
```

If the error is not caught, IoT App stops the Python application and shows the
C++ emergency screen. It stays there until another application is sent or
IoT App restarts. This does not depend on code in the default dashboard.

The function returns a dictionary:

```python
{
    "path": "<temporary path chosen by IoT App>",
    "sha256": "0123abcd...",
    "size_bytes": 147600,
    "content_type": "image/jpeg",
    "loaded_from_cache": False,
}
```

Every completed file is named `<calculated-sha256>.download`. IoT App always
calculates this value, whether or not `expected_sha256` was supplied.

When `expected_sha256` is supplied, another request for that hash can return
the existing file without making a network request. In that case,
`loaded_from_cache` is `True`.

Without an expected hash, IoT App has to download the file before it can
calculate its SHA-256. If it already has the same bytes, it keeps the existing
file instead of storing a second copy. `loaded_from_cache` is still `False`
for this call because the download happened.

For example, one URL might return a file with `Content-Type: application/octet-stream`,
while a second URL returns the same bytes with `Content-Type: image/jpeg`.
After the second download, the cached file keeps `image/jpeg` as its content
type. A later cache hit returns that value. The `content_type` value comes
from the HTTP response; it is not a check of the file's actual format.

Without an expected hash, the transfer needs room within the remaining
50 MiB allowance before IoT App knows whether its bytes are already cached.
A known-hash cache hit can succeed even when that allowance is full.

The downloader can store any file that fits its limits. The display module
still accepts only JPEG image data.

```text
Python: network.download_file(url, expected_sha256)
 │
 v
MicroPython module table maps "download_file" to network_download_file_object [mod_iot_network.c]
 │
 v
network_download_file() checks the arguments and builds the result dictionary [mod_iot_network.c]
 │
 v
iot_network_download_file() gets the current application's downloader [network_cpp_bridge.cpp]
 │
 v
HttpFileDownloader::downloadFile() checks the cache or downloads and verifies the file [http_file_downloader.cpp]
```

### Download and show a JPEG

```python
from iot import display, network

downloaded_file = network.download_file(
    "https://example.com/photo.jpg",
    expected_sha256="0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",
)

display.set_background_image(downloaded_file["path"], mode="fit")
```

## `iot.scheduler`

The scheduler keeps an application responsive without an infinite Python loop.
Callbacks run one at a time on the MicroPython owner thread.

### `scheduler.every()`

```python
from iot import scheduler

def update_display():
    print("Scheduled callback ran")

task_id = scheduler.every(
    milliseconds=1000,
    callback=update_display,
)
```

Registers a repeating callback and returns a positive integer task ID. Both
arguments are required and may be passed by position or by name.

| Parameter | Required? | Meaning |
|---|---|---|
| `milliseconds` | Yes | Time between calls, from 1 millisecond to about 49.7 days |
| `callback` | Yes | Callable that accepts no arguments |

The callback is not called immediately. For example, a 1,000 millisecond timer
first runs after about one second, then runs again about once per second.

One application can have up to 128 active timers. This is a fixed safety limit,
not a Raspberry Pi hardware limit. It prevents a faulty application from
creating timers forever and using all available memory. Most applications only
need a few timers.

The scheduler stores the interval as an unsigned 32-bit millisecond value. Its
valid range is 1 to 4,294,967,295 milliseconds, or about 49.7 days.

If IoT App is busy and misses several calls, it calls the callback only once
when it becomes available again. It does not rapidly repeat the callback to
catch up. For example, if 3.4 seconds pass before IoT App can check a
one-second timer, the callback runs once and its next call is due about 0.6
seconds later.

The time used by the callback is part of the timer interval. Suppose a timer
runs once per second and its callback takes 300 milliseconds. About 700
milliseconds then remain before the next call. IoT App waits for that remaining
time rather than starting a new one-second wait.

Callbacks should finish quickly. They may add or cancel timers, but they should
not sleep for a long time or run forever.

```text
Python: scheduler.every(milliseconds, callback)
 │
 v
MicroPython module table maps "every" to scheduler_every_object [mod_iot_scheduler.c]
 │
 v
scheduler_every() validates the interval and callback, then stores a timer in MicroPython's task list [mod_iot_scheduler.c]
 │
 v
MicroPythonRuntime::runScheduledCallbacks() checks elapsed time on the main thread [micropython_runtime.cpp]
 │
 v
iot_scheduler_run_due_callbacks() invokes the Python callback when its timer is due [mod_iot_scheduler.c]
```

### `scheduler.cancel()`

```python
from iot import scheduler

def update_display():
    print("Scheduled callback ran")

task_id = scheduler.every(
    milliseconds=1000,
    callback=update_display,
)

was_cancelled = scheduler.cancel(task_id)
```

Cancels one timer. It returns `True` when the task existed and was removed, or
`False` when the ID was invalid or no active task had that ID.

| Parameter | Required? | Meaning |
|---|---|---|
| `task_id` | Yes | ID returned by `scheduler.every()` |

```text
Python: scheduler.cancel(task_id)
 │
 v
MicroPython module table maps "cancel" to scheduler_cancel_object [mod_iot_scheduler.c]
 │
 v
scheduler_cancel() finds the ID and removes that timer from MicroPython's task list [mod_iot_scheduler.c]
```

### `scheduler.clear()`

```python
from iot import scheduler

scheduler.clear()
```

Takes no arguments, removes every scheduled task in the current application,
and returns `None`.

IoT App also removes all tasks automatically when it stops the current Python
application. Timer IDs belong only to the interpreter that created them.

```text
Python: scheduler.clear()
 │
 v
MicroPython module table maps "clear" to scheduler_clear_object [mod_iot_scheduler.c]
 │
 v
scheduler_clear() removes the current application's whole task list [mod_iot_scheduler.c]
```

### Scheduler example

```python
from iot import display, scheduler, system

clock_box = display.draw_text_box(
    x=40,
    y=40,
    width=420,
    height=70,
    text=system.current_time(),
)

def update_clock():
    display.update_text_box(clock_box, system.current_time())

clock_task = scheduler.every(
    milliseconds=1000,
    callback=update_clock,
)

# This can be called later when the clock no longer needs updates:
# scheduler.cancel(clock_task)
```

Each timer keeps its own interval. An application can update a clock every
second and refresh another panel every five seconds, for example:

```python
from iot import scheduler

def update_clock():
    print("Update the clock")

def update_network():
    print("Refresh the network panel")

clock_task = scheduler.every(milliseconds=1000, callback=update_clock)
network_task = scheduler.every(milliseconds=5000, callback=update_network)
```

The runtime waits for whichever timer is due next. An MQTT deployment can wake
that wait early when another application arrives.

If a scheduled callback raises an unhandled exception, IoT App stops that
application and shows the traceback on the native emergency screen. The
shipped default application is not started again until `iot_app` restarts.

## `iot.system`

Most system functions use a snapshot taken when the current Python application
started. Related values therefore come from the same point in time, and the
runtime does not keep scanning `/proc`, `/sys`, and `/dev`.

`current_time()`, `uptime_seconds()`, and `network_interfaces()` are live reads.
Call them when a screen needs to show changing time, uptime, or network state.

### `system.information()`

```python
from iot import system

information = system.information()
```

Takes no arguments and returns:

```python
{
    "hostname": "raspberrypi",
    "device_model": "Raspberry Pi 4 Model B Rev 1.4",
    "operating_system": "Raspberry Pi OS",
    "kernel_version": "Linux 6.12.47-v8+",
    "architecture": "aarch64",
    "uptime_seconds": 6138,
}
```

The `uptime_seconds` value in this dictionary is the startup snapshot. Use
`system.uptime_seconds()` for a value that changes while the app is running.
The `kernel_version` value already starts with `Linux`, so applications do not
need to add that word themselves.

```text
Python: system.information()
 │
 v
MicroPython module table maps "information" to system_information_object [mod_iot_system.c]
 │
 v
system_information() builds a dictionary from the stored system snapshot [mod_iot_system.c]
 │
 v
iot_system_read_information() copies the current application's startup snapshot [system_cpp_bridge.cpp]
```

### `system.current_time()`

```python
from iot import system

local_time = system.current_time()
```

Takes no arguments and returns the current Linux local time as a string in
`YYYY-MM-DD HH:MM:SS` format, for example `2000-01-01 00:00:00`.

```text
Python: system.current_time()
 │
 v
MicroPython module table maps "current_time" to system_current_time_object [mod_iot_system.c]
 │
 v
system_current_time() converts the returned time to a Python string [mod_iot_system.c]
 │
 v
iot_system_current_time() asks the active context for local time [system_cpp_bridge.cpp]
 │
 v
LinuxSystemInformationProvider::readCurrentLocalTime() reads and formats the time [system_information.cpp]
```

### `system.uptime_seconds()`

```python
from iot import system

uptime = system.uptime_seconds()
```

Takes no arguments and performs a live read of Linux uptime. It returns the
number of seconds since the system booted.

```text
Python: system.uptime_seconds()
 │
 v
MicroPython module table maps "uptime_seconds" to system_uptime_seconds_object [mod_iot_system.c]
 │
 v
system_uptime_seconds() converts the returned uptime to a Python integer [mod_iot_system.c]
 │
 v
iot_system_uptime_seconds() asks the active context for uptime [system_cpp_bridge.cpp]
 │
 v
LinuxSystemInformationProvider::readUptimeSeconds() reads the live Linux value [system_information.cpp]
```

### `system.resources()`

```python
from iot import system

resources = system.resources()
```

Takes no arguments and returns the resource snapshot:

```python
{
    "logical_cpu_count": 4,
    "cpu_temperature_celsius": 43.2,
    "one_minute_load_average": 0.18,
    "total_memory_bytes": 2000000000,
    "available_memory_bytes": 1500000000,
    "root_storage_total_bytes": 16000000000,
    "root_storage_available_bytes": 12000000000,
}
```

`cpu_temperature_celsius` and `one_minute_load_average` are `None` when Linux
does not provide those values. Byte counts are integers.

```text
Python: system.resources()
 │
 v
MicroPython module table maps "resources" to system_resources_object [mod_iot_system.c]
 │
 v
system_resources() selects resource fields from the startup snapshot [mod_iot_system.c]
 │
 v
iot_system_read_information() copies the current application's startup snapshot [system_cpp_bridge.cpp]
```

### `system.network_interfaces()`

```python
from iot import system

network_interfaces = system.network_interfaces()
```

Takes no arguments, reads Linux at the time of the call, and returns a tuple
containing interfaces that can connect to another device, such as `eth0` and
`wlan0`. The local-only `lo` interface is not included:

```python
(
    {
        "name": "eth0",
        "connected": True,
        "ipv4_address": "192.0.2.10",
        "speed_megabits_per_second": 1000,
    },
)
```

`ipv4_address` is an empty string when no IPv4 address was found. Link speed is
`None` when Linux does not report it. Call this function again to detect a
later connection, disconnection, or address change.

```text
Python: system.network_interfaces()
 │
 v
MicroPython module table maps "network_interfaces" to system_network_interfaces_object [mod_iot_system.c]
 │
 v
system_network_interfaces() builds one dictionary per current interface [mod_iot_system.c]
 │
 v
iot_system_network_interface_count() requests a fresh reading from the active context [system_cpp_bridge.cpp]
 │
 v
LinuxSystemInformationProvider::readNetworkInterfaces() reads current Linux interfaces [system_information.cpp]
 │
 v
iot_system_read_network_interface() supplies each result to the C binding [system_cpp_bridge.cpp]
```

### `system.interfaces()`

```python
from iot import system

system_interface_counts = system.interfaces()
```

Takes no arguments and returns counts of Linux hardware interfaces:

```python
{
    "i2c": 1,
    "gpio_controllers": 2,
    "spi": 0,
    "serial": 2,
}
```

The I2C value counts interfaces such as `/dev/i2c-1`; it does not scan I2C
addresses and does not report how many I2C devices are attached.

These values count Linux interfaces, not connected peripheral devices:

- `i2c` counts device files named `/dev/i2c-*`. For example, `/dev/i2c-1`
  counts as one I2C interface. An I2C address is only needed when communicating
  with a device on that interface.
- `gpio_controllers` counts `/dev/gpiochip*` device files. For example,
  `/dev/gpiochip0` and `/dev/gpiochip1` count as two GPIO controllers. This
  does not count individual GPIO inputs or outputs.
- `spi` counts Linux `spidev` interfaces.
- `serial` counts supported Linux serial device files, including `ttyAMA`,
  `ttyUSB`, `ttyACM`, and `ttyS` devices.

Therefore, `"i2c": 1` means Linux exposes one I2C bus device file. It does not
mean that one I2C peripheral was found or that the application has permission
to open the bus.

```text
Python: system.interfaces()
 │
 v
MicroPython module table maps "interfaces" to system_interfaces_object [mod_iot_system.c]
 │
 v
system_interfaces() selects interface counts from the startup snapshot [mod_iot_system.c]
 │
 v
iot_system_read_information() copies the current application's startup snapshot [system_cpp_bridge.cpp]
```

### `system.devices()`

```python
from iot import system

connected_device_counts = system.devices()
```

Takes no arguments and returns device counts from the startup snapshot:

```python
{
    "usb": 4,
    "input": 3,
    "block": 2,
}
```

```text
Python: system.devices()
 │
 v
MicroPython module table maps "devices" to system_devices_object [mod_iot_system.c]
 │
 v
system_devices() selects device counts from the startup snapshot [mod_iot_system.c]
 │
 v
iot_system_read_information() copies the current application's startup snapshot [system_cpp_bridge.cpp]
```

### `system.app_information()`

```python
from iot import system

application_information = system.app_information()
```

Takes no arguments and returns information about the running IoT App process
and current Python application:

```python
{
    "application_name": "Default app",
    "app_version": "0.1.0",
    "micropython_version": "1.28.0",
    "lvgl_version": "9.5.0",
}
```

`application_name` comes from the current package's `app.json`. `app_version`
is the version of the C++ IoT App executable; it is not read from `app.json`.

```text
Python: system.app_information()
 │
 v
MicroPython module table maps "app_information" to system_app_information_object [mod_iot_system.c]
 │
 v
system_app_information() selects the application and version fields [mod_iot_system.c]
 │
 v
iot_system_read_information() adds the current app name and compiled versions [system_cpp_bridge.cpp]
```

### System example

An application can read all startup information groups like this:

```python
from iot import display, system

system_information = system.information()
resource_information = system.resources()
network_interfaces = system.network_interfaces()
system_interface_counts = system.interfaces()
connected_device_counts = system.devices()
application_information = system.app_information()
connected_monitors = display.monitors()
active_monitor = display.active_monitor()
```

The following example uses part of that information to draw a summary:

```python
from iot import display, system

machine = system.information()
resources = system.resources()

summary = (
    "Host: %s\n"
    "Model: %s\n"
    "Kernel: %s %s\n"
    "CPU cores: %d\n"
    "Uptime: %d seconds"
) % (
    machine["hostname"],
    machine["device_model"],
    machine["kernel_version"],
    machine["architecture"],
    resources["logical_cpu_count"],
    system.uptime_seconds(),
)

display.clear()
display.draw_text_box(
    x=40,
    y=40,
    width=900,
    height=300,
    text=summary,
)
```

## `iot.input`

Choose the class for the board connected to the device:
`input.AdafruitMiniI2cGamepad()` or `input.SeenGreatOledHatController()`.
Call `connect()` after constructing it. Neither class searches for the other
board. A missing board or an access problem raises `RuntimeError`.

None of these constructors takes a bus number or address. The Raspberry Pi 4
connections are set in `iot_app/src/input/controller_hardware_settings.h`:
Adafruit uses I2C bus 1 and address `0x50`; the SeenGreat OLED uses bus 1 and
address `0x3c`. SeenGreat's buttons use GPIO pins, also listed there. To use
different wiring, change those values and rebuild IoT App. Moving the same
device to another host does not normally change the device's own I2C address,
but the host's bus number or GPIO lines can differ.

The common methods are `model_name()`, `board_type()`, `is_connected()`, `joystick()`,
`buttons()`, `refresh_input_state()`, and `close()`. Only the Adafruit gamepad
has `calibrate_joystick()` because its joystick uses analogue values.
Directions are available through `joystick().direction()`. Both boards support
the same direction names, but their physical buttons have different names.
Adafruit reports `"X"`, `"Y"`, `"A"`, `"B"`, `"Select"`, and `"Start"`. SeenGreat
reports `"K1"`, `"K2"`, `"K3"`, and `"Press"` (the joystick's centre press).
SeenGreat's directional keys are exposed as a virtual joystick.
Its position has discrete values rather than the Adafruit joystick's analogue
values. An app written for one board can still use `direction()` without
checking raw joystick values.

Only `input.AdafruitMiniI2cGamepad` provides the firmware-diagnostic methods.

`ControllerJoystick` and `ControllerButtons` are view types returned by a controller.
Applications should not try to construct these view types directly.

### `input.AdafruitMiniI2cGamepad()`

```python
from iot import input

gamepad = input.AdafruitMiniI2cGamepad()
gamepad.connect()
```

The constructor opens the configured Linux I2C device, `/dev/i2c-1`, and
selects address `0x50`. A missing bus or permission problem raises
`RuntimeError` here.

The constructor only opens the Linux I²C device. The application must then call
`connect()` to reset the gamepad, verify its product ID, configure its inputs,
and read its first state.

```python
from iot import input

gamepad = input.AdafruitMiniI2cGamepad()

# Connection now communicates with the gamepad and prepares it for use.
gamepad.connect()

print(gamepad.is_connected())  # True
```

```text
Python: input.AdafruitMiniI2cGamepad()
 │
 v
MicroPython module table maps "AdafruitMiniI2cGamepad" to iot_adafruit_controller_type [mod_iot_input.c]
 │
 v
adafruit_controller_make_new() allocates the Python wrapper [mod_iot_input.c]
 │
 v
iot_adafruit_controller_create() constructs the C++ board object [input_cpp_bridge.cpp]
 │
 v
AdafruitMiniI2cGamepad::AdafruitMiniI2cGamepad() opens the configured I2C device [adafruit_mini_i2c_gamepad.cpp]
```

### `input.SeenGreatOledHatController()`

```python
from iot import input

controller = input.SeenGreatOledHatController()
controller.connect()
print(controller.joystick().direction())
print(controller.buttons().pressed())  # For example: ("K1", "Press")
```

The constructor opens the configured SeenGreat OLED and its reset and D/C
GPIO lines. `connect()` opens the joystick and button GPIO lines. The OLED
uses I2C, but the controls do not. Its joystick centre is fixed at `(512, 512)`
with a dead zone of `100`; there is no calibration step. This class has the
common methods listed above, not the Adafruit calibration or firmware-diagnostic
methods. A missing HAT or a GPIO permission problem raises `RuntimeError`.

```text
Python: input.SeenGreatOledHatController()
 │
 v
MicroPython module table maps "SeenGreatOledHatController" to iot_seengreat_controller_type [mod_iot_input.c]
 │
 v
seengreat_controller_make_new() allocates the Python wrapper [mod_iot_input.c]
 │
 v
iot_seengreat_controller_create() constructs the C++ board object [input_cpp_bridge.cpp]
 │
 v
SeenGreatOledHatController::SeenGreatOledHatController() obtains the shared OLED HAT [seengreat_oled_hat.cpp]
```

### Controller methods

Both board classes provide the methods below unless a method is marked
Adafruit-only.

#### `connect()`

Prepares the selected board and reads its first input state. On Adafruit, this
resets the I2C gamepad, checks its product ID, and configures its buttons. On
SeenGreat, it opens the GPIO lines for the joystick and buttons. It takes no
arguments and returns `None`. Missing hardware or an access problem raises
`RuntimeError`.

Here is how the Python call reaches the C++ driver:

```text
Python: controller.connect()
 │
 v
MicroPython method table maps "connect" to controller_connect_object [mod_iot_input.c]
 │
 v
controller_connect() gets the native handle from the Python object [mod_iot_input.c]
 │
 v
iot_controller_connect() resolves the handle and enters C++ safely [input_cpp_bridge.cpp]
 │
 v
InputControllerBase::connect() is virtual, so C++ selects the matching board:
 ├── AdafruitMiniI2cGamepad::connect() [adafruit_mini_i2c_gamepad.cpp]
 └── SeenGreatOledHatController::connect() [seengreat_oled_hat.cpp]
```

#### `calibrate_joystick()` (Adafruit only)

```python
from iot import input

gamepad = input.AdafruitMiniI2cGamepad()
gamepad.connect()

gamepad.calibrate_joystick(number_of_samples=20, dead_zone=100)
```

This measures the Adafruit joystick's resting centre. Keep the stick untouched
while it runs. Both arguments are optional and keyword-only.

| Parameter | Required? | Meaning |
|---|---|---|
| `number_of_samples` | No, default `20` | Number of joystick readings used to calculate the resting centre; must be greater than zero |
| `dead_zone` | No, default `100` | Distance the joystick must move away from its measured centre before `direction()` reports a direction; from 0 to 1023 |

Call `connect()` first.

`number_of_samples` controls how the centre is measured. The
driver reads both joystick axes several times and averages the readings.
More samples can reduce small changes caused by electrical noise, but calibration takes
longer. Keep the joystick still and near its natural centre until calibration
finishes. The default of 20 is normally enough.

`dead_zone` prevents tiny movements around the centre from being treated as a
direction. For example, if the measured X centre is 510 and the dead zone is
100, X values from 410 through 610 are treated as centred. A value above 610
can report right, and a value below 410 can report left. The same rule is used
for the Y axis. The dead zone only affects `direction()`; it does not change
the values returned by `position()`.

```text
Python: gamepad.calibrate_joystick()
 │
 v
MicroPython method table maps "calibrate_joystick" to adafruit_controller_calibrate_joystick_object [mod_iot_input.c]
 │
 v
adafruit_controller_calibrate_joystick() checks the sample count and dead zone [mod_iot_input.c]
 │
 v
iot_adafruit_controller_calibrate_joystick() checks that the handle belongs to Adafruit [input_cpp_bridge.cpp]
 │
 v
AdafruitMiniI2cGamepad::calibrateJoystick() averages readings and stores the centre and dead zone [adafruit_mini_i2c_gamepad.cpp]
```

#### `refresh_input_state()`

Reads the current joystick and buttons from the physical device and remembers
the result. It takes no arguments and returns `None`. `connect()` already takes
the first reading, so you do not need to refresh immediately after connecting.

Call this once at the start of each input update. After that, read everything
you need from `joystick` and `controller_buttons`. All those values then belong to
the same refresh cycle:

```python
from iot import input

gamepad = input.AdafruitMiniI2cGamepad()
gamepad.connect()
gamepad.calibrate_joystick(number_of_samples=20, dead_zone=100)

joystick = gamepad.joystick()
controller_buttons = gamepad.buttons()

def read_input_update():
    gamepad.refresh_input_state()
    current_direction = joystick.direction()
    pressed_buttons = controller_buttons.pressed()
    return current_direction, pressed_buttons
```

`gamepad.joystick()` and `gamepad.buttons()` create view objects for reading the
state stored by `gamepad`. Create these views once and reuse them during later
input updates.

Methods such as `joystick.direction()` and `controller_buttons.pressed()` only
return the state remembered by `connect()` or the most recent
`refresh_input_state()` call.
They do not contact the hardware again. Call `refresh_input_state()` later
when you want new values from the hardware.

`current_direction` is only needed when the application uses the joystick. For
example, this application moves a text box with the joystick:

```python
from iot import display, input, scheduler

gamepad = input.AdafruitMiniI2cGamepad()
gamepad.connect()
gamepad.calibrate_joystick(number_of_samples=20, dead_zone=100)

joystick = gamepad.joystick()

box_x = 200
box_y = 200
text_box = display.draw_text_box(
    x=box_x,
    y=box_y,
    width=300,
    height=80,
    text="Move me with the joystick",
)

def move_text_box_with_joystick():
    global box_x
    global box_y

    gamepad.refresh_input_state()
    current_direction = joystick.direction()

    if current_direction in ("left", "up_left", "down_left"):
        box_x -= 10
    elif current_direction in ("right", "up_right", "down_right"):
        box_x += 10

    if current_direction in ("up", "up_left", "up_right"):
        box_y -= 10
    elif current_direction in ("down", "down_left", "down_right"):
        box_y += 10

    if current_direction != "center":
        display.move_text_box(text_box, box_x, box_y)

scheduler.every(milliseconds=50, callback=move_text_box_with_joystick)
```

`joystick.direction()` applies the calibrated centre and dead zone. The
application does not need to compare the raw X and Y values itself.

```text
Python: controller.refresh_input_state()
 │
 v
MicroPython method table maps "refresh_input_state" to controller_refresh_input_state_object [mod_iot_input.c]
 │
 v
controller_refresh_input_state() gets the native handle [mod_iot_input.c]
 │
 v
iot_controller_refresh_input_state() resolves the handle and dispatches the virtual method [input_cpp_bridge.cpp]
 │
 v
The matching board reads and saves its latest joystick and button state:
 ├── AdafruitMiniI2cGamepad::refreshInputState() [adafruit_mini_i2c_gamepad.cpp]
 └── SeenGreatOledHatController::refreshInputState() [seengreat_oled_hat.cpp]
```

#### `is_connected()`

Takes no arguments and returns `True` after `connect()` succeeds. It reports
the driver's state and does not send a new probe transaction.

```text
Python: controller.is_connected()
 │
 v
MicroPython method table maps "is_connected" to controller_is_connected_object [mod_iot_input.c]
 │
 v
controller_is_connected() converts the driver's result to a Python boolean [mod_iot_input.c]
 │
 v
iot_controller_is_connected() reads the driver's connection state [input_cpp_bridge.cpp]
 ├── AdafruitMiniI2cGamepad::isConnected() [adafruit_mini_i2c_gamepad.cpp]
 └── SeenGreatOledHatController::isConnected() [seengreat_oled_hat.cpp]
```

#### `model_name()`

Takes no arguments and returns `"Adafruit Mini I2C STEMMA QT Gamepad"` or
`"SeenGreat 1.3 inch OLED HAT"`, depending on the selected board.

```text
Python: controller.model_name()
 │
 v
MicroPython method table maps "model_name" to controller_model_name_object [mod_iot_input.c]
 │
 v
controller_model_name() converts the result to a Python string [mod_iot_input.c]
 │
 v
iot_controller_model_name() reads the driver's display name [input_cpp_bridge.cpp]
 ├── AdafruitMiniI2cGamepad::modelName() [adafruit_mini_i2c_gamepad.cpp]
 └── SeenGreatOledHatController::modelName() [seengreat_oled_hat.cpp]
```

#### `board_type()`

Returns `"adafruit_mini_i2c_gamepad"` for Adafruit or `"seengreat_oled_hat"`
for SeenGreat. The readable `model_name()` is for display; use `board_type()`
when code needs a stable value.

```text
Python: controller.board_type()
 │
 v
MicroPython method table maps "board_type" to controller_board_type_object [mod_iot_input.c]
 │
 v
controller_board_type() converts the result to a Python string [mod_iot_input.c]
 │
 v
iot_controller_board_type() reads the driver's stable board name [input_cpp_bridge.cpp]
 ├── AdafruitMiniI2cGamepad::boardType() [adafruit_mini_i2c_gamepad.cpp]
 └── SeenGreatOledHatController::boardType() [seengreat_oled_hat.cpp]
```

#### `connection_information()` (Adafruit only)

Returns the Adafruit board's configured Linux I2C connection:

```python
from iot import input

gamepad = input.AdafruitMiniI2cGamepad()

connection = gamepad.connection_information()

print(connection["bus_number"])   # 1
print(connection["address"])      # 80, which is 0x50
print(connection["device_path"])  # /dev/i2c-1
```

| Dictionary field | Meaning |
|---|---|
| `bus_number` | Configured Linux I2C bus number |
| `address` | Configured seven-bit I2C address, returned as an integer |
| `device_path` | Linux device opened for that bus, such as `/dev/i2c-1` |

This method does not contact the gamepad, so `connect()` is not required. It is
mainly useful in logs and hardware diagnostic screens.

```text
Python: gamepad.connection_information()
 │
 v
MicroPython method table maps "connection_information" to adafruit_controller_connection_information_object [mod_iot_input.c]
 │
 v
adafruit_controller_connection_information() builds a Python dictionary [mod_iot_input.c]
 │
 v
iot_adafruit_controller_read_connection_information() reads the stored I2C settings [input_cpp_bridge.cpp]
```

#### `joystick()`

Takes no arguments and returns a `ControllerJoystick` view connected to this
controller. Keep the controller open while using the view.

```text
Python: controller.joystick()
 │
 v
MicroPython method table maps "joystick" to controller_joystick_object [mod_iot_input.c]
 │
 v
controller_joystick() creates a joystick view [mod_iot_input.c]
 │
 v
create_state_view() keeps the controller alive; no hardware read happens here [mod_iot_input.c]
```

#### `buttons()`

Takes no arguments and returns a `ControllerButtons` view connected to this
controller. Keep the controller open while using the view.

```text
Python: controller.buttons()
 │
 v
MicroPython method table maps "buttons" to controller_buttons_object [mod_iot_input.c]
 │
 v
controller_buttons() creates a button view [mod_iot_input.c]
 │
 v
create_state_view() keeps the controller alive; no hardware read happens here [mod_iot_input.c]
```

#### Adafruit-only diagnostic methods

These methods take no arguments and return integer values read during
`connect()`:

| Method | Meaning |
|---|---|
| `processor_hardware_id()` | Hardware ID reported by the gamepad processor |
| `firmware_product_id()` | Product ID from the upper 16 bits of the combined value; expected value is 5743 |
| `firmware_date_code()` | Encoded firmware date from the lower 16 bits |
| `combined_product_id_and_firmware_date_code()` | Original 32-bit product/date value reported by the device |

Call `connect()` before reading diagnostics. Until a successful connection,
these methods return their initial zero values because no identity registers
have been read yet.

```python
from iot import input

gamepad = input.AdafruitMiniI2cGamepad()
gamepad.connect()

print("Processor hardware ID:", gamepad.processor_hardware_id())
print("Product ID:", gamepad.firmware_product_id())
print("Firmware date code:", gamepad.firmware_date_code())
print(
    "Combined product and date value:",
    gamepad.combined_product_id_and_firmware_date_code(),
)
```

`connect()` already checks that the product ID is 5743. These methods are
mainly useful for logs and hardware diagnostics.

All four methods read values saved by `connect()`; they do not contact the
gamepad again:

```text
Python: gamepad.processor_hardware_id()
 │
 v
MicroPython method table maps "processor_hardware_id" to adafruit_controller_processor_hardware_id_object [mod_iot_input.c]
 │
 v
adafruit_controller_processor_hardware_id() selects the hardware ID [mod_iot_input.c]
 │
 v
read_diagnostics() gathers the board's stored diagnostic values [mod_iot_input.c]
 │
 v
iot_adafruit_controller_read_diagnostics() copies the saved values into a C structure [input_cpp_bridge.cpp]
 │
 v
AdafruitMiniI2cGamepad::processorHardwareId() returns the saved ID [adafruit_mini_i2c_gamepad.cpp]
```

```text
Python: gamepad.firmware_product_id()
 │
 v
MicroPython method table maps "firmware_product_id" to adafruit_controller_firmware_product_id_object [mod_iot_input.c]
 │
 v
adafruit_controller_firmware_product_id() selects the product ID [mod_iot_input.c]
 │
 v
read_diagnostics() gathers the board's stored diagnostic values [mod_iot_input.c]
 │
 v
iot_adafruit_controller_read_diagnostics() copies the saved values into a C structure [input_cpp_bridge.cpp]
 │
 v
AdafruitMiniI2cGamepad::firmwareProductId() returns the saved product ID [adafruit_mini_i2c_gamepad.cpp]
```

```text
Python: gamepad.firmware_date_code()
 │
 v
MicroPython method table maps "firmware_date_code" to adafruit_controller_firmware_date_code_object [mod_iot_input.c]
 │
 v
adafruit_controller_firmware_date_code() selects the date code [mod_iot_input.c]
 │
 v
read_diagnostics() gathers the board's stored diagnostic values [mod_iot_input.c]
 │
 v
iot_adafruit_controller_read_diagnostics() copies the saved values into a C structure [input_cpp_bridge.cpp]
 │
 v
AdafruitMiniI2cGamepad::firmwareDateCode() returns the saved date code [adafruit_mini_i2c_gamepad.cpp]
```

```text
Python: gamepad.combined_product_id_and_firmware_date_code()
 │
 v
MicroPython method table maps "combined_product_id_and_firmware_date_code" to adafruit_controller_combined_product_id_and_firmware_date_code_object [mod_iot_input.c]
 │
 v
adafruit_controller_combined_product_id_and_firmware_date_code() selects the combined value [mod_iot_input.c]
 │
 v
read_diagnostics() gathers the board's stored diagnostic values [mod_iot_input.c]
 │
 v
iot_adafruit_controller_read_diagnostics() copies the saved values into a C structure [input_cpp_bridge.cpp]
 │
 v
AdafruitMiniI2cGamepad::productIdAndFirmwareDateCode() returns the saved value [adafruit_mini_i2c_gamepad.cpp]
```

#### `close()`

Releases the native controller and its I2C or GPIO connection. It takes no
arguments, returns `None`, and is safe to call more than once. Any later method
call on the controller or one of its views raises `ValueError`. MicroPython also
releases the object during garbage collection, but explicit `close()` is useful
when the device is no longer needed.

```text
Python: controller.close()
 │
 v
MicroPython method table maps "close" to controller_close_object [mod_iot_input.c]
 │
 v
controller_close() clears the native handle after releasing the object [mod_iot_input.c]
 │
 v
iot_controller_destroy() deletes the board object through InputControllerBase [input_cpp_bridge.cpp]
 │
 v
The matching board releases the resources it owns:
 ├── AdafruitMiniI2cGamepad::~AdafruitMiniI2cGamepad() releases the owned I2C device [adafruit_mini_i2c_gamepad.h]
 └── SeenGreatOledHatController::~SeenGreatOledHatController() closes its input GPIO lines [seengreat_oled_hat.cpp]
```

### `ControllerJoystick` methods

| Method | Return value |
|---|---|
| `position()` | Returns the latest `(x, y)` values remembered by `connect()` or `refresh_input_state()`. Each value is normally from 0 to 1023. X increases towards the right and Y increases upwards. |
| `centre()` | Returns the resting centre. Adafruit measures it during `calibrate_joystick()`; SeenGreat uses the fixed centre `(512, 512)`. It does not change during normal input refreshes. |
| `dead_zone()` | Returns the distance around the centre ignored by `direction()`. Adafruit sets it during calibration; SeenGreat uses a fixed value of `100`. |
| `direction()` | Compares the latest position with the centre and dead zone, then returns a simple direction name such as `"left"`, `"up_right"`, or `"center"`. |

`direction()` returns one of:

```text
center, left, right, up, down,
up_left, up_right, down_left, down_right
```

Larger X values mean right and larger Y values mean up.

These methods read the state saved by `connect()` or `refresh_input_state()`:

```text
Python: joystick.position()
 │
 v
MicroPython method table maps "position" to joystick_position_object [mod_iot_input.c]
 │
 v
joystick_position() returns the saved X and Y values as a Python tuple [mod_iot_input.c]
 │
 v
iot_controller_read_state() copies the saved controller state into a C structure [input_cpp_bridge.cpp]
 │
 v
ControllerJoystick::position() returns the saved X and Y values [input_controller_base.cpp]
```

```text
Python: joystick.centre()
 │
 v
MicroPython method table maps "centre" to joystick_centre_object [mod_iot_input.c]
 │
 v
joystick_centre() returns the resting centre as a Python tuple [mod_iot_input.c]
 │
 v
iot_controller_read_state() copies the saved controller state into a C structure [input_cpp_bridge.cpp]
 │
 v
ControllerJoystick::centre() returns the saved resting centre [input_controller_base.cpp]
```

```text
Python: joystick.dead_zone()
 │
 v
MicroPython method table maps "dead_zone" to joystick_dead_zone_object [mod_iot_input.c]
 │
 v
joystick_dead_zone() returns the stored dead zone as a Python integer [mod_iot_input.c]
 │
 v
iot_controller_read_state() copies the saved controller state into a C structure [input_cpp_bridge.cpp]
 │
 v
ControllerJoystick::deadZone() returns the saved dead zone [input_controller_base.cpp]
```

```text
Python: joystick.direction()
 │
 v
MicroPython method table maps "direction" to joystick_direction_object [mod_iot_input.c]
 │
 v
joystick_direction() returns the direction as a Python string [mod_iot_input.c]
 │
 v
iot_controller_joystick_direction() converts the C++ direction to text [input_cpp_bridge.cpp]
 │
 v
ControllerJoystick::direction() compares the saved position with the centre and dead zone [input_controller_base.cpp]
```

### `ControllerButtons` methods

#### `pressed()`

Takes no arguments and returns a tuple containing every button that is held
down. An empty tuple means no buttons are pressed. An Adafruit gamepad returns
names in this order:

```text
X, Y, A, B, Select, Start
```

The SeenGreat controller returns `K1, K2, K3, Press` in that order.

Use `pressed()` when you need the complete button state, for example:

```python
from iot import input

gamepad = input.AdafruitMiniI2cGamepad()
gamepad.connect()

controller_buttons = gamepad.buttons()

print(controller_buttons.pressed())  # For example: ("A", "Start")
```

```text
Python: controller_buttons.pressed()
 │
 v
MicroPython method table maps "pressed" to buttons_pressed_object [mod_iot_input.c]
 │
 v
buttons_pressed() converts the saved pressed-button mask to names [mod_iot_input.c]
 │
 v
iot_controller_read_state() copies the saved controller state into a C structure [input_cpp_bridge.cpp]
 │
 v
ControllerButtons::pressed() returns the saved pressed buttons [input_controller_base.cpp]
```

#### `is_pressed()`

```python
from iot import input

gamepad = input.AdafruitMiniI2cGamepad()
gamepad.connect()

controller_buttons = gamepad.buttons()

is_down = controller_buttons.is_pressed("A")
if is_down:
    print("A is held down")
```

Returns `True` when one named button is held down.

| Parameter | Required? | Meaning |
|---|---|---|
| `button_name` | Yes | Adafruit: `"X"`, `"Y"`, `"A"`, `"B"`, `"Select"`, `"Start"`; SeenGreat: `"K1"`, `"K2"`, `"K3"`, `"Press"` |

Button names are case-sensitive. An unknown name raises `ValueError`.

```text
Python: controller_buttons.is_pressed("A")
 │
 v
MicroPython method table maps "is_pressed" to buttons_is_pressed_object [mod_iot_input.c]
 │
 v
buttons_is_pressed() finds the button name and tests its saved bit [mod_iot_input.c]
 │
 v
iot_controller_read_state() returns the saved pressed-button mask [input_cpp_bridge.cpp]
 │
 v
buttons_is_pressed() tests that button's bit in the mask [mod_iot_input.c]
```

Use `is_pressed()` when you only care about one button, as shown by the
`is_down` condition above.

Both methods use the state saved by `connect()` or the latest
`refresh_input_state()` call.
They report whether a button is currently held down; they do not report a
separate one-time "button was just pressed" event.

`pressed()` returns the actual names for the connected board. An Adafruit
button name does not stand for a SeenGreat button. Since the app constructs
the board-specific class, it knows which button names to expect.

### Adafruit input example

```python
from iot import display, input, scheduler

gamepad = input.AdafruitMiniI2cGamepad()
gamepad.connect()

# Leave the joystick still while its centre is measured.
gamepad.calibrate_joystick(number_of_samples=20, dead_zone=100)

joystick = gamepad.joystick()
controller_buttons = gamepad.buttons()

status_box = display.draw_text_box(
    x=40,
    y=40,
    width=800,
    height=180,
    text="Waiting for input: %s" % gamepad.model_name(),
)

def refresh_gamepad():
    gamepad.refresh_input_state()
    pressed_buttons = controller_buttons.pressed()
    pressed_buttons_text = ", ".join(pressed_buttons) if pressed_buttons else "none"
    display.update_text_box(
        status_box,
        "Controller: %s\nDirection: %s\nButtons: %s" % (
            gamepad.model_name(),
            joystick.direction(),
            pressed_buttons_text,
        ),
    )

scheduler.every(milliseconds=50, callback=refresh_gamepad)
```

The [SeenGreat controls sample](../../../iot_app_sender/sample_applications/seengreat_controls/README.md)
creates that board's controller directly. The
[Adafruit joystick visualizer](../../../iot_app_sender/sample_applications/joystick_visualizer/README.md)
uses the Adafruit-specific class.

## Errors

Invalid Python arguments normally raise `TypeError` or `ValueError`. Errors
reported by the C++ display, Linux, or hardware layers raise `RuntimeError`.

An unhandled startup or scheduled-callback exception stops the current Python
application. IoT App displays the traceback on its native emergency screen and
writes it to the terminal or service log. No Python application remains
running. The emergency screen stays visible until another valid external
application arrives or `iot_app` restarts.
