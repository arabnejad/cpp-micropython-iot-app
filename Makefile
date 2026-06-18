SHELL := /bin/bash

.DEFAULT_GOAL := help

PROJECT_ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))

STORAGE_LAYOUT_CONFIGURATION := $(PROJECT_ROOT)/storage_layout.conf
include $(STORAGE_LAYOUT_CONFIGURATION)

# The image needs a small formatted data partition before it can be flashed.
# Users do not configure this value because the partition grows to the end of
# the SD card on first boot.
DATA_PARTITION_BOOTSTRAP_SIZE_MIB := 64

BUILD_JOBS ?= $(shell getconf _NPROCESSORS_ONLN 2>/dev/null || echo 1)

IOT_APP_BUILD ?= $(PROJECT_ROOT)/build/iot_app
IOT_TEST_BUILD ?= $(PROJECT_ROOT)/build/iot_app_tests
IOT_COVERAGE_BUILD ?= $(PROJECT_ROOT)/build/iot_app_coverage
CLANG_FORMAT ?= clang-format
BUILD_SCRIPTS_DIRECTORY := $(PROJECT_ROOT)/scripts/build

IOT_FORMAT_DIRECTORIES := \
	$(PROJECT_ROOT)/iot_app/include \
	$(PROJECT_ROOT)/iot_app/micropython_iot_modules \
	$(PROJECT_ROOT)/iot_app/src \
	$(PROJECT_ROOT)/iot_app/tests

PERSISTENT_BUILD_ROOT ?= /opt/iot-app-builds
IMAGE_OUTPUT_DIRECTORY ?= $(PERSISTENT_BUILD_ROOT)/images

BUILDROOT_OUTPUT ?= $(PERSISTENT_BUILD_ROOT)/buildroot-raspberry-pi-4
BUILDROOT_EXTERNAL := $(PROJECT_ROOT)/iot_app/buildroot_external
BUILDROOT_COMMAND = env -u LD_LIBRARY_PATH $(MAKE) -C $(PROJECT_ROOT)/buildroot \
	BR2_EXTERNAL="$(BUILDROOT_EXTERNAL)" O="$(BUILDROOT_OUTPUT)"

WIFI_CONFIGURATION_EXAMPLE := $(PROJECT_ROOT)/wpa_supplicant.conf.example
WIFI_CONFIGURATION := $(PROJECT_ROOT)/wpa_supplicant.conf

YOCTO_OUTPUT ?= $(PERSISTENT_BUILD_ROOT)/yocto-raspberry-pi-4
YOCTO_BUILD_DIRECTORY := $(YOCTO_OUTPUT)/build
YOCTO_SOURCE_LINK_DIRECTORY ?= $(PERSISTENT_BUILD_ROOT)/yocto-sources
YOCTO_DOWNLOAD_DIRECTORY ?= $(PERSISTENT_BUILD_ROOT)/yocto-downloads
YOCTO_SSTATE_DIRECTORY ?= $(PERSISTENT_BUILD_ROOT)/yocto-sstate-cache
YOCTO_DEPLOY_DIRECTORY := $(YOCTO_BUILD_DIRECTORY)/tmp/deploy/images/raspberrypi4-64
YOCTO_GENERATED_IMAGE := $(YOCTO_DEPLOY_DIRECTORY)/iot-app-image-raspberrypi4-64.rootfs.wic.xz
YOCTO_RASPBERRY_PI_IMAGER_IMAGE := $(IMAGE_OUTPUT_DIRECTORY)/iot-app-yocto-rpi4.img.xz

define RUN_YOCTO_COMMAND
	env -u LD_LIBRARY_PATH bash -c 'set -e; \
		source "$(PROJECT_ROOT)/poky/oe-init-build-env" "$(YOCTO_BUILD_DIRECTORY)" >/dev/null; \
		$(1)'
endef

.PHONY: help submodules format format-check iot-app test coverage wifi-prepare wifi-check storage-check \
	buildroot-prepare buildroot-app buildroot-image \
	yocto-prepare yocto-check yocto-app yocto-image images

help:
	@echo "IoT project commands:"
	@echo
	@echo "  make submodules        Initialize the pinned Git submodules"
	@echo "  make format            Format the project C and C++ files"
	@echo "  make format-check      Check formatting without changing files"
	@echo "  make iot-app           Build IoT App for this Linux computer"
	@echo "  make test              Build and run all unit tests"
	@echo "  make coverage          Run tests and create HTML/XML coverage reports"
	@echo "  make wifi-prepare      Create the shared private Wi-Fi configuration"
	@echo "  make storage-check     Check the shared image partition sizes"
	@echo "  make buildroot-prepare Prepare the persistent Buildroot configuration"
	@echo "  make buildroot-app     Cross-compile only IoT App for Raspberry Pi"
	@echo "  make buildroot-image   Build or refresh the complete Raspberry Pi image"
	@echo "  make yocto-prepare     Prepare the persistent Yocto configuration"
	@echo "  make yocto-check       Parse the Yocto configuration without building an image"
	@echo "  make yocto-app         Cross-compile only the Yocto IoT App package"
	@echo "  make yocto-image       Build the Yocto image for Raspberry Pi Imager"
	@echo "  make images            Build both Buildroot and Yocto images"
	@echo
	@echo "Optional variables:"
	@echo "  BUILD_JOBS=$(BUILD_JOBS)"
	@echo "  PERSISTENT_BUILD_ROOT=$(PERSISTENT_BUILD_ROOT)"
	@echo "  BUILDROOT_OUTPUT=$(BUILDROOT_OUTPUT)"
	@echo "  YOCTO_OUTPUT=$(YOCTO_OUTPUT)"
	@echo "  YOCTO_SOURCE_LINK_DIRECTORY=$(YOCTO_SOURCE_LINK_DIRECTORY)"
	@echo "  YOCTO_DOWNLOAD_DIRECTORY=$(YOCTO_DOWNLOAD_DIRECTORY)"
	@echo "  YOCTO_SSTATE_DIRECTORY=$(YOCTO_SSTATE_DIRECTORY)"
	@echo "  IMAGE_OUTPUT_DIRECTORY=$(IMAGE_OUTPUT_DIRECTORY)"
	@echo "  ROOT_PARTITION_SIZE_MIB=$(ROOT_PARTITION_SIZE_MIB)"

submodules:
	git submodule sync --recursive
	git submodule update --init --recursive

format:
	@"$(BUILD_SCRIPTS_DIRECTORY)/run-clang-format.sh" \
		format "$(CLANG_FORMAT)" $(IOT_FORMAT_DIRECTORIES)

format-check:
	@"$(BUILD_SCRIPTS_DIRECTORY)/run-clang-format.sh" \
		check "$(CLANG_FORMAT)" $(IOT_FORMAT_DIRECTORIES)

iot-app:
	cmake -S "$(PROJECT_ROOT)/iot_app" -B "$(IOT_APP_BUILD)" -DCMAKE_BUILD_TYPE=Release
	cmake --build "$(IOT_APP_BUILD)" --parallel "$(BUILD_JOBS)"

test:
	cmake -S "$(PROJECT_ROOT)/iot_app" -B "$(IOT_TEST_BUILD)" \
		-DCMAKE_BUILD_TYPE=Debug -DIOT_BUILD_TESTS=ON
	cmake --build "$(IOT_TEST_BUILD)" --parallel "$(BUILD_JOBS)"
	ctest --test-dir "$(IOT_TEST_BUILD)" --output-on-failure

coverage:
	cmake -S "$(PROJECT_ROOT)/iot_app" -B "$(IOT_COVERAGE_BUILD)" \
		-DCMAKE_BUILD_TYPE=Debug -DIOT_BUILD_TESTS=ON -DIOT_ENABLE_COVERAGE=ON
	cmake --build "$(IOT_COVERAGE_BUILD)" --target coverage --parallel "$(BUILD_JOBS)"

wifi-prepare:
	@if [ ! -f "$(WIFI_CONFIGURATION)" ]; then \
		install -m 0600 "$(WIFI_CONFIGURATION_EXAMPLE)" "$(WIFI_CONFIGURATION)"; \
		echo "Created private Wi-Fi configuration: $(WIFI_CONFIGURATION)"; \
		echo "Edit that file and replace YOUR_WIFI_NAME and YOUR_WIFI_PASSWORD before building an image."; \
	else \
		chmod 0600 "$(WIFI_CONFIGURATION)"; \
	fi

wifi-check: wifi-prepare
	@if grep -q 'YOUR_WIFI_NAME\|YOUR_WIFI_PASSWORD' "$(WIFI_CONFIGURATION)"; then \
		echo "Add the real Wi-Fi details to $(WIFI_CONFIGURATION) before building the image." >&2; \
		exit 1; \
	fi

storage-check:
	@if ! [[ "$(ROOT_PARTITION_SIZE_MIB)" =~ ^[0-9]+$$ ]]; then \
		echo "ROOT_PARTITION_SIZE_MIB must be a whole number of MiB: $(ROOT_PARTITION_SIZE_MIB)" >&2; \
		exit 1; \
	fi
	@if [ "$(ROOT_PARTITION_SIZE_MIB)" -lt 256 ]; then \
		echo "ROOT_PARTITION_SIZE_MIB must be at least 256 MiB" >&2; \
		exit 1; \
	fi
	@echo "Storage layout: $(ROOT_PARTITION_SIZE_MIB) MiB root; /data uses the remaining card space"

buildroot-prepare: wifi-prepare storage-check
	@PROJECT_ROOT="$(PROJECT_ROOT)" \
		BUILDROOT_OUTPUT="$(BUILDROOT_OUTPUT)" \
		IMAGE_OUTPUT_DIRECTORY="$(IMAGE_OUTPUT_DIRECTORY)" \
		ROOT_PARTITION_SIZE_MIB="$(ROOT_PARTITION_SIZE_MIB)" \
		DATA_PARTITION_BOOTSTRAP_SIZE_MIB="$(DATA_PARTITION_BOOTSTRAP_SIZE_MIB)" \
		"$(BUILD_SCRIPTS_DIRECTORY)/prepare-buildroot.sh"

buildroot-app: buildroot-prepare
	$(BUILDROOT_COMMAND) iot_app-dirclean
	$(BUILDROOT_COMMAND) iot_app -j"$(BUILD_JOBS)"
	@echo "Raspberry Pi executable: $(BUILDROOT_OUTPUT)/target/usr/bin/iot_app"

buildroot-image: buildroot-prepare wifi-check
	$(BUILDROOT_COMMAND) iot_app-dirclean
	$(BUILDROOT_COMMAND) all -j"$(BUILD_JOBS)"
	@echo "Raspberry Pi image: $(BUILDROOT_OUTPUT)/images/sdcard.img"
	@install -m 0644 "$(BUILDROOT_OUTPUT)/images/sdcard.img" \
		"$(IMAGE_OUTPUT_DIRECTORY)/iot-app-buildroot-rpi4.img"
	@echo "Raspberry Pi Imager file: $(IMAGE_OUTPUT_DIRECTORY)/iot-app-buildroot-rpi4.img"

yocto-prepare: wifi-prepare storage-check
	@PROJECT_ROOT="$(PROJECT_ROOT)" \
		YOCTO_BUILD_DIRECTORY="$(YOCTO_BUILD_DIRECTORY)" \
		YOCTO_SOURCE_LINK_DIRECTORY="$(YOCTO_SOURCE_LINK_DIRECTORY)" \
		YOCTO_DOWNLOAD_DIRECTORY="$(YOCTO_DOWNLOAD_DIRECTORY)" \
		YOCTO_SSTATE_DIRECTORY="$(YOCTO_SSTATE_DIRECTORY)" \
		IMAGE_OUTPUT_DIRECTORY="$(IMAGE_OUTPUT_DIRECTORY)" \
		ROOT_PARTITION_SIZE_MIB="$(ROOT_PARTITION_SIZE_MIB)" \
		DATA_PARTITION_BOOTSTRAP_SIZE_MIB="$(DATA_PARTITION_BOOTSTRAP_SIZE_MIB)" \
		"$(BUILD_SCRIPTS_DIRECTORY)/prepare-yocto.sh"

yocto-check: yocto-prepare
	$(call RUN_YOCTO_COMMAND,bitbake-layers show-layers)
	$(call RUN_YOCTO_COMMAND,bitbake -e iot-app-image >/dev/null)
	@echo "Yocto configuration parsed successfully"

yocto-app: yocto-prepare
	$(call RUN_YOCTO_COMMAND,bitbake iot-app)
	@echo "Yocto application packages: $(YOCTO_BUILD_DIRECTORY)/tmp/deploy/ipk"

yocto-image: yocto-prepare wifi-check
	$(call RUN_YOCTO_COMMAND,bitbake iot-app-image)
	@test -f "$(YOCTO_GENERATED_IMAGE)" || { \
		echo "Yocto did not create the expected image: $(YOCTO_GENERATED_IMAGE)" >&2; \
		exit 1; \
	}
	@install -m 0644 "$(YOCTO_GENERATED_IMAGE)" "$(YOCTO_RASPBERRY_PI_IMAGER_IMAGE)"
	@echo "Raspberry Pi Imager file: $(YOCTO_RASPBERRY_PI_IMAGER_IMAGE)"

images: buildroot-image yocto-image
