#include "iot/video/iexclusive_video_player.h"

#include <cstddef>
#include <stdexcept>
#include <string>
#include <utility>

#include <mpv/client.h>

/* Check for service shutdown at least ten times per second during playback. */
constexpr double shutdownCheckIntervalInSeconds = 0.1;
/* Keep mpv error details useful without allowing its log to grow indefinitely. */
constexpr std::size_t maximumRememberedMpvErrorSizeInBytes = 1024U;
/* This is the hardware-decoding mode tested on the Raspberry Pi 4 images. */
constexpr char raspberryPiHardwareDecoderName[] = "v4l2m2m-copy";

namespace iot {
namespace video {
namespace {

/* Stops mpv and releases its display resources when playback ends. */
struct MpvHandleDeleter {
  void operator()(mpv_handle *mpvHandle) const noexcept {
    if (mpvHandle != nullptr) {
      mpv_terminate_destroy(mpvHandle);
    }
  }
};

using OwnedMpvHandle = std::unique_ptr<mpv_handle, MpvHandleDeleter>;

void throwIfMpvCallFailed(int mpvResult, const std::string &operation) {
  if (mpvResult < 0) {
    throw std::runtime_error(operation + ": " + mpv_error_string(mpvResult));
  }
}

void setMpvOption(mpv_handle *mpvHandle, const char *optionName, const std::string &optionValue) {
  throwIfMpvCallFailed(mpv_set_option_string(mpvHandle, optionName, optionValue.c_str()),
                       std::string{"Could not set mpv option "} + optionName);
}

void appendTextWithinLimit(std::string &destination, const char *text, std::size_t maximumSize) {
  if (text == nullptr) {
    return;
  }
  for (std::size_t textIndex = 0U; text[textIndex] != '\0' && destination.size() < maximumSize; ++textIndex) {
    const char character = text[textIndex];
    destination.push_back(character == '\n' || character == '\r' || character == '\t' ? ' ' : character);
  }
}

void rememberMpvErrorMessage(std::string &rememberedErrors, const mpv_event_log_message *logMessage) {
  if (logMessage == nullptr || logMessage->text == nullptr ||
      rememberedErrors.size() >= maximumRememberedMpvErrorSizeInBytes) {
    return;
  }

  if (!rememberedErrors.empty()) {
    appendTextWithinLimit(rememberedErrors, " | ", maximumRememberedMpvErrorSizeInBytes);
  }
  if (logMessage->prefix != nullptr && logMessage->prefix[0] != '\0') {
    appendTextWithinLimit(rememberedErrors, logMessage->prefix, maximumRememberedMpvErrorSizeInBytes);
    appendTextWithinLimit(rememberedErrors, ": ", maximumRememberedMpvErrorSizeInBytes);
  }
  appendTextWithinLimit(rememberedErrors, logMessage->text, maximumRememberedMpvErrorSizeInBytes);

  while (!rememberedErrors.empty() && rememberedErrors.back() == ' ') {
    rememberedErrors.pop_back();
  }
}

std::string displayModeForMpv(const display::DisplayMode &displayMode) {
  return std::to_string(displayMode.width) + "x" + std::to_string(displayMode.height) + '@' +
         std::to_string(displayMode.refreshRateHz);
}

void configureMpvForDisplay(mpv_handle *mpvHandle, const display::ActiveDisplay &activeDisplay) {
  /*
   * These are the libmpv equivalents of the command that played smoothly
   * on the Raspberry Pi 4. The installed mpv build listed v4l2m2m-copy as
   * an available mode and reported hardware decoding with no dropped frames.
   * mpv_create() already disables config files, terminal input, and default
   * keyboard bindings for an embedded player. Use explicit scaling options
   * instead of the named fast profile because Yocto's mpv 0.35.1 does not
   * provide that profile, while Buildroot's mpv 0.40.0 does.
   */
  setMpvOption(mpvHandle, "osd-level", "0");
  setMpvOption(mpvHandle, "audio", "no");
  setMpvOption(mpvHandle, "fullscreen", "yes");
  setMpvOption(mpvHandle, "vo", "gpu");
  setMpvOption(mpvHandle, "gpu-api", "opengl");
  setMpvOption(mpvHandle, "gpu-context", "drm");
  setMpvOption(mpvHandle, "hwdec", raspberryPiHardwareDecoderName);
  setMpvOption(mpvHandle, "scale", "bilinear");
  setMpvOption(mpvHandle, "dscale", "bilinear");
  setMpvOption(mpvHandle, "drm-device", activeDisplay.display().displayId.devicePath);
  setMpvOption(mpvHandle, "drm-connector", activeDisplay.display().displayId.connectorName);
  setMpvOption(mpvHandle, "drm-mode", displayModeForMpv(activeDisplay.mode()));
}

class MpvExclusiveVideoPlayer final : public IExclusiveVideoPlayer {
public:
  explicit MpvExclusiveVideoPlayer(std::function<bool()> applicationShutdownRequested)
      : m_applicationShutdownRequested(std::move(applicationShutdownRequested)) {}

  void playVideoAndWait(const std::filesystem::path  &videoFilePath,
                        const display::ActiveDisplay &activeDisplay) override {
    OwnedMpvHandle mpvHandle{mpv_create()};
    if (!mpvHandle) {
      throw std::runtime_error("Could not create the mpv video player");
    }

    throwIfMpvCallFailed(mpv_request_log_messages(mpvHandle.get(), "error"), "Could not enable mpv error reporting");
    configureMpvForDisplay(mpvHandle.get(), activeDisplay);
    throwIfMpvCallFailed(mpv_initialize(mpvHandle.get()), "Could not initialize mpv");

    const std::string absoluteVideoFilePath = videoFilePath.string();
    const char       *loadFileCommand[]     = {"loadfile", absoluteVideoFilePath.c_str(), nullptr};
    throwIfMpvCallFailed(mpv_command(mpvHandle.get(), loadFileCommand), "Could not ask mpv to load the video");

    waitForPlaybackToFinish(mpvHandle.get());
  }

private:
  void waitForPlaybackToFinish(mpv_handle *mpvHandle) const {
    std::string rememberedMpvErrors;
    while (true) {
      if (m_applicationShutdownRequested && m_applicationShutdownRequested()) {
        return;
      }

      const mpv_event *event = mpv_wait_event(mpvHandle, shutdownCheckIntervalInSeconds);

      if (event->event_id == MPV_EVENT_SHUTDOWN) {
        throw std::runtime_error("mpv stopped before the video finished");
      }
      if (event->event_id == MPV_EVENT_LOG_MESSAGE) {
        rememberMpvErrorMessage(rememberedMpvErrors, static_cast<const mpv_event_log_message *>(event->data));
        continue;
      }
      if (event->event_id != MPV_EVENT_END_FILE) {
        continue;
      }

      const auto *endOfFile = static_cast<const mpv_event_end_file *>(event->data);
      if (endOfFile == nullptr) {
        throw std::runtime_error("mpv did not provide the final playback result");
      }
      if (endOfFile->reason == MPV_END_FILE_REASON_ERROR) {
        std::string playbackError = std::string{"Video playback failed: "} + mpv_error_string(endOfFile->error);
        if (!rememberedMpvErrors.empty()) {
          playbackError += "; mpv reported: " + rememberedMpvErrors;
        }
        throw std::runtime_error(playbackError);
      }
      if (endOfFile->reason != MPV_END_FILE_REASON_EOF) {
        throw std::runtime_error("Video playback stopped before reaching the end of the file");
      }
      return;
    }
  }

  std::function<bool()> m_applicationShutdownRequested;
};

} // namespace

std::unique_ptr<IExclusiveVideoPlayer> makeMpvExclusiveVideoPlayer(std::function<bool()> applicationShutdownRequested) {
  return std::make_unique<MpvExclusiveVideoPlayer>(std::move(applicationShutdownRequested));
}

} // namespace video
} // namespace iot
