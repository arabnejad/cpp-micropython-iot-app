# Video playback

This guide explains how IoT App plays full-screen video. It also records the
tests run on the Raspberry Pi 4 and why the current playback settings were
chosen.

Video playback starts with compressed data in a media file and ends with a
sequence of decoded pictures appearing on a monitor. The sections below follow
that path and explain where H.264, MP4, FFmpeg, libmpv, the Raspberry Pi video
decoder, OpenGL, and the Linux display system are used.

## Video basics

### A video is a timed sequence of pictures

A video frame is one picture. Showing frames quickly, at the correct times,
makes them appear to move.

```text
Frame 1       Frame 2       Frame 3       Frame 4
00:00.000     00:00.042     00:00.083     00:00.125
   |             |             |             |
   v             v             v             v
[picture] --> [picture] --> [picture] --> [picture]

             24 frames are shown each second
```

The main properties are:

| Term | Meaning | Example used in testing |
|---|---|---|
| Resolution | The width and height of each frame | 1920 x 1080 pixels |
| Frame rate | How many frames should be shown each second | 24 frames per second |
| Duration | How long the sequence lasts | 60 seconds |
| Bit rate | How many encoded bits are read each second | Depends on the video file |

Frame rate and monitor refresh rate are related but different. A 24 fps video
contains 24 new frames per second. A 60 Hz monitor refreshes its output 60
times per second. The player decides when each video frame should appear on
that monitor.

The video and the monitor run at the same time. During one second, the video
provides 24 different frames while the monitor redraws the screen 60 times.
The extra monitor redraws repeat the current video frame; they do not make the
video take longer to play:

```text
One second = 1,000 milliseconds

Monitor redraw interval = 1,000 ms / 60 redraws = 16.666... ms ~= 16.7 ms
Video frame interval    = 1,000 ms / 24 frames  = 41.666... ms ~= 41.7 ms


Time:          0    16.7   33.3   50.0   66.7   83.3 ms
Monitor draws: A      A      A      B      B      C
```

This is one monitor redrawing video frame A three times, then video frame B
twice. It does not mean that two or three monitors are involved. The count
alternates because `60 / 24 = 2.5`, but a monitor cannot redraw half a frame.
Across two video frames, the monitor redraws five times: three redraws for one
frame and two for the other.

After one second, the player has shown 24 different video frames across 60
monitor redraws. After two seconds, it has shown 48 video frames across 120
monitor redraws.

### Why video is compressed

An uncompressed frame stores a colour value for every pixel. One 1920 x 1080
RGB frame with three bytes per pixel needs about 5.9 MiB:

```text
1920 x 1080 x 3 bytes = 6,220,800 bytes
```

At 24 frames per second, that is about 142 MiB every second. A one-minute clip
would need several gigabytes before adding audio. Most neighbouring video
frames are similar, so storing every frame in full would waste space.

A video encoder compresses the frames before the file is distributed. H.264
can store complete picture information at selected points and describe parts
of later pictures using already decoded pictures. A decoder follows those
instructions to reconstruct the frames during playback. H.264 normally uses
lossy compression, so the reconstructed frame may not be exactly the same as
the original, but it can be much smaller.

```text
Recording or video editor
          |
          | raw frames
          v
      H.264 encoder
          |
          | compressed video packets
          v
        MP4 file


        MP4 file
          |
          | compressed video packets
          v
      H.264 decoder
          |
          | reconstructed frames
          v
        Monitor
```

Encoding happens when a video is created. Decoding happens when it is played.
IoT App only performs the playback side.

### Codec and container are not the same thing

H.264 and MP4 describe different parts of the file:

- **H.264 is the video codec.** It defines how video frames are compressed and
  reconstructed.
- **MP4 is the container.** It holds one or more streams and the information
  needed to play them at the correct times. An MP4 may contain H.264 video,
  audio, subtitles, and metadata.

The file extension tells us the container, not necessarily the video codec.
Two `.mp4` files can contain video encoded with different codecs. IoT App only
claims support for H.264 video inside an MP4 container because that is the
combination tested on the Raspberry Pi images.

Opening the container and separating its streams is called **demultiplexing**,
or **demuxing** for short. The video packets then go to the matching decoder.
The [FFmpeg pipeline documentation](https://ffmpeg.org/ffmpeg.html#Detailed-description)
describes this same flow: a demuxer reads encoded packets and a decoder turns
them into raw video frames.

```text
                         MP4 container
                  +-----------------------+
                  | H.264 video packets   |
                  | AAC audio packets     |
                  | timing and metadata   |
                  +-----------+-----------+
                              |
                           demuxer
                     +--------+--------+
                     |                 |
                     v                 v
              H.264 decoder       audio decoder
                     |                 |
                     v                 v
              video frames       audio samples

IoT App uses the video path and disables the audio path.
```

### Decoded frames still need a pixel format

After decoding, the player needs to know how colour values are arranged in
memory. This arrangement is called the pixel format.

RGB stores red, green, and blue values for each pixel. Video commonly uses YUV
instead:

```text
Y = brightness
U = one part of the colour information
V = the other part of the colour information
```

The test output reports `yuv420p`. In this format, every pixel has a brightness
sample, while each 2 x 2 group of pixels shares one U and one V sample. This
uses 12 bits, or 1.5 bytes, per pixel instead of the three bytes used by RGB24.
FFmpeg's
[`AVPixelFormat` definition](https://github.com/FFmpeg/FFmpeg/blob/master/libavutil/pixfmt.h)
describes `YUV420P` as planar 4:2:0 data with one pair of colour samples for
each 2 x 2 group of brightness samples.

The `p` means **planar**. The Y, U, and V values are kept in separate memory
areas rather than stored next to one another for every pixel.

```text
Four displayed pixels:       Brightness samples: Y0 Y1 Y2 Y3

Y0  Y1                       Shared colour:       one U and one V
Y2  Y3
```

The monitor ultimately needs display-ready pixels. The GPU can scale the
decoded frame to the monitor and convert its YUV colour data while drawing it.

### Decoding and drawing are separate jobs

Hardware decoding does not mean that every playback step runs in hardware.
The pipeline has several jobs:

```text
MP4 file
   |
   v
Demux MP4 and read H.264 packets
   |
   v
Decode H.264 packets into frames
   |
   v
Scale and convert the frame for the monitor
   |
   v
Present the frame at the correct time
```

The CPU can perform decoding in software, or a dedicated video block can do
it. The GPU can still draw the decoded frames in either case. This is why the
test result `HWDEC=no` can appear together with `VO: [gpu]`.

On the Raspberry Pi, mpv reaches the hardware decoder through Linux V4L2. The
name `v4l2m2m` means **Video4Linux2 memory-to-memory**: compressed data is sent
from memory to a hardware device, and decoded frames come back into memory.
The [Linux V4L2 documentation](https://docs.kernel.org/userspace-api/media/v4l/dev-mem2mem.html)
describes codecs as a common use of this interface.

The selected `v4l2m2m-copy` mode copies each decoded frame into normal system
memory before OpenGL draws it. A copy sounds slower, but the Raspberry Pi test
later in this guide recorded no dropped frames with this mode. The direct
DRM PRIME route dropped many frames on the same system, so IoT App uses the
measured result rather than choosing a path only because it avoids a copy.

### What the playback components do

The names in mpv's output describe different layers. No single one of these
components is the complete video player.

| Component | Job in this project |
|---|---|
| FFmpeg libraries | Read the MP4 streams and provide the H.264 decoding support used by mpv |
| V4L2 M2M | Gives mpv access to the Raspberry Pi video decoder |
| OpenGL | Uses the GPU to convert, scale, and draw decoded video frames |
| EGL | Connects OpenGL to the Linux display system and manages its drawing surface |
| DRM/KMS | Selects the monitor, display mode, and buffers that are shown on screen |
| mpv | Coordinates the file, timing, decoder, renderer, errors, and end of playback |
| libmpv | Lets IoT App control mpv through a C API instead of starting the `mpv` command |
| LVGL | Draws the normal IoT App interface before and after the video; it is stopped during playback |

In this guide, DRM means the Linux **Direct Rendering Manager**, not digital
rights management. KMS means **Kernel Mode Setting**. Together they let a
program choose a connector and display mode, then present image buffers on the
monitor. The Linux kernel's
[KMS documentation](https://docs.kernel.org/gpu/drm-kms.html) describes the
framebuffers and page flips used by this display layer.

EGL sits between OpenGL and the native display system. It creates drawing
contexts and surfaces and handles synchronization. The
[Khronos EGL overview](https://www.khronos.org/egl) gives the same role in its
description of EGL.

The full playback path on this Raspberry Pi is:

```text
demo.mp4 on disk
       |
       v
mpv / FFmpeg demuxes the MP4 container
       |
       v
H.264 packets
       |
       v
Raspberry Pi decoder through V4L2 M2M
       |
       v
Decoded YUV420P frames copied to system memory
       |
       v
OpenGL converts and scales the frames
       |
       v
EGL connects OpenGL output to DRM/KMS
       |
       v
Selected HDMI monitor
```

## How libmpv works

The `mpv` command and libmpv use the same player core. The command is useful
for testing from a shell. libmpv is the C API used when another program wants
to embed and control that player.

IoT App does not implement MP4 parsing, H.264 decoding, frame timing, or GPU
rendering itself. It tells libmpv which file and display to use, and libmpv
coordinates those jobs.

### The libmpv lifecycle

The player follows the same small sequence for each video:

```text
mpv_create()
    |
    | Create an uninitialized player handle
    v
mpv_set_option_string()
    |
    | Select no audio, full screen, V4L2 decoding,
    | OpenGL rendering, and the active monitor
    v
mpv_initialize()
    |
    | Start the configured player core
    v
mpv_command(... "loadfile" ...)
    |
    | Ask the player to open and play the file
    v
mpv_wait_event()
    |
    | Wait for errors, shutdown, or end-of-file
    | Playback continues inside mpv while IoT App waits
    v
MPV_EVENT_END_FILE
    |
    | Report success or turn the mpv error into a C++ exception
    v
mpv_terminate_destroy()
    |
    v
Release the player and its display resources
```

The official
[`mpv/client.h` documentation](https://github.com/mpv-player/mpv/blob/v0.40.0/include/mpv/client.h)
describes handle creation, options, initialization, commands, and the event
loop. The
[small official libmpv example](https://github.com/mpv-player/mpv-examples/blob/master/libmpv/simple/simple.c)
shows the same create, initialize, load, wait, and destroy sequence.

`mpv_wait_event()` does not decode a frame itself. It lets IoT App wait for
notifications from the player. mpv continues its own playback work while the
IoT App call is waiting.

IoT App does not turn on mpv configuration files or terminal controls. The
[`mpv_create()` documentation](https://github.com/mpv-player/mpv/blob/v0.40.0/include/mpv/client.h#L429-L460)
explains that an embedded player leaves those features disabled unless its
caller enables them.

### How the Python call reaches libmpv

The code keeps the MicroPython binding, screen lifetime, and mpv details in
separate places:

```text
Python application
display.play_video(path)
          |
          v
mod_iot_display.c
Converts the Python value to a C string
          |
          v
display_cpp_bridge.cpp
Finds the active C++ application context
          |
          v
ScreenManager::playExclusiveVideoAndWait()
Checks the file and controls the screen transition
          |
          v
IExclusiveVideoPlayer
Small boundary used by ScreenManager and its tests
          |
          v
MpvExclusiveVideoPlayer::playVideoAndWait()
Configures libmpv and waits for the result
```

The implementation can be followed in this order:

1. [`mod_iot_display.c`](../../micropython_iot_modules/display/mod_iot_display.c)
   receives `display.play_video()` from Python.
2. [`display_cpp_bridge.cpp`](../../micropython_iot_modules/display/display_cpp_bridge.cpp)
   forwards the request to the active application's `ScreenManager`.
3. [`screen_manager.cpp`](../../src/ui/screen_manager.cpp) checks the path,
   stops framebuffer rendering, calls the player, and starts rendering again.
4. [`iexclusive_video_player.h`](../../include/iot/video/iexclusive_video_player.h)
   keeps libmpv out of `ScreenManager`'s public design.
5. [`mpv_exclusive_video_player.cpp`](../../src/video/mpv_exclusive_video_player.cpp)
   contains the libmpv calls and Raspberry Pi playback options.

### Why LVGL stops during playback

LVGL normally draws IoT App through the Linux framebuffer. mpv uses
DRM/EGL/OpenGL to control the same monitor directly. Allowing both renderers to
write to the display at the same time would make ownership unclear and could
leave either renderer showing stale content.

`ScreenManager` therefore stops its LVGL render thread and closes the
framebuffer backend before it calls libmpv. When the video ends, the libmpv
handle is destroyed and its display resources are released. `ScreenManager`
then opens the framebuffer again. The Python application redraws its interface
because the old LVGL widgets no longer exist.

Only the display renderer stops. The IoT App process, MicroPython interpreter,
MQTT receiver, and application manager keep running. Stopping the whole service
would destroy the Python application that requested the video, so there would
be no caller left to handle a playback error or draw the next screen.

### Common points of confusion

- An `.mp4` extension does not prove that the video codec is H.264.
- `HWDEC=no` means software video decoding; it does not mean GPU output is off.
- Hardware decoding only covers the decode stage, not every stage of playback.
- `v4l2m2m-copy` adds a memory copy, but it was faster in the measured test than
  the direct path used on that system.
- IoT App links to libmpv. It does not start the `/usr/bin/mpv` command for
  normal application playback.
- `display.play_video()` opens a local file. It does not download a URL.
- Video playback is currently blocking. Python continues after the video ends,
  not once the video merely starts.

## Raspberry Pi playback tests

The first hardware check was done on Raspberry Pi OS. That made it possible to
test mpv and its command-line options before changing either embedded Linux
image. The same test should be repeated on Buildroot and Yocto after their new
images have been built.

The test file was an H.264 MP4 at 1920 x 1080 and 24 frames per second. IoT App
was stopped so mpv could use the monitor directly.

### Software decoding with GPU output

The first test used mpv's normal hardware-decoder selection:

```bash
sudo mpv \
  --no-config \
  --fullscreen \
  --audio=no \
  --vo=gpu \
  --gpu-api=opengl \
  --gpu-context=drm \
  --hwdec=auto \
  --profile=fast \
  --term-status-msg='HWDEC=${hwdec-current} | VO dropped=${frame-drop-count} | Decoder dropped=${decoder-frame-drop-count}' \
  ~/Videos/h264-1080p-test.mp4
```

The result was:

```text
VO: [gpu] 1920x1080 yuv420p
HWDEC=no | VO dropped=1 | Decoder dropped=0
```

`HWDEC=no` means that the CPU decoded the H.264 stream. OpenGL and DRM still
handled the final display output. mpv reported one dropped output frame during
this test.

This mpv build does not accept `--verbose`. Use `-v` when detailed output is
needed. A log can also be saved with
`--log-file=/tmp/mpv-drm-test.log` and searched afterwards:

```bash
grep -Ei 'hardware decoding|hwdec|drmprime|video output' \
  /tmp/mpv-drm-test.log
```

### Direct V4L2 hardware-decoded frames

`--hwdec=auto-unsafe` found the Raspberry Pi V4L2 decoder:

```text
Using hardware decoding (v4l2m2m).
VO: [gpu] 1920x1080 drm_prime[yuv420p]
HWDEC=v4l2m2m | VO dropped=171 | Decoder dropped=0
```

The decoder dropped no frames, but the video-output stage dropped 171. The
problem was therefore after decoding, in the DRM PRIME presentation path. This
was much worse than software decoding on this system and is not used by IoT
App.

### V4L2 hardware decoding with copied frames

The third test selected `v4l2m2m-copy` and fixed the monitor mode:

```bash
sudo mpv \
  --no-config \
  --fullscreen \
  --audio=no \
  --vo=gpu \
  --gpu-api=opengl \
  --gpu-context=drm \
  --hwdec=v4l2m2m-copy \
  --profile=fast \
  --drm-mode=1920x1080@60 \
  --term-status-msg='HWDEC=${hwdec-current} | VO dropped=${frame-drop-count} | Decoder dropped=${decoder-frame-drop-count}' \
  ~/Videos/h264-1080p-test.mp4
```

The result was:

```text
Using hardware decoding (v4l2m2m-copy).
VO: [gpu] 1920x1080 yuv420p
HWDEC=v4l2m2m-copy | VO dropped=0 | Decoder dropped=0
```

This was the best result. mpv reported hardware H.264 decoding, normal
`yuv420p` output for the OpenGL renderer, and no dropped frames.

That Raspberry Pi OS test used the `fast` profile provided by its newer mpv
version. Yocto uses mpv 0.35.1, which does not have that named profile. The
project therefore sets `scale=bilinear` and `dscale=bilinear` directly in its
player and test command. Both options exist in the mpv versions used by the
Buildroot and Yocto images. Compare the built-in profiles in
[mpv 0.35.1](https://github.com/mpv-player/mpv/blob/v0.35.1/etc/builtin.conf)
and [mpv 0.40.0](https://github.com/mpv-player/mpv/blob/v0.40.0/etc/builtin.conf).

The [mpv hardware-decoding guide](https://mpv.io/manual/master/#options-hwdec)
uses the `-copy` suffix for modes that copy decoded video back into system
memory. It explains that behaviour in general, but it does not list
`v4l2m2m-copy` by name. That exact mode came from `mpv --hwdec=help` on the
tested Raspberry Pi.

The copy is extra work, but it avoided the slow DRM PRIME path seen in the
previous test. For this Raspberry Pi and software combination, the measured
result matters more than assuming that a no-copy path must be faster.

## Meaning of the warnings

mpv printed these messages when it was started through SSH:

```text
VT_GETMODE failed: Inappropriate ioctl for device
Failed to set up VT switcher. Terminal switching will be unavailable.
```

An SSH session is not a physical Linux virtual terminal, so mpv cannot manage
console switching. Video playback still worked. These warnings do not mean
that decoding failed.

The earlier test also said that no preferred mode was found. Supplying
`drm-mode=1920x1080@60` removed that uncertainty. IoT App builds this value from
the display selected during startup. It also supplies the selected DRM device
and connector so mpv uses the same monitor as LVGL. The mpv manual documents
the [`drm-mode`, `drm-device`, and `drm-connector` options](https://mpv.io/manual/master/#video-output-drivers).

## Project reference guides

The sections above explain video files, decoding, libmpv, and the Raspberry Pi
tests. These project guides hold the details that developers are most likely
to check while working on IoT App:

| Information | Guide |
|---|---|
| Python arguments, blocking behaviour, errors, and a complete example | [`display.play_video()` in the MicroPython API guide](../micropython-api/README.md#displayplay_video) |
| The handoff between Python, `ScreenManager`, LVGL, and libmpv | [Full-screen video workflow in the system-design guide](../system-design/README.md#115-how-a-full-screen-video-reaches-the-monitor) |
| Commands for checking hardware decoding and dropped frames on a Raspberry Pi | [Video troubleshooting in the device-image guide](../device-image/README.md#video-does-not-play-smoothly) |

### End-to-end image check

Use this order after changing the image or video packages:

1. Build the Buildroot or Yocto image, flash it, and boot the Raspberry Pi.
2. Copy the same H.264 1080p test file to `/data`. The
   [full-screen video sample](../../../iot_app_sender/sample_applications/full_screen_video/README.md)
   shows the copy and permission commands.
3. Stop IoT App and run `iot-app-check-video-playback` as described in the
   [device-image guide](../device-image/README.md#video-does-not-play-smoothly).
4. Confirm `HWDEC=v4l2m2m-copy` and check both dropped-frame counters.
5. Start IoT App and send the `full_screen_video` sample application.
6. Confirm that LVGL closes during playback, the video remains smooth, and the
   completion screen appears after the framebuffer reopens.

## Supported scope

Video playback currently has these limits:

- One video at a time.
- Local normal files only; URLs are not passed directly to mpv.
- H.264 video in an MP4 container is the format this project tests and
  supports. Other formats accepted by libmpv are not guaranteed by IoT App.
- Full-screen output on the monitor selected when IoT App started.
- The monitor's existing resolution and refresh rate.
- No audio.
- No pause, seek, loop, subtitles, or LVGL overlay.

A Python application can use `network.download_file()` first and pass its
returned path to `display.play_video()`. That downloader has a 10 MiB limit, so
larger videos should be copied to a directory such as
`/data/iot-app/videos/`.

## Build support

Video playback is part of every IoT App build. A native build therefore needs
the libmpv development package:

```bash
sudo apt install libmpv-dev mpv
make iot-app
```

CMake stops during configuration if it cannot find libmpv. The Buildroot and
Yocto recipes both depend on mpv, so their images include the same playback
support.

The Buildroot configuration selects mpv, Mesa's Raspberry Pi V3D driver, EGL,
GBM, and OpenGL ES. The Yocto configuration keeps the `opengl` distribution
feature and enables mpv's DRM, GBM, EGL, and OpenGL options. X11, Wayland,
audio, and Lua controls are not required.

Adding mpv, FFmpeg, Mesa, and their supporting libraries will make both images
larger and will increase the first build time. The default Linux root partition
is 512 MiB; `/data` still receives the remaining space on the card. The
[storage guide](../storage/README.md) explains that layout and how to change
it.

Package versions, graphics options, and image-licence notes belong to the
[Buildroot guide](../buildroot/README.md#12-files-that-implement-the-buildroot-image)
and the
[Yocto guide](../yocto/README.md#18-files-that-implement-the-yocto-image). The
[FFmpeg licence flag](../yocto/README.md#ffmpeg-licence-flag) used by the Yocto
build is explained separately.

## Why video does not pass through LVGL

A video is a series of images called frames. One possible design was to pass
every frame through the same LVGL screen used by the dashboard:

```text
libmpv decodes a video frame
        |
        v
libmpv converts and scales it on the CPU
        |
        v
XRGB8888 image in memory
        |
        v
IoT App gives the image to LVGL
        |
        v
LVGL draws it on the framebuffer
```

This design has one useful advantage: LVGL could draw text boxes, buttons, or
other widgets over the video. The cost is that the whole path must run again
for every video frame.

XRGB8888 describes how one pixel is stored in memory. The name can be read as:

```text
X          Red        Green      Blue
8 bits     8 bits     8 bits     8 bits
unused     0 to 255   0 to 255   0 to 255
```

The red, green, and blue values are combined to produce the pixel's colour.
For example, red at 255 with green and blue at 0 produces a bright red pixel.
The `X` part reserves another eight bits but does not contain transparency or
other image data. A format such as ARGB8888 uses that extra byte for an alpha,
or transparency, value instead. The exact order of these bytes in memory can
depend on the processor and graphics API, but each XRGB8888 pixel always needs
32 bits, or four bytes.

The size of one 1920 x 1080 frame is therefore:

```text
1920 x 1080 x 4 bytes = 8,294,400 bytes, or about 7.9 MiB
```

At 24 frames per second, copying each frame once already moves about 190 MiB of
data per second. The complete path may need more than one copy.

The problem is not only the copy. The libmpv
[software-renderer documentation](https://github.com/mpv-player/mpv/blob/v0.40.0/include/mpv/render.h#L121-L149)
states that colour conversion, scaling, subtitles, and other on-screen drawing
are performed by one CPU thread. It warns that large videos or displays can be
too slow for real-time playback.

IoT App therefore uses a shorter path:

```text
libmpv
  |
  v
Raspberry Pi v4l2m2m-copy hardware decoder
  |
  v
OpenGL and the Linux display system
  |
  v
Monitor
```

This is called exclusive playback because LVGL temporarily closes the
framebuffer and mpv controls the display. The dashboard and its widgets cannot
appear over the video. When the video ends, mpv releases the display and IoT
App starts LVGL again.

The Raspberry Pi test played the 1920 x 1080 H.264 sample with hardware
decoding and no dropped frames. This made exclusive playback the better first
choice: it gives smooth full-screen video, with the clear limitation that LVGL
overlays are not available during playback.

## Possible later work

### Playback controls that do not block Python

The current `display.play_video()` call waits until the video finishes. For
example:

```python
print("Before video")
display.play_video("/data/iot-app/videos/demo.mp4")
print("After video")
```

`After video` is not printed until playback ends. While Python is waiting in
this call, the main thread also cannot run Python timer callbacks or process a
new application deployment. The MQTT thread may receive a deployment message,
but the main thread cannot act on it until the video finishes.

Pause, resume, stop, and status operations would need a different API. Starting
a video would have to return immediately, while a player object remained alive
and continued playing in the background. Later calls could then control that
same player:

```python
display.start_video("/data/iot-app/videos/demo.mp4")
display.pause_video()
display.resume_video()
display.stop_video()
```

The main loop would also need to check mpv events, report playback errors, and
stop the player when the Python application is replaced. The current blocking
call avoids those extra states: one call starts the video, waits for its final
result, and then restores the LVGL screen.

### Showing LVGL controls over a video

LVGL and mpv currently use the display at different times:

```text
Before and after video:  LVGL -> Linux framebuffer -> monitor
During video:            mpv  -> OpenGL and DRM   -> monitor
```

This is why an LVGL button or text box cannot appear over a playing video. mpv
has exclusive control of the display while LVGL is stopped.

Supporting overlays would require both outputs to meet in one GPU renderer:

```text
mpv video frame -----------+
                            |
                            v
                       combine both ------> DRM ------> monitor
                            ^
                            |
LVGL controls -------------+
```

In that design, mpv would provide each decoded frame as an OpenGL texture. LVGL
would draw its controls into another texture. OpenGL would combine the two
textures, and DRM would send the completed frame to the monitor. EGL would be
used to create the OpenGL rendering context without requiring a desktop or
window system.

This would replace the current framebuffer backend and add responsibility for
video timing, GPU resources, LVGL refreshes, and display ownership. It is a
larger rendering-system change rather than a small addition to the existing
video player, so the current implementation supports full-screen video without
LVGL overlays.

## References

- [FFmpeg media pipeline and demuxing](https://ffmpeg.org/ffmpeg.html#Detailed-description)
- [FFmpeg pixel-format definitions](https://github.com/FFmpeg/FFmpeg/blob/master/libavutil/pixfmt.h)
- [ITU H.264 video-coding standard](https://www.itu.int/rec/t-rec-h.264)
- [Linux V4L2 memory-to-memory devices](https://docs.kernel.org/userspace-api/media/v4l/dev-mem2mem.html)
- [Linux DRM/KMS display layer](https://docs.kernel.org/gpu/drm-kms.html)
- [Khronos EGL overview](https://www.khronos.org/egl)
- [Embedding mpv as libmpv](https://mpv.io/manual/master/#embedding-into-other-programs-libmpv)
- [mpv hardware-decoding and `-copy` options](https://mpv.io/manual/master/#options-hwdec)
- [mpv DRM connector and mode options](https://mpv.io/manual/master/#video-output-drivers)
- [Official libmpv client header and embedded-player defaults](https://github.com/mpv-player/mpv/blob/v0.40.0/include/mpv/client.h#L429-L460)
- [Official simple libmpv example source](https://github.com/mpv-player/mpv-examples/blob/master/libmpv/simple/simple.c)
- [Official libmpv render API and software-renderer warning](https://github.com/mpv-player/mpv/blob/v0.40.0/include/mpv/render.h#L121-L149)
