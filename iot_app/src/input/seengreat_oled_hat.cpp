#include "iot/input/seengreat_oled_hat.h"

#include "controller_hardware_settings.h"
#include "seengreat_input_mapping.h"

#include <linux/gpio.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cstring>
#include <mutex>
#include <stdexcept>
#include <thread>

namespace iot {
namespace input {
namespace {

// Use the GPIO character API v1: the Buildroot toolchain's linux/gpio.h does
// not expose API v2.
std::weak_ptr<SeenGreatOledHat> sharedHat;
std::mutex                      hatMutex;

// Older toolchain headers may predate the bias flags even when the runtime
// kernel implements them. This bit is defined by the Linux GPIO v1 UAPI.
#ifndef GPIOHANDLE_REQUEST_BIAS_PULL_UP
#define GPIOHANDLE_REQUEST_BIAS_PULL_UP (1UL << 5)
#endif

void checkIoctl(int result, const char *action) {
  if (result < 0) {
    throw std::runtime_error(std::string(action) + ": " + std::strerror(errno));
  }
}

int requestLines(gpiohandle_request &request) {
  const int chip = ::open(hardwareSettings::gpioDevicePath, O_RDONLY | O_CLOEXEC);
  if (chip < 0) {
    throw std::runtime_error(std::string("Could not open ") + hardwareSettings::gpioDevicePath + ": " +
                             std::strerror(errno));
  }
  const int result     = ::ioctl(chip, GPIO_GET_LINEHANDLE_IOCTL, &request);
  const int savedError = errno;
  ::close(chip);
  errno = savedError;
  checkIoctl(result, "Could not claim SeenGreat GPIO lines");
  return request.fd;
}

} // namespace

SeenGreatOledHat::SeenGreatOledHat() {
  gpiohandle_request request{};
  request.lineoffsets[0] = hardwareSettings::seenGreatDataCommandPin;
  request.lineoffsets[1] = hardwareSettings::seenGreatResetPin;
  request.lines          = 2;
  std::strncpy(request.consumer_label, "iot-app-oled", sizeof(request.consumer_label) - 1);
  request.flags             = GPIOHANDLE_REQUEST_OUTPUT;
  request.default_values[0] = 1;
  request.default_values[1] = 1;
  m_outputLines             = requestLines(request);

  try {
    gpiohandle_data reset{};
    reset.values[0] = 1; // Keep GPIO25 HIGH throughout the reset pulse.
    reset.values[1] = 0;
    checkIoctl(::ioctl(m_outputLines, GPIOHANDLE_SET_LINE_VALUES_IOCTL, &reset), "OLED reset low");
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    reset.values[1] = 1;
    checkIoctl(::ioctl(m_outputLines, GPIOHANDLE_SET_LINE_VALUES_IOCTL, &reset), "OLED reset high");
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    m_oled = std::make_unique<hardware::I2cDevice>(hardwareSettings::seenGreatOledI2cBusNumber,
                                                   hardwareSettings::seenGreatOledI2cAddress);
    // Opening the I2C bus does not prove the OLED is present; this transfer does.
    m_oled->write({0x00, 0xa4});
  } catch (...) {
    ::close(m_outputLines);
    m_outputLines = -1;
    throw;
  }
}

std::shared_ptr<SeenGreatOledHat> SeenGreatOledHat::open() {
  std::lock_guard<std::mutex> lock(hatMutex);
  auto                        hat = sharedHat.lock();
  if (!hat) {
    hat.reset(new SeenGreatOledHat());
    sharedHat = hat;
  }
  return hat;
}

SeenGreatOledHat::~SeenGreatOledHat() {
  if (m_outputLines >= 0)
    ::close(m_outputLines);
}

hardware::II2cDevice &SeenGreatOledHat::oled() noexcept {
  return *m_oled;
}

SeenGreatOledHatController::SeenGreatOledHatController() : m_hat(SeenGreatOledHat::open()) {}

SeenGreatOledHatController::~SeenGreatOledHatController() {
  if (m_inputLines >= 0)
    ::close(m_inputLines);
}

const char *SeenGreatOledHatController::modelName() const noexcept {
  return "SeenGreat 1.3 inch OLED HAT";
}

const char *SeenGreatOledHatController::boardType() const noexcept {
  return "seengreat_oled_hat";
}

void SeenGreatOledHatController::connect() {
  if (m_inputLines >= 0)
    return;
  gpiohandle_request request{};
  for (std::size_t i = 0; i < hardwareSettings::seenGreatInputPins.size(); ++i) {
    request.lineoffsets[i] = hardwareSettings::seenGreatInputPins[i];
  }
  request.lines = hardwareSettings::seenGreatInputPins.size();
  std::strncpy(request.consumer_label, "iot-app-keys", sizeof(request.consumer_label) - 1);
  request.flags = GPIOHANDLE_REQUEST_INPUT | GPIOHANDLE_REQUEST_ACTIVE_LOW | GPIOHANDLE_REQUEST_BIAS_PULL_UP;
  m_inputLines  = requestLines(request);
  try {
    setJoystickCentreAndDeadZone({512, 512}, 100);
    refreshInputState();
  } catch (...) {
    ::close(m_inputLines);
    m_inputLines = -1;
    throw;
  }
}

void SeenGreatOledHatController::refreshInputState() {
  if (m_inputLines < 0)
    throw std::logic_error("SeenGreat controller is not connected");
  gpiohandle_data values{};
  checkIoctl(::ioctl(m_inputLines, GPIOHANDLE_GET_LINE_VALUES_IOCTL, &values), "Read SeenGreat keys");
  internal::SeenGreatPressedKeys pressedKeys;
  pressedKeys.up     = values.values[0] != 0;
  pressedKeys.down   = values.values[1] != 0;
  pressedKeys.left   = values.values[2] != 0;
  pressedKeys.right  = values.values[3] != 0;
  pressedKeys.centre = values.values[4] != 0;
  pressedKeys.key1   = values.values[5] != 0;
  pressedKeys.key2   = values.values[6] != 0;
  pressedKeys.key3   = values.values[7] != 0;

  const auto mappedInput = internal::mapSeenGreatPressedKeys(pressedKeys);
  updateJoystick(mappedInput.joystickPosition);
  updateButtons(mappedInput.pressedButtonsMask);
}

bool SeenGreatOledHatController::isConnected() const noexcept {
  return m_inputLines >= 0;
}

} // namespace input
} // namespace iot
