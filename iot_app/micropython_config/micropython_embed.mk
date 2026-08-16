MICROPYTHON_TOP ?= $(abspath ../../micropython)
PACKAGE_DIR ?= $(abspath ../generated/micropython_embed)
USER_C_MODULES ?= $(abspath ../micropython_iot_modules)

include $(MICROPYTHON_TOP)/ports/embed/embed.mk
