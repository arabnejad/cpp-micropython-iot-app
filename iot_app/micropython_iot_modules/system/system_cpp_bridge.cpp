#include "system_cpp_bridge.h"

#include "native_bridge_context.h"
#include "native_bridge_error_handler.h"

#include "py/misc.h"
#include <lvgl.h>

#include <stdexcept>
#include <string>
#include <vector>

#ifndef IOT_APP_VERSION
#define IOT_APP_VERSION "unknown"
#endif

namespace {

thread_local std::string                                           currentLocalTimeText;
thread_local std::vector<iot::system::NetworkInterfaceInformation> latestNetworkInterfaces;
/* System-information failures become RuntimeError messages in mod_iot_system.c. */
thread_local iot::python::internal::NativeBridgeErrorHandler nativeBridgeErrorHandler{
    "Unknown C++ system-information error"};

constexpr const char lvglVersion[] =
    MP_STRINGIFY(LVGL_VERSION_MAJOR) "." MP_STRINGIFY(LVGL_VERSION_MINOR) "." MP_STRINGIFY(LVGL_VERSION_PATCH);
constexpr const char micropythonVersion[] =
    MP_STRINGIFY(MICROPY_VERSION_MAJOR) "." MP_STRINGIFY(MICROPY_VERSION_MINOR) "." MP_STRINGIFY(MICROPY_VERSION_MICRO);

iot::python::MicroPythonApplicationContext &context() {
  return iot::python::internal::requireActiveApplicationContext("system");
}

} // namespace

extern "C" iot_native_result_t iot_system_read_information(iot_system_information_t *systemInformation) {
  return nativeBridgeErrorHandler.runSafely([=] {
    if (systemInformation == nullptr) {
      throw std::invalid_argument("System-information output is missing");
    }

    const auto &systemInformationSnapshot           = context().systemInformation();
    systemInformation->hostname                     = systemInformationSnapshot.hostname.c_str();
    systemInformation->device_model                 = systemInformationSnapshot.deviceModel.c_str();
    systemInformation->operating_system             = systemInformationSnapshot.operatingSystem.c_str();
    systemInformation->kernel_version               = systemInformationSnapshot.kernelVersion.c_str();
    systemInformation->architecture                 = systemInformationSnapshot.architecture.c_str();
    systemInformation->uptime_seconds               = systemInformationSnapshot.uptimeSeconds;
    systemInformation->logical_cpu_count            = systemInformationSnapshot.logicalCpuCount;
    systemInformation->has_cpu_temperature          = systemInformationSnapshot.cpuTemperatureCelsius.has_value();
    systemInformation->cpu_temperature_celsius      = systemInformationSnapshot.cpuTemperatureCelsius.value_or(0.0);
    systemInformation->has_one_minute_load_average  = systemInformationSnapshot.oneMinuteLoadAverage.has_value();
    systemInformation->one_minute_load_average      = systemInformationSnapshot.oneMinuteLoadAverage.value_or(0.0);
    systemInformation->total_memory_bytes           = systemInformationSnapshot.totalMemoryBytes;
    systemInformation->available_memory_bytes       = systemInformationSnapshot.availableMemoryBytes;
    systemInformation->root_storage_total_bytes     = systemInformationSnapshot.rootStorageTotalBytes;
    systemInformation->root_storage_available_bytes = systemInformationSnapshot.rootStorageAvailableBytes;
    systemInformation->i2c_interface_count          = systemInformationSnapshot.i2cInterfaceCount;
    systemInformation->gpio_controller_count        = systemInformationSnapshot.gpioControllerCount;
    systemInformation->spi_interface_count          = systemInformationSnapshot.spiInterfaceCount;
    systemInformation->serial_interface_count       = systemInformationSnapshot.serialInterfaceCount;
    systemInformation->usb_device_count             = systemInformationSnapshot.usbDeviceCount;
    systemInformation->input_device_count           = systemInformationSnapshot.inputDeviceCount;
    systemInformation->block_device_count           = systemInformationSnapshot.blockDeviceCount;
    systemInformation->application_name             = context().applicationName().c_str();
    systemInformation->app_version                  = IOT_APP_VERSION;
    systemInformation->micropython_version          = micropythonVersion;
    systemInformation->lvgl_version                 = lvglVersion;
  });
}

extern "C" iot_native_result_t iot_system_current_time(const char **formatted_time) {
  return nativeBridgeErrorHandler.runSafely([=] {
    if (formatted_time == nullptr) {
      throw std::invalid_argument("Current-time output is missing");
    }

    currentLocalTimeText = context().readCurrentLocalTime();
    *formatted_time      = currentLocalTimeText.c_str();
  });
}

extern "C" iot_native_result_t iot_system_uptime_seconds(uint64_t *uptime_seconds) {
  return nativeBridgeErrorHandler.runSafely([=] {
    if (uptime_seconds == nullptr) {
      throw std::invalid_argument("System-uptime output is missing");
    }
    *uptime_seconds = context().currentUptimeSeconds();
  });
}

extern "C" iot_native_result_t iot_system_network_interface_count(size_t *networkInterfaceCount) {
  return nativeBridgeErrorHandler.runSafely([=] {
    if (networkInterfaceCount == nullptr) {
      throw std::invalid_argument("Network-interface count output is missing");
    }
    // Keep this reading until MicroPython has copied each interface below.
    latestNetworkInterfaces = context().readCurrentNetworkInterfaces();
    *networkInterfaceCount  = latestNetworkInterfaces.size();
  });
}

extern "C" iot_native_result_t
iot_system_read_network_interface(size_t index, iot_network_interface_information_t *networkInterfaceInformation) {
  return nativeBridgeErrorHandler.runSafely([=] {
    if (networkInterfaceInformation == nullptr) {
      throw std::invalid_argument("Network-interface output is missing");
    }
    if (index >= latestNetworkInterfaces.size()) {
      throw std::out_of_range("Network-interface index is outside the latest reading");
    }

    const auto &networkInterface              = latestNetworkInterfaces[index];
    networkInterfaceInformation->name         = networkInterface.name.c_str();
    networkInterfaceInformation->connected    = networkInterface.connected ? 1 : 0;
    networkInterfaceInformation->ipv4_address = networkInterface.ipv4Address.c_str();
    networkInterfaceInformation->has_speed    = networkInterface.speedMegabitsPerSecond.has_value() ? 1 : 0;
    networkInterfaceInformation->speed_megabits_per_second = networkInterface.speedMegabitsPerSecond.value_or(0U);
  });
}
