# This file extends Yocto's existing Mosquitto recipe. The % in the file name
# means that the changes apply to any Mosquitto version selected by Yocto.
#
# IoT App receives Python applications through MQTT. The standard Mosquitto
# package does not include this project's broker configuration, so this file
# adds it while Yocto builds the package.
#
# Configuration flow:
#
#   This Yocto layer:
#   files/iot-app-mosquitto.conf
#           |
#           | mosquitto_%.bbappend adds and installs the file
#           v
#   Generated image:
#   /etc/mosquitto/mosquitto.conf
#           |
#           v
#   mosquitto.service reads it when the MQTT broker starts

# Allow SRC_URI to find files stored in the files directory beside this file.
FILESEXTRAPATHS:prepend := "${THISDIR}/files:"

# Add the IoT App broker configuration to Mosquitto's build inputs.
SRC_URI += "file://iot-app-mosquitto.conf"

# IoT App uses normal MQTT over TCP. Leaving WebSocket support out avoids
# pulling an unused web networking stack into the image.
PACKAGECONFIG:remove = "websockets"

# Install the project configuration as Mosquitto's main configuration file.
# It opens port 1883 on every IPv4 interface so iot_app_sender can reach the
# broker from another computer on the same development network.
do_install:append() {
    install -D -m 0644 "${WORKDIR}/iot-app-mosquitto.conf" \
        "${D}${sysconfdir}/mosquitto/mosquitto.conf"
}
