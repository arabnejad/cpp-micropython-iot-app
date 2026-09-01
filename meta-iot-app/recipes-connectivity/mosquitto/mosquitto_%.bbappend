# This file extends Yocto's existing Mosquitto recipe. The % in the file name
# means that the changes apply to any Mosquitto version selected by Yocto.
#
# IoT App receives Python applications through MQTT. The standard Mosquitto
# package does not include this project's broker configuration, so this file
# adds it while Yocto builds the package.
#
# Configuration flow:
#
#   Shared image support:
#   iot_app/image_support/mosquitto.conf
#           |
#           | mosquitto_%.bbappend adds and installs the file
#           v
#   Generated image:
#   /etc/mosquitto/mosquitto.conf
#           |
#           v
#   mosquitto.service reads it when the MQTT broker starts

# Read the same broker configuration used by the Buildroot image.
FILESEXTRAPATHS:prepend := "${IOT_APP_PROJECT_ROOT}/iot_app/image_support:"

# Add the IoT App broker configuration to Mosquitto's build inputs.
SRC_URI += "file://mosquitto.conf"

# IoT App uses normal MQTT over TCP. Leaving WebSocket support out avoids
# pulling an unused web networking stack into the image.
PACKAGECONFIG:remove = "websockets"

# Install the project configuration as Mosquitto's main configuration file.
# It opens port 1883 on every IPv4 interface so iot_app_sender can reach the
# broker from another computer on the same development network.
do_install:append() {
    install -D -m 0644 "${WORKDIR}/mosquitto.conf" \
        "${D}${sysconfdir}/mosquitto/mosquitto.conf"
}
