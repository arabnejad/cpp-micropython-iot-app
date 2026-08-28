SHELL := /bin/bash

.DEFAULT_GOAL := help

PROJECT_ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))

BUILD_JOBS ?= $(shell getconf _NPROCESSORS_ONLN 2>/dev/null || echo 1)

IOT_APP_BUILD ?= $(PROJECT_ROOT)/build/iot_app
IOT_TEST_BUILD ?= $(PROJECT_ROOT)/build/iot_app_tests
IOT_COVERAGE_BUILD ?= $(PROJECT_ROOT)/build/iot_app_coverage
CLANG_FORMAT ?= clang-format

IOT_FORMAT_DIRECTORIES := \
	$(PROJECT_ROOT)/iot_app/include \
	$(PROJECT_ROOT)/iot_app/micropython_iot_modules \
	$(PROJECT_ROOT)/iot_app/src \
	$(PROJECT_ROOT)/iot_app/tests

BUILDROOT_OUTPUT ?= /opt/iot-app-builds/raspberry-pi-4
BUILDROOT_EXTERNAL := $(PROJECT_ROOT)/iot_app/buildroot_external
BUILDROOT_COMMAND = env -u LD_LIBRARY_PATH $(MAKE) -C $(PROJECT_ROOT)/buildroot \
	BR2_EXTERNAL="$(BUILDROOT_EXTERNAL)" O="$(BUILDROOT_OUTPUT)"

.PHONY: help submodules format format-check iot-app test coverage buildroot-prepare buildroot-app buildroot-image

help:
	@echo "IoT project commands:"
	@echo
	@echo "  make submodules        Initialize the pinned Git submodules"
	@echo "  make format            Format the project C and C++ files"
	@echo "  make format-check      Check formatting without changing files"
	@echo "  make iot-app           Build IoT App for this Linux computer"
	@echo "  make test              Build and run all unit tests"
	@echo "  make coverage          Run tests and create HTML/XML coverage reports"
	@echo "  make buildroot-prepare Create the persistent Buildroot output directory"
	@echo "  make buildroot-app     Cross-compile only IoT App for Raspberry Pi"
	@echo "  make buildroot-image   Build or refresh the complete Raspberry Pi image"
	@echo
	@echo "Optional variables:"
	@echo "  BUILD_JOBS=$(BUILD_JOBS)"
	@echo "  BUILDROOT_OUTPUT=$(BUILDROOT_OUTPUT)"

submodules:
	git submodule sync --recursive
	git submodule update --init --recursive

format:
	@command -v "$(CLANG_FORMAT)" >/dev/null 2>&1 || { \
		echo "$(CLANG_FORMAT) was not found. Install clang-format or set CLANG_FORMAT." >&2; \
		exit 1; \
	}
	@find $(IOT_FORMAT_DIRECTORIES) -type f \
		\( -name '*.c' -o -name '*.cc' -o -name '*.cpp' -o -name '*.cxx' \
		-o -name '*.h' -o -name '*.hh' -o -name '*.hpp' -o -name '*.hxx' \) \
		-print0 | xargs -0 -r "$(CLANG_FORMAT)" -i

format-check:
	@command -v "$(CLANG_FORMAT)" >/dev/null 2>&1 || { \
		echo "$(CLANG_FORMAT) was not found. Install clang-format or set CLANG_FORMAT." >&2; \
		exit 1; \
	}
	@find $(IOT_FORMAT_DIRECTORIES) -type f \
		\( -name '*.c' -o -name '*.cc' -o -name '*.cpp' -o -name '*.cxx' \
		-o -name '*.h' -o -name '*.hh' -o -name '*.hpp' -o -name '*.hxx' \) \
		-print0 | xargs -0 -r "$(CLANG_FORMAT)" --dry-run --Werror

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

buildroot-prepare:
	@if [[ "$(BUILDROOT_OUTPUT)" == *"@"* ]]; then \
		echo "BUILDROOT_OUTPUT must not contain '@': $(BUILDROOT_OUTPUT)" >&2; \
		exit 1; \
	fi
	@if [ ! -d "$(BUILDROOT_OUTPUT)" ]; then \
		echo "Creating persistent Buildroot output directory: $(BUILDROOT_OUTPUT)"; \
		mkdir -p "$(BUILDROOT_OUTPUT)" 2>/dev/null || \
			sudo install -d -m 0755 -o "$$(id -u)" -g "$$(id -g)" "$(BUILDROOT_OUTPUT)"; \
	fi
	@if [ ! -w "$(BUILDROOT_OUTPUT)" ]; then \
		echo "Changing ownership of $(BUILDROOT_OUTPUT) to the current user"; \
		sudo chown "$$(id -u):$$(id -g)" "$(BUILDROOT_OUTPUT)"; \
	fi
	@if [ ! -f "$(BUILDROOT_OUTPUT)/.config" ]; then \
		echo "Loading the Raspberry Pi 4 Buildroot configuration"; \
		$(BUILDROOT_COMMAND) iot_rpi4_defconfig; \
	else \
		echo "Using existing Buildroot configuration: $(BUILDROOT_OUTPUT)/.config"; \
	fi

buildroot-app: buildroot-prepare
	$(BUILDROOT_COMMAND) iot_app-dirclean
	$(BUILDROOT_COMMAND) iot_app -j"$(BUILD_JOBS)"
	@echo "Raspberry Pi executable: $(BUILDROOT_OUTPUT)/target/usr/bin/iot_app"

buildroot-image: buildroot-prepare
	$(BUILDROOT_COMMAND) iot_app-dirclean
	$(BUILDROOT_COMMAND) all -j"$(BUILD_JOBS)"
	@echo "Raspberry Pi image: $(BUILDROOT_OUTPUT)/images/sdcard.img"
