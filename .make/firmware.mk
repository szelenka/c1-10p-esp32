# ============================================================================
# Firmware build / flash / monitor
# ============================================================================

FIRMWARE_ENV ?= esp32dev
UPLOAD_PORT ?=

define run_firmware_build
	if [ -x "./.scripts/pio_local.sh" ]; then \
		echo "Using PlatformIO local wrapper (env=$(FIRMWARE_ENV))"; \
		./.scripts/pio_local.sh run -e "$(FIRMWARE_ENV)"; \
	elif command -v pio >/dev/null 2>&1; then \
		echo "Using PlatformIO global CLI (env=$(FIRMWARE_ENV))"; \
		pio run -e "$(FIRMWARE_ENV)"; \
	elif command -v idf.py >/dev/null 2>&1; then \
		echo "Using ESP-IDF idf.py build"; \
		idf.py build; \
	else \
		echo "Error: no supported firmware tool found (expected ./.scripts/pio_local.sh, pio, or idf.py)." >&2; \
		exit 1; \
	fi
endef

define run_firmware_flash
	if [ -x "./.scripts/pio_local.sh" ]; then \
		echo "Using PlatformIO local wrapper flash/monitor (env=$(FIRMWARE_ENV))"; \
		if [ -n "$(UPLOAD_PORT)" ]; then \
			./.scripts/pio_local.sh run -e "$(FIRMWARE_ENV)" --upload-port "$(UPLOAD_PORT)" -t upload; \
		else \
			./.scripts/pio_local.sh run -e "$(FIRMWARE_ENV)" -t upload; \
		fi; \
	elif command -v pio >/dev/null 2>&1; then \
		echo "Using PlatformIO global CLI flash/monitor (env=$(FIRMWARE_ENV))"; \
		if [ -n "$(UPLOAD_PORT)" ]; then \
			pio run -e "$(FIRMWARE_ENV)" --upload-port "$(UPLOAD_PORT)" -t upload; \
		else \
			pio run -e "$(FIRMWARE_ENV)" -t upload; \
		fi; \
	elif command -v idf.py >/dev/null 2>&1; then \
		echo "Using ESP-IDF idf.py flash monitor"; \
		idf.py flash monitor; \
	else \
		echo "Error: no supported firmware tool found (expected ./.scripts/pio_local.sh, pio, or idf.py)." >&2; \
		exit 1; \
	fi
endef

define run_firmware_monitor
	if [ -x "./.scripts/pio_local.sh" ]; then \
		echo "Using PlatformIO local wrapper monitor (env=$(FIRMWARE_ENV))"; \
		if [ -n "$(UPLOAD_PORT)" ]; then \
			./.scripts/pio_local.sh run -e "$(FIRMWARE_ENV)" --upload-port "$(UPLOAD_PORT)" -t monitor; \
		else \
			./.scripts/pio_local.sh run -e "$(FIRMWARE_ENV)" -t monitor; \
		fi; \
	elif command -v pio >/dev/null 2>&1; then \
		echo "Using PlatformIO global CLI monitor (env=$(FIRMWARE_ENV))"; \
		if [ -n "$(UPLOAD_PORT)" ]; then \
			pio run -e "$(FIRMWARE_ENV)" --upload-port "$(UPLOAD_PORT)" -t monitor; \
		else \
			pio run -e "$(FIRMWARE_ENV)" -t monitor; \
		fi; \
	elif command -v idf.py >/dev/null 2>&1; then \
		echo "Using ESP-IDF idf.py monitor"; \
		if [ -n "$(UPLOAD_PORT)" ]; then \
			idf.py -p "$(UPLOAD_PORT)" monitor; \
		else \
			idf.py monitor; \
		fi; \
	else \
		echo "Error: no supported firmware tool found (expected ./.scripts/pio_local.sh, pio, or idf.py)." >&2; \
		exit 1; \
	fi
endef

build:
	@set -e; $(run_firmware_build)

flash:
	@set -e; $(run_firmware_flash)

monitor:
	@set -e; $(run_firmware_monitor)
