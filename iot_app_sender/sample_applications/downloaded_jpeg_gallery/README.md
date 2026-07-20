# Downloaded JPEG gallery

This application downloads five JPEG files over HTTPS and changes the
background every five seconds. The first three pictures use their original
size. The last two have a 50-percent scale limit. All five use `center`, so
each picture is placed in the middle of the screen. The title and status text
stay above each background.

Before downloading, the app draws a loading screen. Its status line shows
which file it is downloading. Python waits for each download, but the render
thread keeps running so this message can appear on the monitor.

IoT App uses libjpeg-turbo to decode the pictures before LVGL draws them. It
chooses the largest decoder-supported size that does not exceed each picture's
scale limit, then keeps recently decoded pixels in the current application's
memory cache. Returning to a picture reuses those pixels when they are still
in the cache.

The example shows how these APIs work together:

- `network.download_file()` downloads and verifies each file.
- `display.set_background_image()` shows the cached JPEG path.
- `scheduler.every()` changes the image without an infinite Python loop.

Each SHA-256 value was calculated from the file returned by its URL. This lets
the application reject a changed or incomplete download and reuse a matching
cached copy.

These files belong to external websites and may change or become unavailable.
A changed file will fail the SHA-256 check. Only update a hash after checking
the new file yourself. Omitting `expected_sha256` is also supported, but then
the application no longer checks the file against a value you already trust.

The Raspberry Pi needs internet access and a working system clock for HTTPS
certificate checks. No extra hardware is required.

Use this directory in the `application` section of `sender_config.json`:

```json
{
  "directory": "sample_applications/downloaded_jpeg_gallery"
}
```

To try your own pictures, replace each URL and SHA-256 value in `main.py`. The
file must contain valid JPEG image data. Its filename extension does not matter.
On Ubuntu, calculate the expected value with:

```bash
sha256sum your-picture.jpg
```

See the [MicroPython API guide](../../../iot_app/docs/micropython-api/README.md)
for the JPEG dimension limits, scale range, and background modes.

## Startup time and errors

IoT App replies `accepted` before running this Python app or downloading any
pictures. The sender exits on that reply without waiting for the downloads.
Acceptance confirms validation and temporary installation, not successful
Python execution. Five downloads can take longer than one request, but each
transfer still has its own 30-second limit.

This example lets download, hash-check, and JPEG errors reach IoT App. It then
stops the Python application and shows the C++ emergency screen. Whether the
error happens during startup or in the gallery timer, the traceback appears
on the device and in its log. No further deployment status is sent.

Decoded pictures share a 32 MiB cache. A background replacement needs room
for both its old and new pixels while the renderer switches them. If your
replacement pictures exceed that limit, reduce their scale or use smaller
source images.
