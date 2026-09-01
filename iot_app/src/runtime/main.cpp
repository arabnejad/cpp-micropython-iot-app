#include "runtime_config.h"

#include "iot/display/display_manager.h"
#include "iot/logging/logger.h"
#include "iot/messaging/application_deployment_controller.h"
#include "iot/messaging/application_message_queue.h"
#include "iot/messaging/mqtt_application_receiver.h"
#include "iot/python/python_application_manager.h"
#include "iot/python/python_application_loader.h"
#include "iot/python/temporary_python_application_installer.h"
#include "iot/system/system_information.h"
#include "iot/ui/render_backend.h"
#include "iot/ui/screen_manager.h"

#include "messaging/internal/imqtt_client_api.h"

#include <algorithm>
#include <chrono>
#include <csignal>
#include <exception>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

volatile std::sig_atomic_t keepRunning              = 1;
constexpr const char       preferredConnectorName[] = "HDMI-A-1";
iot::logging::Logger       applicationLogger;

extern "C" void stopApplication(int) {
  keepRunning = 0;
}

const iot::display::DisplayInfo *
choosePreferredDisplay(const std::vector<iot::display::DisplayInfo> &connectedDisplays) {
  for (const auto &displayInformation : connectedDisplays) {
    if (displayInformation.displayId.connectorName == preferredConnectorName) {
      return &displayInformation;
    }
  }
  if (connectedDisplays.empty()) {
    return nullptr;
  }

  IOT_LOG_WARNING(applicationLogger, preferredConnectorName, " is not connected; using ",
                  connectedDisplays.front().displayId.connectorName, " instead");
  return &connectedDisplays.front();
}

void printDisplaySummary(const iot::display::ActiveDisplay &activeDisplay) {
  const auto &displayInformation = activeDisplay.display();
  const auto &activeDisplayMode  = activeDisplay.mode();
  std::string displayDescription =
      "Display: " + displayInformation.displayId.connectorName + " on " + displayInformation.displayId.devicePath;
  if (!displayInformation.manufacturer.empty() || !displayInformation.model.empty()) {
    displayDescription += " (" + displayInformation.manufacturer + ' ' + displayInformation.model + ')';
  }
  IOT_LOG_INFO(applicationLogger, displayDescription);
  IOT_LOG_INFO(applicationLogger, "Active mode: ", activeDisplayMode.width, 'x', activeDisplayMode.height, '@',
               activeDisplayMode.refreshRateHz, " Hz");
}

} // namespace

int main(int argc, char **argv) {
  try {
    const auto runtimeConfig = iot::runtime::loadRuntimeConfig(argc, argv);
    if (runtimeConfig.showHelp) {
      IOT_LOG_INFO(applicationLogger, iot::runtime::runtimeUsage(argv[0]));
      return 0;
    }

    iot::display::DisplayManager                displayManager;
    iot::system::LinuxSystemInformationProvider systemInformationProvider;
    auto                                        connectedDisplays = displayManager.connectedDisplays();
    const auto                                 *selectedDisplay   = choosePreferredDisplay(connectedDisplays);
    if (selectedDisplay == nullptr) {
      throw std::runtime_error("No connected DRM display was found");
    }

    const auto activeDisplay = displayManager.readActiveDisplay(selectedDisplay->displayId);
    for (auto &displayInformation : connectedDisplays) {
      if (displayInformation.displayId == activeDisplay.display().displayId) {
        displayInformation = activeDisplay.display();
        break;
      }
    }
    printDisplaySummary(activeDisplay);
    std::signal(SIGINT, stopApplication);
    std::signal(SIGTERM, stopApplication);

    const iot::python::PythonApplicationLoader applicationLoader{iot::runtime::maximumPythonSourceSizeInBytes};
    IOT_LOG_INFO(applicationLogger, "Loading default application: ", runtimeConfig.defaultApplicationDirectory);
    const auto defaultPythonApplication = applicationLoader.load(runtimeConfig.defaultApplicationDirectory);

    iot::ui::ScreenManager screenManager{activeDisplay, iot::ui::makeLvglFramebufferRenderBackend(),
                                         iot::runtime::maximumPendingRenderCommands};
    screenManager.start();

    iot::python::PythonApplicationManager pythonApplicationManager{
        screenManager, activeDisplay, std::move(connectedDisplays), systemInformationProvider,
        iot::runtime::pythonHeapSizeInBytes};
    pythonApplicationManager.startDefaultApplication(defaultPythonApplication);

    IOT_LOG_INFO(applicationLogger, "Running Python app '", pythonApplicationManager.activeScreenName(),
                 "'. Press Ctrl+C to stop");

    iot::messaging::ApplicationMessageQueue applicationMessageQueue{iot::runtime::maximumQueuedApplicationMessages};
    iot::messaging::MqttApplicationReceiverSettings mqttSettings;
    mqttSettings.deviceId                  = runtimeConfig.deviceId;
    mqttSettings.brokerHost                = runtimeConfig.mqttBrokerHost;
    mqttSettings.brokerPort                = runtimeConfig.mqttBrokerPort;
    mqttSettings.keepAliveSeconds          = iot::runtime::mqttKeepAliveSeconds;
    mqttSettings.username                  = runtimeConfig.mqttUsername;
    mqttSettings.password                  = runtimeConfig.mqttPassword;
    mqttSettings.maximumMessageSizeInBytes = iot::runtime::maximumMqttMessageSizeInBytes;

    iot::messaging::MqttApplicationReceiver mqttApplicationReceiver{std::move(mqttSettings), applicationMessageQueue,
                                                                    iot::messaging::internal::mqttClientApi()};
    iot::python::TemporaryPythonApplicationInstaller temporaryApplicationInstaller{
        iot::python::defaultTemporaryApplicationRoot()};
    iot::messaging::ApplicationDeploymentController deploymentController{
        runtimeConfig.deviceId,
        iot::messaging::ApplicationDeploymentMessageParser{iot::runtime::maximumPythonSourceSizeInBytes},
        temporaryApplicationInstaller,
        pythonApplicationManager,
        mqttApplicationReceiver,
        iot::runtime::maximumRememberedDeployments};

    try {
      mqttApplicationReceiver.start();
    } catch (const std::exception &error) {
      // Keep the default dashboard running even when MQTT cannot start.
      IOT_LOG_ERROR(applicationLogger, "MQTT application receiver could not start: ", error.what());
    }

    constexpr auto maximumMainLoopWait = std::chrono::milliseconds(1000);
    while (keepRunning != 0) {
      screenManager.throwIfRenderThreadFailed();
      const auto scheduledDelay = pythonApplicationManager.timeUntilNextScheduledCallback();
      const auto waitDuration   = scheduledDelay ? std::min(*scheduledDelay, maximumMainLoopWait) : maximumMainLoopWait;
      const auto receivedMessage = applicationMessageQueue.waitAndPopMessage(waitDuration);
      if (receivedMessage) {
        deploymentController.process(*receivedMessage);
      }

      pythonApplicationManager.runScheduledCallbacks();
    }

    mqttApplicationReceiver.stop();
    pythonApplicationManager.stop();
    screenManager.stop();
    return 0;
  } catch (const std::exception &error) {
    IOT_LOG_ERROR(applicationLogger, "iot_app failed: ", error.what());
    return 1;
  }
}
