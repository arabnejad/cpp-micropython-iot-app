# LVGL in IoT App

This guide explains what LVGL is and how IoT App uses it. It is written for a
reader who has not used LVGL before.

The project currently uses the pinned LVGL 9.5.0 submodule at the repository
root. IoT App does not modify that submodule. Project-specific settings live in
[`iot_app/config/lv_conf.h`](../../config/lv_conf.h), and project-specific
rendering code lives under [`iot_app/src/ui`](../../src/ui/).

## 1. What LVGL is

[LVGL](https://lvgl.io/) is a graphics library for embedded user interfaces.
It provides screens, labels, buttons, images, charts, styles, layouts, and the
drawing code needed to turn those objects into pixels.

LVGL is not a desktop environment and it does not decide which HDMI resolution
Linux should use. It needs a display driver that can send its pixels to the
real output device.

IoT App uses LVGL for three jobs:

1. Keep an in-memory tree of the objects shown on the screen.
2. Draw those objects into pixel buffers.
3. Copy the changed pixels to the Linux framebuffer at `/dev/fb0`.

Linux then sends that framebuffer to the active HDMI display.

```text
IoT App drawing request
          |
          v
LVGL objects and styles
          |
          v
LVGL Linux framebuffer driver
          |
          v
       /dev/fb0
          |
          v
   Linux display system
          |
          v
      HDMI monitor
```

The [LVGL basics guide](https://lvgl.io/docs/open/9.5/getting_started/learn_the_basics.html)
introduces the same display, screen, widget, and style concepts used below.

## 2. The LVGL ideas used by this project

### 2.1 Display

An LVGL display describes one output that LVGL can draw to. Its type is
`lv_display_t`.

IoT App creates one display and connects it to `/dev/fb0`. The framebuffer
already has a width, height, and pixel format selected by Linux. IoT App reads
those values instead of choosing new ones.

### 2.2 Screen

A screen is the root container for normal widgets. This project gets the
current screen with:

```cpp
lv_obj_t *activeScreen = lv_screen_active();
```

Text boxes and filled areas are normally created as children of this screen.

### 2.3 Widget

A widget is one visible UI object. LVGL represents widgets with `lv_obj_t *`
pointers. A label, button, image, and plain rectangular container are all
widgets.

Widgets form a parent-child tree. For example, an IoT App text box contains
two LVGL widgets:

```text
Active screen
   |
   +--> Outer box: position, size, background, border and padding
            |
            +--> Label: text, text colour, font and wrapping
```

The outer box is created first. The label is created with the box as its
parent. Moving the box also moves the label. Deleting the box also deletes the
label. This is normal LVGL parent-child behaviour.

### 2.4 Style

Styles control how widgets look. They include properties such as background
colour, opacity, border width, corner radius, padding, text colour, and font.

IoT App sets local style properties directly on each widget. For example:

```cpp
lv_obj_set_style_bg_color(textBoxObject, backgroundColor, LV_PART_MAIN);
lv_obj_set_style_border_width(textBoxObject, borderWidth, LV_PART_MAIN);
```

`LV_PART_MAIN` means the main visual part of the widget. More complicated LVGL
widgets can have other parts, such as a slider's track and knob. The official
[styles guide](https://lvgl.io/docs/open/9.5/common-widget-features/styles/overview.html)
explains parts, states, inheritance, and reusable styles.

### 2.5 Timer handler

Changing a widget tells LVGL that part of the display needs to be redrawn. The
project must then call `lv_timer_handler()` regularly so LVGL can process that
work and refresh the display.

`lv_timer_handler()` returns the number of milliseconds until LVGL expects to
run again. IoT App uses that value when its render thread waits, but limits the
wait to between 1 and 50 milliseconds so it can also respond promptly to new
drawing commands and shutdown.

The official [timer guide](https://lvgl.io/docs/open/9.5/main-modules/timer.html)
explains why an application must call this function repeatedly.

## 3. How IoT App connects LVGL to Linux

The normal runtime creates the renderer in
[`main.cpp`](../../src/runtime/main.cpp):

```cpp
iot::ui::ScreenManager screenManager{
    activeDisplay,
    iot::ui::makeLvglFramebufferRenderBackend(),
    iot::runtime::maximumPendingRenderCommands,
};

screenManager.start();
```

The factory creates `LvglFramebufferRenderBackend`. During initialization, the
backend performs this sequence:

1. `lv_init()` starts LVGL.
2. `lv_linux_fbdev_create()` creates an LVGL display with the Linux framebuffer
   driver.
3. `lv_linux_fbdev_set_file(display, "/dev/fb0")` opens the framebuffer.
4. `lv_display_set_default()` makes it the default LVGL display.
5. The backend reads the framebuffer width and height from LVGL.
6. It compares that size with the active DRM display mode found by
   `DisplayManager`.
7. It gets the active LVGL screen and applies the initial background colour.

The size check prevents IoT App from drawing with two different ideas of the
screen dimensions. If DRM reports `1920x1080`, `/dev/fb0` must also be
`1920x1080`.

This framebuffer approach does not require X11, Wayland, a desktop compositor,
Mesa, EGL, or OpenGL. LVGL's
[embedded Linux documentation](https://lvgl.io/docs/open/9.5/integration/embedded_linux/)
describes the available Linux display drivers.

LVGL is built from the repository-root submodule. CMake points LVGL at the
project's `lv_conf.h`, disables the upstream examples and demos, and builds
LVGL as a static library. Its code becomes part of the `iot_app` executable, so
the target does not need a separate project-owned `liblvgl.so` file.

## 4. Why LVGL has its own render thread

LVGL is not treated as thread-safe in this project. Two threads must not call
LVGL at the same time, including calls to `lv_timer_handler()`. See LVGL's
[threading guidance](https://lvgl.io/docs/open/9.5/integration/overview).

IoT App solves this by allowing only the render thread to call LVGL:

```text
Main or MicroPython thread                    Render thread

Request drawing
      |
      v
ScreenManager command queue  ------------->  Run queued command
                                              Call LVGL
                                              Call lv_timer_handler()
                                              Wait for work
```

`ScreenManager` is the safe entry point for the rest of IoT App. Its public
functions do not call LVGL. They copy the request into a command and place that
command in a bounded queue. The render thread removes commands in order and
passes them to `LvglFramebufferRenderBackend`.

The queue is bounded so a Python application cannot consume all memory by
creating drawing requests faster than the renderer can process them. If the
queue becomes full, the drawing request returns an error.

When `clear()` starts a new application screen, `ScreenManager` removes pending
commands from the previous application before it queues the clear operation.
This prevents old drawing commands from appearing on the new screen.

If the render thread throws an exception, `ScreenManager` stores it. The main
loop calls `throwIfRenderThreadFailed()` and stops instead of continuing with a
renderer that no longer works.

## 5. How a Python drawing request reaches LVGL

Python applications use `iot.display`; they do not import LVGL and they never
receive an `lv_obj_t *` pointer.

For a text box, the complete path is:

```text
Python application
  display.draw_text_box(...)
          |
          v
MicroPython C binding
  display_draw_text_box()
          |
          v
C-to-C++ bridge
  iot_display_draw_text_box()
          |
          v
ScreenManager
  drawTextBox()
  enqueueRenderCommand()
          |
          v
Render thread
  runRenderLoop()
          |
          v
LVGL backend
  createTextBox()
  createTextBoxWidgets()
          |
          v
LVGL framebuffer driver -> /dev/fb0 -> HDMI
```

The steps are:

1. `display_draw_text_box()` checks the Python arguments.
2. `iot_display_draw_text_box()` creates a C++ `TextBoxSpec`.
3. `ScreenManager::drawTextBox()` assigns a project widget ID and queues the
   request.
4. The render thread calls `IRenderBackend::createTextBox()`.
5. The LVGL backend creates the outer box and its label.
6. A later `lv_timer_handler()` call draws the changed area to `/dev/fb0`.

The returned widget ID is an IoT App ID, not an LVGL pointer. Python keeps the
ID and uses it to update, move, or delete the same text box later.

## 6. Public drawing operations

The public C++ entry point is
[`ScreenManager`](../../include/iot/ui/screen_manager.h). The backend contract
is [`IRenderBackend`](../../include/iot/ui/render_backend.h).

| `ScreenManager` function | What it does | Backend operation |
|---|---|---|
| `start()` | Starts the render thread and waits until LVGL and `/dev/fb0` are ready. | `initialize()` |
| `drawTextBox(spec)` | Queues a new text box and returns its widget ID. | `createTextBox()` |
| `updateTextBox(id, text)` | Replaces the label text in an existing box. | `updateTextBox()` |
| `moveTextBox(id, x, y)` | Moves the existing outer box. Its label moves with it. | `moveTextBox()` |
| `deleteTextBox(id)` | Deletes the outer box and its label. | `deleteTextBox()` |
| `fillArea(spec)` | Creates a solid rectangle. The current API does not return an ID for it. | `fillArea()` |
| `clear(colour)` | Removes application widgets and changes the screen background. | `clear()` |
| `showErrorScreen(spec)` | Removes application widgets and shows the native emergency screen. | `showErrorScreen()` |
| `throwIfRenderThreadFailed()` | Rethrows a stored render failure on the main thread. | No direct backend call |
| `stop()` | Stops and joins the render thread. | `shutdown()` |

### 6.1 Create a text box

`LvglFramebufferRenderBackend::createTextBox()` calls the private
`createTextBoxWidgets()` helper. The helper:

1. Validates that width and height are positive.
2. Creates a plain LVGL object for the outer box.
3. Applies its position, size, background, opacity, border, radius, and
   padding.
4. Disables scrolling on the box.
5. Creates an LVGL label inside the box.
6. Applies the text, wrapping, colour, and font.
7. Returns both LVGL pointers as a `TextBoxWidgets` value.

Normal text is centred in its box. The error-screen label is aligned to the
top-left because a traceback is easier to read that way.

The backend stores the two pointers in `textBoxes_`, indexed by the IoT App
widget ID:

```text
Widget ID 7 -> outer LVGL box pointer + LVGL label pointer
```

### 6.2 Update a text box

`updateTextBox()` finds the stored widget ID and calls:

```cpp
lv_label_set_text(label, updatedText.c_str());
```

It changes the existing label. It does not create another text box.

### 6.3 Move a text box

`moveTextBox()` finds the outer box and calls:

```cpp
lv_obj_set_pos(box, x, y);
```

The label is a child of the box, so it moves automatically.

### 6.4 Delete a text box

`deleteTextBox()` calls `lv_obj_delete()` on the outer box. LVGL deletes its
child label at the same time. The backend then removes the widget ID from its
map.

### 6.5 Draw a filled area

`fillArea()` creates a plain LVGL object with a solid background, no border,
and square corners. It is useful for coloured blocks and simple shapes.

The function currently returns no widget ID. A filled area remains until the
screen is cleared, and it cannot be moved, updated, or deleted on its own
through the public API.

### 6.6 Clear the screen

`clear()` performs three actions:

1. Deletes the native error layer if it exists.
2. Calls `lv_obj_clean(activeScreen)` to delete the active screen's children.
3. Sets the new screen background colour and full opacity.

It also clears the backend's text-box map because those LVGL objects no longer
exist.

### 6.7 Show the emergency screen

`showErrorScreen()` first clears normal application content. It then creates a
full-screen object on `lv_layer_top()` and places the error text box inside it.

The top layer appears above the active screen. A normal Python application
widget cannot cover the runtime-owned error message.

## 7. Important LVGL calls in the backend

The calls below are all made from
[`lvgl_framebuffer_render_backend.cpp`](../../src/ui/lvgl_framebuffer_render_backend.cpp).

### 7.1 Lifecycle and display calls

| LVGL call | Simple meaning in this project |
|---|---|
| `lv_init()` | Starts LVGL before any display or widget is created. |
| `lv_linux_fbdev_create()` | Creates an LVGL display that knows how to use Linux fbdev. |
| `lv_linux_fbdev_set_file(display, "/dev/fb0")` | Connects that display to the console framebuffer. |
| `lv_display_set_default(display)` | Makes this display the one used by calls that do not specify a display. |
| `lv_display_get_horizontal_resolution()` | Reads the framebuffer width reported to LVGL. |
| `lv_display_get_vertical_resolution()` | Reads the framebuffer height reported to LVGL. |
| `lv_display_delete(display)` | Deletes the LVGL display and releases its driver data. |
| `lv_deinit()` | Shuts down LVGL after the display has been deleted. |
| `lv_timer_handler()` | Processes pending LVGL work, redraws changed areas, and returns the next wait time. |

### 7.2 Widget-tree calls

| LVGL call | Simple meaning in this project |
|---|---|
| `lv_screen_active()` | Gets the root screen used for normal application widgets. |
| `lv_layer_top()` | Gets the layer used for the emergency screen. |
| `lv_obj_create(parent)` | Creates a plain rectangular widget inside `parent`. |
| `lv_label_create(parent)` | Creates a text label inside `parent`. |
| `lv_obj_clean(parent)` | Deletes all children of `parent`, but keeps `parent`. |
| `lv_obj_delete(widget)` | Deletes a widget and all of its children. |

### 7.3 Position and size calls

| LVGL call | Simple meaning in this project |
|---|---|
| `lv_obj_set_pos(widget, x, y)` | Places the widget relative to its parent's top-left corner. |
| `lv_obj_set_size(widget, width, height)` | Sets both dimensions. |
| `lv_obj_set_width(widget, width)` | Sets only the width. Labels use `LV_PCT(100)` to fill their parent. |
| `lv_obj_center(widget)` | Centres a normal text label in its parent box. |
| `lv_obj_align(widget, LV_ALIGN_TOP_LEFT, 0, 0)` | Places error text at the top-left of its parent. |
| `LV_PCT(100)` | Expresses a size as 100 percent of the parent instead of pixels. |

Coordinates use pixels. `(0, 0)` is the top-left corner. Positive X moves
right, and positive Y moves down.

### 7.4 Text and style calls

| LVGL call | Simple meaning in this project |
|---|---|
| `lv_color_make(r, g, b)` | Converts project RGB values into an LVGL colour. |
| `lv_label_set_text(label, text)` | Sets or replaces label text. |
| `lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP)` | Wraps long text onto more lines. |
| `lv_obj_set_style_bg_color()` | Sets a widget's background colour. |
| `lv_obj_set_style_bg_opa()` | Sets background opacity. `0` is transparent and `255` is solid. |
| `lv_obj_set_style_border_color()` | Sets border colour. |
| `lv_obj_set_style_border_width()` | Sets border thickness. Zero hides the border. |
| `lv_obj_set_style_radius()` | Rounds the corners. Zero keeps square corners. |
| `lv_obj_set_style_pad_all()` | Adds space between a box edge and its content. |
| `lv_obj_set_style_text_color()` | Sets label text colour. |
| `lv_obj_set_style_text_font()` | Selects the compiled LVGL font used by a label. |
| `lv_obj_set_style_text_align()` | Controls text alignment inside the label. |
| `lv_obj_remove_flag(widget, LV_OBJ_FLAG_SCROLLABLE)` | Prevents a box from scrolling when its content is large. |

## 8. Project data types and defaults

The project-facing types are defined in
[`ui_types.h`](../../include/iot/ui/ui_types.h). They keep LVGL types out of the
rest of the application.

### `Color`

```cpp
struct Color {
  std::uint8_t red;
  std::uint8_t green;
  std::uint8_t blue;
};
```

Each component is from 0 to 255.

### `Rect`

```cpp
struct Rect {
  std::int32_t x;
  std::int32_t y;
  std::int32_t width;
  std::int32_t height;
};
```

Width and height must be positive. Positions may be negative, although that
can place part or all of the object outside the visible screen.

### `TextBoxSpec`

The required C++ values are bounds and text. Its defaults create simple white
text with no visible box:

| Property | Default |
|---|---|
| Text colour | White `(255, 255, 255)` |
| Background colour | Black `(0, 0, 0)` |
| Border colour | White `(255, 255, 255)` |
| Background opacity | `0`, fully transparent |
| Border width | `0`, no visible border |
| Requested font size | `24` |

## 9. Font handling

IoT App compiles four Montserrat fonts into LVGL: 14, 20, 24, and 32 pixels.
`fontForSize()` maps a requested size to one of them:

| Requested size | Font selected |
|---|---|
| 0 to 14 | Montserrat 14 |
| 15 to 20 | Montserrat 20 |
| 21 to 24 | Montserrat 24 |
| 25 or greater | Montserrat 32 |

The current API does not select a font family, and FreeType is disabled. To add
another built-in or custom font, update `lv_conf.h`, compile the font, and
extend `fontForSize()`. LVGL's
[built-in font guide](https://lvgl.io/docs/open/9.5/main-modules/fonts/built_in_fonts.html)
explains the available font macros.

## 10. Project LVGL configuration

[`lv_conf.h`](../../config/lv_conf.h) controls which LVGL features are compiled
and how the framebuffer renderer uses memory.

| Setting | Project value | Why it is used |
|---|---:|---|
| `LV_COLOR_DEPTH` | `32` | Sets LVGL's native colour depth. The framebuffer driver still detects `/dev/fb0` and selects its actual 16-, 24-, or 32-bit display format. |
| `LV_MEM_SIZE` | 512 KiB | Gives LVGL space for widgets and drawing buffers on a 1920-pixel-wide display. |
| `LV_USE_OS` | `LV_OS_NONE` | IoT App owns the render thread and queue instead of using LVGL's OS integration. This does not mean the whole process is single-threaded. |
| `LV_USE_LOG` | `1` | Keeps LVGL's own logging available. |
| `LV_LOG_LEVEL` | Warning | Shows LVGL warnings and errors without normal informational noise. |
| `LV_USE_LINUX_FBDEV` | `1` | Enables the `/dev/fb0` driver. |
| `LV_USE_LINUX_DRM` | `0` | Prevents LVGL from taking DRM/KMS mode-setting ownership. IoT App uses DRM only for display discovery. |
| Render mode | Partial | Draws the screen in smaller sections instead of allocating a full-screen drawing buffer. |
| Buffer count | `1` | Uses one LVGL drawing buffer. |
| Buffer size | 20 rows | Keeps memory use smaller than a full `1920x1080` buffer. |
| `LV_LINUX_FBDEV_MMAP` | `1` | Maps framebuffer memory for writing. |
| Montserrat fonts | 14, 20, 24, 32 | Provides the four sizes used by `fontForSize()`. |
| FreeType, SDL, Wayland, X11 | Disabled | They are not needed by the console framebuffer design. |

The official [`lv_conf.h` reference](https://lvgl.io/docs/open/9.5/API/lv_conf_h.html)
lists the available options.

### Why partial rendering matters

A full `1920x1080` buffer consumes several megabytes. IoT App instead gives
LVGL enough drawing memory for 20 screen rows. LVGL renders one section, copies
it to the framebuffer, and reuses the buffer for another section.

This is why increasing `LV_LINUX_FBDEV_BUFFER_SIZE` also increases the LVGL
heap requirement. If LVGL reports that it cannot allocate a drawing buffer,
either reduce the row count or increase `LV_MEM_SIZE` after checking the
target's available RAM.

## 11. Using the display from MicroPython

The normal application author uses the project-owned `iot.display` module:

```python
from iot import display

screen_width, screen_height = display.size()

display.clear(color=(8, 13, 22))

message_box = display.draw_text_box(
    x=40,
    y=40,
    width=screen_width - 80,
    height=120,
    text="Hello from MicroPython",
    text_color=(255, 255, 255),
    background_color=(24, 34, 51),
    border_color=(64, 220, 255),
    background_opacity=255,
    border_width=2,
    font_size=24,
)

display.update_text_box(message_box, "The existing label changed")
display.move_text_box(message_box, 80, 180)
display.delete_text_box(message_box)
```

Python should keep the returned widget ID for as long as it needs to change or
delete that text box. Using an ID after its box was deleted or after the screen
was cleared raises an error when the render thread processes the command.

For the exact required arguments, optional arguments, return values, and more
examples, use the [MicroPython API guide](../micropython-api/README.md).

### Current input limitation

IoT App does not currently register a touch screen, mouse, keyboard, or
gamepad as an LVGL input device. The Adafruit gamepad is read through the
project's `iot.input` module. A Python scheduled callback reads it and then
updates display widgets when needed.

The MicroPython `scheduler.every()` function is also separate from LVGL's
timer system. The Python scheduler decides when an application callback runs;
the render thread's `lv_timer_handler()` call lets LVGL redraw the resulting
widget changes.

## 12. Using the display from C++

Most project code should use `ScreenManager`, not raw LVGL calls:

```cpp
iot::ui::TextBoxSpec textBoxSpec;
textBoxSpec.bounds             = {40, 40, 600, 100};
textBoxSpec.text               = "Hello from C++";
textBoxSpec.backgroundColor    = {24, 34, 51};
textBoxSpec.backgroundOpacity  = 255;
textBoxSpec.borderColor        = {64, 220, 255};
textBoxSpec.borderWidth        = 2;

const iot::ui::WidgetId textBoxId = screenManager.drawTextBox(textBoxSpec);
screenManager.updateTextBox(textBoxId, "Updated text");
```

Raw `lv_...` functions belong inside the LVGL render backend and must execute
on the render thread. Calling LVGL directly from the main, MQTT, or
MicroPython thread would break the project's threading rule.

## 13. Shutdown and object ownership

The normal shutdown order is:

1. `ScreenManager::stop()` tells the render loop to finish.
2. The render thread calls `IRenderBackend::shutdown()`.
3. The backend clears its project-side widget records.
4. `lv_display_delete()` deletes the display and releases framebuffer driver
   resources.
5. `lv_deinit()` shuts down LVGL.
6. The C++ render thread is joined before `stop()` returns.

`shutdown()` is safe to call more than once. This matters when startup fails
partway through and the destructor still needs to clean up the resources that
were created successfully.

## 14. Common problems

### `/dev/fb0` does not exist

The Linux kernel has not created a framebuffer device. Check:

```bash
ls -l /dev/fb*
```

On Buildroot, confirm that the kernel DRM framebuffer emulation and the project
device configuration are enabled.

### Permission denied for `/dev/fb0`

The runtime user needs write access to the framebuffer. Check the device owner
and groups:

```bash
ls -l /dev/fb0
id
```

### Framebuffer size and DRM mode do not match

IoT App deliberately stops when the two sizes differ. Check them with:

```bash
fbset -fb /dev/fb0 -s
kmsprint
```

Configure Linux so both use the same resolution. IoT App does not change the
resolution itself.

### LVGL cannot allocate memory

An error mentioning a null display buffer or failed `lv_malloc()` usually
means the private LVGL heap is too small for the configured partial-buffer
height and framebuffer pixel size. Review `LV_MEM_SIZE` and
`LV_LINUX_FBDEV_BUFFER_SIZE` together.

### The screen does not update

Check that the render thread is still running and that
`processEventsAndGetWaitMilliseconds()` continues to call
`lv_timer_handler()`. Also check the IoT App and LVGL logs for an earlier
render failure.

### The drawing queue is full

The Python application is sending commands faster than the render thread can
process them. Reuse existing widgets with `update_text_box()` and
`move_text_box()` instead of creating new widgets repeatedly. A scheduled
callback should do a small amount of work and then return.

### A deleted widget ID is used again

After `delete_text_box()` or `clear()`, the old ID no longer refers to an LVGL
object. Remove it from the Python application's state and create a new widget
when needed.


## 15. Official LVGL references

- [Learn the basics](https://lvgl.io/docs/open/9.5/getting_started/learn_the_basics.html)
- [Widget tree](https://lvgl.io/docs/open/9.5/common-widget-features/tree.html)
- [All widgets](https://lvgl.io/docs/open/9.5/widgets/)
- [Styles](https://lvgl.io/docs/open/9.5/common-widget-features/styles/overview.html)
- [Integration, operating systems, and threads](https://lvgl.io/docs/open/9.5/integration/overview)
- [Timers and `lv_timer_handler()`](https://lvgl.io/docs/open/9.5/main-modules/timer.html)
- [Embedded Linux support](https://lvgl.io/docs/open/9.5/integration/embedded_linux/)
- [Linux framebuffer driver](https://lvgl.io/docs/open/9.5/integration/embedded_linux/drivers/fbdev.html)
- [Linux framebuffer API](https://lvgl.io/docs/open/9.5/API/drivers/display/fb/lv_linux_fbdev_h.html)
- [`lv_conf.h` reference](https://lvgl.io/docs/open/9.5/API/lv_conf_h.html)
- [Built-in fonts](https://lvgl.io/docs/open/9.5/main-modules/fonts/built_in_fonts.html)
