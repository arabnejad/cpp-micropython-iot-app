# IoT App gives mpv exclusive access to the monitor while a video plays.
# These options provide an OpenGL renderer through EGL, GBM, and Linux DRM
# without adding X11 or Wayland. Audio and Lua controls are not needed by the
# first video API.
PACKAGECONFIG = "drm gbm egl opengl"

# The upstream 0.35.1 recipe builds only the mpv command-line program by
# default. IoT App embeds mpv, so it also needs the shared library, headers,
# and mpv.pc file used by CMake. The version in this filename is intentional:
# a newer recipe may use different build options and should be reviewed before
# this append is updated for it.
EXTRA_OECONF:append = " --enable-libmpv-shared"
