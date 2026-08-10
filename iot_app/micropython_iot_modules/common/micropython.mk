IOT_COMMON_MODULE_DIR := $(USERMOD_DIR)

# Include the shared error helper when MicroPython scans module source files.
# This adds its RuntimeError text to MicroPython's generated string data.
SRC_USERMOD_C += $(IOT_COMMON_MODULE_DIR)/native_module_error.c
CFLAGS_USERMOD += -I$(IOT_COMMON_MODULE_DIR)
