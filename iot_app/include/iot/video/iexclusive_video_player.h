#pragma once

#include "iot/display/display_types.h"

#include <filesystem>
#include <functional>
#include <memory>

namespace iot {
namespace video {

/*
 * Plays one video directly on the selected monitor.
 *
 * ScreenManager stops LVGL before calling this interface, so the player has
 * exclusive access to the display. The call returns when the video finishes
 * or application shutdown interrupts it. It throws an exception when
 * playback fails.
 */
class IExclusiveVideoPlayer {
public:
  virtual ~IExclusiveVideoPlayer() = default;

  IExclusiveVideoPlayer(const IExclusiveVideoPlayer &)            = delete;
  IExclusiveVideoPlayer &operator=(const IExclusiveVideoPlayer &) = delete;
  IExclusiveVideoPlayer(IExclusiveVideoPlayer &&)                 = delete;
  IExclusiveVideoPlayer &operator=(IExclusiveVideoPlayer &&)      = delete;

  virtual void playVideoAndWait(const std::filesystem::path  &videoFilePath,
                                const display::ActiveDisplay &activeDisplay) = 0;

protected:
  IExclusiveVideoPlayer() = default;
};

/*
 * Creates the libmpv player used by ScreenManager.
 *
 * applicationShutdownRequested lets a service stop interrupt a long video.
 * The player checks it while waiting for mpv events. An empty function lets
 * playback run until the file ends.
 */
std::unique_ptr<IExclusiveVideoPlayer>
makeMpvExclusiveVideoPlayer(std::function<bool()> applicationShutdownRequested = {});

} // namespace video
} // namespace iot
