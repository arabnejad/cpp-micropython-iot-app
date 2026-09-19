# Full-screen video

This example plays one local video and draws a completion screen when playback
returns. It catches playback errors so a missing or invalid file can be shown
on the monitor without stopping the Python application.

The video is not included in the MQTT application package. H.264 in an MP4
container is the format tested and supported by IoT App.

For a quick test, the
[`chthomos/video-media-samples`](https://github.com/chthomos/video-media-samples)
repository provides a 30-second, 1080p H.264/AAC version of Big Buck Bunny.
Run this command on the development computer to download it as `demo.mp4`:

```bash
curl --fail --location \
  https://raw.githubusercontent.com/chthomos/video-media-samples/master/big-buck-bunny-1080p-30sec.mp4 \
  --output demo.mp4
```

The repository's
[`LICENSE.md`](https://github.com/chthomos/video-media-samples/blob/master/LICENSE.md)
identifies Big Buck Bunny as Creative Commons Attribution 3.0 material.

Before sending the application, copy the video to the path used by `main.py`:

```bash
ssh root@rspi-iot-app.local '
  grep -q " /data " /proc/mounts || {
    echo "The optional /data partition is not mounted"
    exit 1
  }
  mkdir -p /data/iot-app/videos
'
# Buildroot uses Dropbear, so run this command on the development computer.
# Capital -O tells a newer scp client to use the original SCP protocol.
scp -O demo.mp4 root@rspi-iot-app.local:/data/iot-app/videos/demo.mp4
ssh root@rspi-iot-app.local \
  'chmod 0644 /data/iot-app/videos/demo.mp4'
```

The Yocto image includes an SFTP server, so the same transfer can use `scp`
without `-O`. The permission command is still required because IoT App runs as
the unprivileged `iot-app` user and must be able to read the video.

This check matters because `/data` is optional. It prevents the command from
creating the video directory on the small Linux root partition when the data
partition is unavailable.

Then select this application in `iot_app_sender/sender_config.json`:

```json
"application": {
  "directory": "sample_applications/full_screen_video"
}
```

Run `python send_app.py` from `iot_app_sender`. The video takes over the whole
monitor. LVGL starts again when it finishes, and the application draws its new
screen. Audio, controls, looping, and LVGL widgets over the video are not part
of this first version.
